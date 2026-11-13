/*
 * lp104mqttv2.c — Cliente MQTT 3.1.1 con telemetría para Teltonika RUT200 (V2)
 *
 * Extiende el V1 (lp104mqtt.c, auditado APROBADO) reutilizando íntegramente su
 * arquitectura MQTT (socket TCP, MQTT 3.1.1 a mano, QoS 0, keepalive, backoff,
 * señales) y añadiendo la publicación de un JSON de telemetría.
 *
 * Publica cada 30 s (y de forma inmediata al conectar/reconectar):
 *   {MQTT_CLIENT}/report/online = "true"     (mismo payload que V1 -> control web intacto)
 *   {MQTT_CLIENT}/report/status = {"signal":..,"users":..,"cpu":..,"temp":..,"ts":..}
 *
 * Métricas (ver PLAN.md §4 y §7):
 *   signal -> `gsmctl -q`  (RSSI en dBm)                      | null si falla/N/A
 *   users  -> clientes asociados al AP (iwinfo assoclist)     | 0 si no hay AP (router STA)
 *   cpu    -> delta idle/total de /proc/stat (lectura directa)| null si falla
 *   temp   -> `gsmctl -c` / 10.0 (temperatura del MÓDEM, °C)  | null si falla
 *
 * Notas de plataforma (verificadas en el router, RUT2M_R_00.07.22.3):
 *   - El MT7628AN NO expone /sys/class/thermal/thermal_zoneN/temp (N=indice) ->
 *     se usa gsmctl -c como métrica de temperatura del módem.
 *   - En este despliegue la única interfaz WiFi (wlan0-1) está en modo STA (cliente),
 *     por lo que no hay AP y `users` = 0 (0 clientes asociados al AP). Si existiera un
 *     AP (Mode: Master) se contarían sus estaciones.
 *
 * `popen()` se usa SOLO con cadenas de comando constantes/hardcodeadas (sin
 * interpolación de datos de red ni metacaracteres de shell). La CPU se lee con
 * fopen()/sscanf() (sin popen). No se usa system().
 *
 * Cross-compile: ver Makefile (SDK OpenWrt 21.02.0 ramips/mt76x8, mipsel_24kc, musl).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "mqtt_config.h"

/* ------------------------------------------------------------------ */
/* Constantes de comportamiento                                        */
/* ------------------------------------------------------------------ */
#define REPORT_INTERVAL_S   30      /* intervalo de publicación          */
#define MQTT_KEEPALIVE_S    60      /* keepalive declarado en CONNECT    */
#define PING_THRESHOLD_S    30      /* PINGREQ si inactivo > keepalive/2 */
#define CONNECT_TIMEOUT_S   10      /* timeout DNS/connect/CONNACK       */
#define BACKOFF_INITIAL_S   5       /* delay inicial de reconexión       */
#define BACKOFF_MAX_S       60      /* delay máximo de reconexión        */

#define TOPIC_ONLINE_SUFFIX "report/online"
#define TOPIC_STATUS_SUFFIX "report/status"
#define REPORT_PAYLOAD      "true"
#define REPORT_PAYLOAD_LEN  4       /* strlen("true"), sin '\0'          */

#define RXBUF_SIZE          512
#define WRITE_TIMEOUT_S     5       /* poll(POLLOUT) breve al escribir   */

/* Ruta del archivo de configuración (V2.1): credenciales MQTT en runtime.
 * Sobreescribible con la variable de entorno LP104MQTTV2_CONF y/o con el
 * flag de compilación -DCONFIG_PATH="...". */
#ifndef CONFIG_PATH
#define CONFIG_PATH "/etc/lp104mqttv2.conf"
#endif

/* Tamaños de los campos del struct mqtt_cfg (guard de snprintf). */
#define CFG_BROKER_LEN   64
#define CFG_CLIENT_LEN   64
#define CFG_USER_LEN     64
#define CFG_PASS_LEN     128

/* Comandos del sistema (constantes, rutas absolutas verificadas en RutOS). */
#define CMD_GSM_SIGNAL   "/usr/sbin/gsmctl -q"
#define CMD_GSM_TEMP     "/usr/sbin/gsmctl -c"
#define CMD_LS_NET       "/bin/ls /sys/class/net/"
#define PATH_PROC_STAT   "/proc/stat"

/* ------------------------------------------------------------------ */
/* Estado global                                                       */
/* ------------------------------------------------------------------ */
static volatile sig_atomic_t g_running = 1;
static char g_topic_online[256];    /* {client_id}/report/online */
static char g_topic_status[256];    /* {client_id}/report/status */

/* ------------------------------------------------------------------ */
/* Configuración en runtime (V2.1)                                     */
/* ------------------------------------------------------------------ */
struct mqtt_cfg {
    char broker[CFG_BROKER_LEN];
    int  port;
    char client_id[CFG_CLIENT_LEN];
    char user[CFG_USER_LEN];
    char pass[CFG_PASS_LEN];
};

/* log_msg() se define más abajo; se declara aquí para config_load(). */
static void log_msg(const char *level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Defaults compilados (no sensibles) + user/pass vacíos; config_load()
 * los sobreescribe con los valores del archivo de configuración. */
static struct mqtt_cfg g_cfg = {
    .broker    = MQTT_BROKER,
    .port      = MQTT_PORT,
    .client_id = MQTT_CLIENT,
    .user      = "",
    .pass      = "",
};

/* Recorta espacios/tabs/CR/LF de ambos extremos de la cadena (in-place). */
static void trim_ws(char *s)
{
    char *start = s;
    char *end;

    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
}

/* Lee /etc/lp104mqttv2.conf (clave=valor) y rellena cfg.
 *
 * - Formato: líneas "clave=valor"; comentarios '#'; líneas vacías ignoradas.
 * - Claves reconocidas (exactas): user, pass, client_id, broker, port.
 * - port se valida numérico (1..65535); si es inválido -> WARNING y default.
 * - Línea >255 bytes -> WARNING y se ignora esa línea.
 * - Devuelve 0 si el archivo se abrió y parseó; -1 si no existe / no se
 *   pudo abrir (entonces cfg conserva los defaults; user/pass vacíos).
 * - NUNCA se loguea el valor de `pass` (solo `user`). */
static int config_load(struct mqtt_cfg *cfg)
{
    const char *path = getenv("LP104MQTTV2_CONF");
    if (!path || !*path)
        path = CONFIG_PATH;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        log_msg("WARNING", "no existe %s (%s); usando defaults compilados",
                path, strerror(errno));
        return -1;
    }

    char line[256];
    int lineno = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;

        /* Línea demasiado larga (sin '\n' y sin EOF): drenar el resto. */
        if (!strchr(line, '\n') && !feof(fp)) {
            log_msg("WARNING", "%s:%d línea >%zu bytes, ignorada",
                    path, lineno, sizeof(line) - 1);
            int c;
            while ((c = fgetc(fp)) != EOF && c != '\n')
                ;
            continue;
        }

        trim_ws(line);
        if (*line == '\0' || *line == '#')
            continue;

        char *eq = strchr(line, '=');
        if (!eq) {
            log_msg("WARNING", "%s:%d línea sin '=': ignorada", path, lineno);
            continue;
        }

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_ws(key);
        trim_ws(val);

        if (*key == '\0') {
            log_msg("WARNING", "%s:%d clave vacía: ignorada", path, lineno);
            continue;
        }

        if (strcmp(key, "user") == 0) {
            snprintf(cfg->user, sizeof(cfg->user), "%s", val);
        } else if (strcmp(key, "pass") == 0) {
            snprintf(cfg->pass, sizeof(cfg->pass), "%s", val);
        } else if (strcmp(key, "client_id") == 0) {
            snprintf(cfg->client_id, sizeof(cfg->client_id), "%s", val);
        } else if (strcmp(key, "broker") == 0) {
            snprintf(cfg->broker, sizeof(cfg->broker), "%s", val);
        } else if (strcmp(key, "port") == 0) {
            char *end = NULL;
            long p = strtol(val, &end, 10);
            if (end == val || *end != '\0' || p < 1 || p > 65535) {
                log_msg("WARNING", "%s:%d port inválido '%s'; "
                        "conservando %d", path, lineno, val, cfg->port);
            } else {
                cfg->port = (int)p;
            }
        } else {
            log_msg("WARNING", "%s:%d clave desconocida '%s': ignorada",
                    path, lineno, key);
        }
    }

    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */
static void log_msg(const char *level, const char *fmt, ...)
{
    char ts[32];
    time_t now = time(NULL);
    struct tm tm;

    localtime_r(&now, &tm);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(stderr, "[%s] [%s] ", ts, level);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
    fflush(stderr);
}

/* ------------------------------------------------------------------ */
/* Señales                                                             */
/* ------------------------------------------------------------------ */
static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void init_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignorar SIGPIPE: escribir en socket cerrado debe dar EPIPE, no matar. */
    signal(SIGPIPE, SIG_IGN);
}

/* ------------------------------------------------------------------ */
/* Codificación de remaining length (entero de longitud variable)      */
/* Devuelve el número de bytes escritos en out (1..4).                 */
/* ------------------------------------------------------------------ */
static int mqtt_remaining_length(size_t n, unsigned char *out)
{
    int i = 0;

    do {
        unsigned char b = (unsigned char)(n % 128);
        n /= 128;
        if (n > 0)
            b |= 0x80;
        out[i++] = b;
    } while (n > 0 && i < 4);

    return i;
}

/* ------------------------------------------------------------------ */
/* Escritura completa en el socket                                     */
/* ------------------------------------------------------------------ */
static int write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Buffer de envío lleno: esperar disponibilidad brevemente. */
                struct pollfd pfd = { fd, POLLOUT, 0 };
                int pr = poll(&pfd, 1, WRITE_TIMEOUT_S * 1000);
                if (pr > 0 && (pfd.revents & POLLOUT) &&
                    !(pfd.revents & (POLLERR | POLLHUP)))
                    continue;       /* reintentar el send */
                if (pr < 0 && errno == EINTR)
                    continue;
                if (pr == 0)
                    errno = ETIMEDOUT;
                return -1;          /* error o timeout real */
            }
            return -1;
        }
        if (n == 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Conexión TCP (DNS + socket + connect) con timeout                   */
/* ------------------------------------------------------------------ */
static int tcp_connect(const char *host, int port)
{
    struct addrinfo hints, *res = NULL, *rp;
    char portstr[16];
    int fd = -1;

    snprintf(portstr, sizeof(portstr), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0) {
        log_msg("ERROR", "DNS: no se pudo resolver %s: %s", host, gai_strerror(rc));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;

        /* Modo no bloqueante desde el inicio: connect() no respeta
         * SO_SNDTIMEO, así que se usa poll(POLLOUT) con deadline. */
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0)
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int rc2 = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc2 == 0)
            break;              /* éxito inmediato */

        if (rc2 < 0 && errno == EINPROGRESS) {
            /* connect() en curso: esperar POLLOUT con deadline. */
            struct pollfd pfd = { fd, POLLOUT, 0 };
            int pr = poll(&pfd, 1, CONNECT_TIMEOUT_S * 1000);
            if (pr > 0 && (pfd.revents & POLLOUT) &&
                !(pfd.revents & (POLLERR | POLLHUP))) {
                /* Verificar el resultado real del connect(). */
                int serr = 0;
                socklen_t elen = sizeof(serr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &serr, &elen) == 0 &&
                    serr == 0)
                    break;      /* connect() completado con éxito */
                log_msg("ERROR", "TCP: connect a %s:%d falló: %s",
                        host, port, strerror(serr ? serr : errno));
            } else if (pr == 0) {
                log_msg("ERROR", "TCP: timeout de connect a %s:%d (%ds)",
                        host, port, CONNECT_TIMEOUT_S);
            } else if (pr < 0 && errno != EINTR) {
                log_msg("ERROR", "TCP: poll de connect falló: %s",
                        strerror(errno));
            }
        } else if (rc2 < 0) {
            log_msg("ERROR", "TCP: connect a %s:%d falló: %s",
                    host, port, strerror(errno));
        }

        close(fd);
        fd = -1;
    }

    if (res)
        freeaddrinfo(res);

    if (fd < 0) {
        log_msg("ERROR", "TCP: no se pudo conectar a %s:%d", host, port);
        return -1;
    }

    /* Desactivar Nagle: paquetes pequeños (CONNECT/PUBLISH/PING) inmediatos. */
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    /* El socket ya quedó en modo no bloqueante (necesario para el bucle
     * principal, que usa poll() con timeouts). */

    return fd;
}

/* ------------------------------------------------------------------ */
/* Envía CONNECT MQTT 3.1.1                                            */
/* ------------------------------------------------------------------ */
static int mqtt_send_connect(int fd)
{
    unsigned char pkt[512];
    size_t pos = 0;
    size_t pl, rem;
    unsigned char rl[4];
    int rl_len;

    const char *cid  = g_cfg.client_id;
    const char *user = g_cfg.user;
    const char *pass = g_cfg.pass;
    size_t cid_len  = strlen(cid);
    size_t user_len = strlen(user);
    size_t pass_len = strlen(pass);

    /* Guard de tamaño: 10 bytes de variable header + 3 campos UTF-8
     * (2 bytes de longitud + contenido) deben caber en pkt. */
    if (10 + 2 + cid_len + 2 + user_len + 2 + pass_len > sizeof(pkt)) {
        log_msg("ERROR", "CONNECT: credenciales demasiado largas "
                "(se necesitan %zu bytes, buffer %zu)",
                10 + 2 + cid_len + 2 + user_len + 2 + pass_len, sizeof(pkt));
        return -1;
    }

    /* --- Variable header --- */
    /* Protocol name "MQTT" */
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x04;
    pkt[pos++] = 'M';
    pkt[pos++] = 'Q';
    pkt[pos++] = 'T';
    pkt[pos++] = 'T';
    /* Protocol level 3.1.1 */
    pkt[pos++] = 0x04;
    /* Connect flags: clean session + username + password */
    pkt[pos++] = 0x02 | 0x80 | 0x40;
    /* Keepalive (big-endian) */
    pkt[pos++] = (unsigned char)((MQTT_KEEPALIVE_S >> 8) & 0xFF);
    pkt[pos++] = (unsigned char)(MQTT_KEEPALIVE_S & 0xFF);

    /* --- Payload --- */
    pkt[pos++] = (unsigned char)((cid_len >> 8) & 0xFF);
    pkt[pos++] = (unsigned char)(cid_len & 0xFF);
    memcpy(pkt + pos, cid, cid_len);
    pos += cid_len;

    pkt[pos++] = (unsigned char)((user_len >> 8) & 0xFF);
    pkt[pos++] = (unsigned char)(user_len & 0xFF);
    memcpy(pkt + pos, user, user_len);
    pos += user_len;

    pkt[pos++] = (unsigned char)((pass_len >> 8) & 0xFF);
    pkt[pos++] = (unsigned char)(pass_len & 0xFF);
    memcpy(pkt + pos, pass, pass_len);
    pos += pass_len;

    pl  = pos;                  /* remaining length = variable header + payload */
    rem = pl;
    rl_len = mqtt_remaining_length(rem, rl);

    /* --- Ensamblar: fixed header + variable header + payload --- */
    unsigned char out[600];
    size_t opos = 0;

    if (1 + (size_t)rl_len + pl > sizeof(out)) {
        log_msg("ERROR", "CONNECT: paquete final demasiado grande "
                "(%zu bytes, buffer %zu)", 1 + (size_t)rl_len + pl,
                sizeof(out));
        return -1;
    }

    out[opos++] = 0x10;         /* CONNECT */
    memcpy(out + opos, rl, (size_t)rl_len);
    opos += (size_t)rl_len;
    memcpy(out + opos, pkt, pl);
    opos += pl;

    if (write_all(fd, out, opos) != 0) {
        log_msg("ERROR", "CONNECT: fallo al enviar paquete");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Espera y lee CONNACK. Devuelve 0 = aceptado, -1 = error.            */
/* ------------------------------------------------------------------ */
static int mqtt_read_connack(int fd)
{
    unsigned char buf[4];
    size_t got = 0;
    struct pollfd pfd = { fd, POLLIN, 0 };
    time_t deadline = time(NULL) + CONNECT_TIMEOUT_S;

    while (got < sizeof(buf)) {
        int remaining = (int)(deadline - time(NULL));
        if (remaining <= 0) {
            log_msg("ERROR", "CONNACK: timeout esperando respuesta");
            return -1;
        }

        int pr = poll(&pfd, 1, remaining * 1000);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            log_msg("ERROR", "CONNACK: poll falló: %s", strerror(errno));
            return -1;
        }
        if (pr == 0) {
            log_msg("ERROR", "CONNACK: timeout esperando respuesta");
            return -1;
        }

        ssize_t n = recv(fd, buf + got, sizeof(buf) - got, 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            log_msg("ERROR", "CONNACK: recv falló: %s", strerror(errno));
            return -1;
        }
        if (n == 0) {
            log_msg("ERROR", "CONNACK: conexión cerrada por el broker");
            return -1;
        }
        got += (size_t)n;
    }

    if (buf[0] != 0x20) {
        log_msg("ERROR", "CONNACK: tipo de paquete inesperado 0x%02X", buf[0]);
        return -1;
    }

    unsigned char rc = buf[3];
    if (rc != 0) {
        const char *msg;
        switch (rc) {
            case 1: msg = "versión de protocolo no aceptada";           break;
            case 2: msg = "client identifier rechazado";                break;
            case 3: msg = "servidor no disponible";                     break;
            case 4: msg = "usuario o password incorrectos";             break;
            case 5: msg = "no autorizado";                              break;
            default: msg = "código de retorno desconocido";             break;
        }
        log_msg("ERROR", "CONNACK rechazado (código %u): %s", rc, msg);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* PUBLISH QoS 0 (sin packet id, sin retain)                           */
/* ------------------------------------------------------------------ */
static int mqtt_publish(int fd, const char *topic, const char *payload, size_t plen)
{
    size_t tlen = strlen(topic);
    size_t rem = 2 + tlen + plen;       /* topic len(2) + topic + payload */
    unsigned char rl[4];
    int rl_len = mqtt_remaining_length(rem, rl);

    unsigned char pkt[600];
    size_t pos = 0;

    if (rem + 2 + 4 > sizeof(pkt)) {
        log_msg("ERROR", "PUBLISH: paquete demasiado grande");
        return -1;
    }

    pkt[pos++] = 0x30;                  /* PUBLISH, QoS 0, retain 0 */
    memcpy(pkt + pos, rl, (size_t)rl_len);
    pos += (size_t)rl_len;
    pkt[pos++] = (unsigned char)((tlen >> 8) & 0xFF);
    pkt[pos++] = (unsigned char)(tlen & 0xFF);
    memcpy(pkt + pos, topic, tlen);
    pos += tlen;
    memcpy(pkt + pos, payload, plen);
    pos += plen;

    if (write_all(fd, pkt, pos) != 0) {
        log_msg("ERROR", "PUBLISH: fallo al enviar: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* PINGREQ                                                             */
/* ------------------------------------------------------------------ */
static int mqtt_send_pingreq(int fd)
{
    unsigned char pkt[2] = { 0xC0, 0x00 };
    if (write_all(fd, pkt, sizeof(pkt)) != 0) {
        log_msg("ERROR", "PINGREQ: fallo al enviar");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Drena y procesa datos entrantes.                                   */
/* Devuelve 0 si OK, -1 si la conexión debe considerarse perdida.      */
/* ------------------------------------------------------------------ */
static int drain_incoming(int fd)
{
    unsigned char buf[RXBUF_SIZE];

    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            /* En este cliente solo esperamos PINGRESP/acks; se ignoran. */
            continue;
        }
        if (n == 0) {
            log_msg("ERROR", "socket: conexión cerrada por el broker");
            return -1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;           /* no hay más datos por ahora */
        if (errno == EINTR)
            continue;
        log_msg("ERROR", "socket: recv falló: %s", strerror(errno));
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Backoff exponencial: 5 s -> 60 s. Devuelve el delay usado.          */
/* ------------------------------------------------------------------ */
static int backoff_sleep(int *delay)
{
    int d = *delay;
    int slept = 0;

    log_msg("RECONECTANDO", "en %d s", d);

    while (slept < d && g_running) {
        sleep(1);
        slept++;
    }

    *delay *= 2;
    if (*delay > BACKOFF_MAX_S)
        *delay = BACKOFF_MAX_S;

    return d;
}

/* ================================================================== */
/*  TELEMETRÍA                                                        */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* Ejecuta un comando FIJO (sin interpolación de datos externos) vía   */
/* popen y copia su salida en `out` (terminada en NUL, truncada a n-1).*/
/* Devuelve 0 si el comando terminó con éxito, -1 en caso contrario.   */
/*                                                                     */
/* NOTA: popen() no permite un timeout duro; los comandos usados son   */
/* locales y rápidos (gsmctl/iwinfo/ls). Ver PLAN.md §5 y §12.         */
/* ------------------------------------------------------------------ */
static int read_cmd_line(const char *cmd, char *out, size_t n)
{
    if (n == 0)
        return -1;
    out[0] = '\0';

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        log_msg("ERROR", "popen('%s') falló: %s", cmd, strerror(errno));
        return -1;
    }

    size_t used = 0;
    size_t got;
    while (used + 1 < n &&
           (got = fread(out + used, 1, n - 1 - used, fp)) > 0)
        used += got;
    out[used] = '\0';

    int rc = pclose(fp);
    if (rc == -1) {
        log_msg("ERROR", "pclose('%s') falló: %s", cmd, strerror(errno));
        return -1;
    }
    if (WIFEXITED(rc) && WEXITSTATUS(rc) == 0)
        return 0;

    return -1;
}

/* ------------------------------------------------------------------ */
/* Parseo de RSSI: busca la línea "RSSI: <int>"; si no existe, toma el */
/* primer entero con signo de la salida. "N/A" o vacío -> error.       */
/* ------------------------------------------------------------------ */
static int parse_rssi(const char *out, int *rssi)
{
    const char *p = strstr(out, "RSSI:");
    if (p)
        p += 5;
    else
        p = out;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    if (*p == 'N' || *p == 'n' || *p == '\0')
        return -1;              /* N/A o sin datos */

    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p)
        return -1;

    *rssi = (int)v;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Parseo de temperatura del módem: gsmctl -c devuelve décimas de °C   */
/* (p. ej. "250" = 25.0 °C).                                           */
/* ------------------------------------------------------------------ */
static int parse_temp_tenths(const char *out, double *temp)
{
    const char *p = out;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    if (*p == '\0' || *p == 'N' || *p == 'n')
        return -1;

    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p)
        return -1;

    *temp = (double)v / 10.0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* CPU: lectura directa de /proc/stat (sin popen).                    */
/*                                                                     */
/* Contadores de 64 bits (unsigned long long + strtoull): en MIPS32    */
/* unsigned long es de 32 bits y strtoul SATURA a ULONG_MAX en         */
/* overflow (no hace wrap modular); con HZ=100 /proc/stat supera 2^32  */
/* tras ~497 días de uptime y el delta quedaría 0 (cpu% fijo en 0.0).  */
/* El costo del helper de 64 bits en MIPS32 es mínimo (I-002).         */
/* ------------------------------------------------------------------ */
struct cpu_sample {
    unsigned long long total;
    unsigned long long idle;
};

static int cpu_read(struct cpu_sample *s)
{
    FILE *fp = fopen(PATH_PROC_STAT, "r");
    if (!fp)
        return -1;

    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    if (strncmp(line, "cpu ", 4) != 0)
        return -1;

    unsigned long long f[10] = { 0 };
    int n = 0;
    const char *q = line + 4;
    while (n < 10) {
        while (*q == ' ' || *q == '\t')
            q++;
        if (*q < '0' || *q > '9')
            break;
        char *end = NULL;
        f[n++] = strtoull(q, &end, 10);
        if (end == q)
            break;
        q = end;
    }
    if (n < 4)
        return -1;

    /* total = user+nice+system+idle+iowait+irq+softirq+steal
     * (guest/guest_nice ya están contabilizados en user/nice). */
    s->total = 0;
    for (int i = 0; i < 8 && i < n; i++)
        s->total += f[i];
    s->idle = f[3] + (n > 4 ? f[4] : 0ULL);
    return 0;
}

/* %CPU entre dos muestras. En la primera llamada (sin referencia previa)
 * toma una muestra, espera 200 ms y toma otra para producir un valor
 * válido de inmediato. */
static int cpu_percent(double *pct)
{
    static struct cpu_sample prev;
    static int have_prev = 0;
    struct cpu_sample cur;

    if (cpu_read(&cur) != 0)
        return -1;

    if (!have_prev) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 200L * 1000L * 1000L;      /* 200 ms */
        nanosleep(&ts, NULL);

        prev = cur;
        if (cpu_read(&cur) != 0)
            return -1;
        have_prev = 1;
    }

    unsigned long long dt = cur.total - prev.total;
    unsigned long long di = cur.idle  - prev.idle;
    prev = cur;

    if (dt == 0) {
        *pct = 0.0;
        return 0;
    }

    double p = 100.0 * (1.0 - (double)di / (double)dt);
    if (p < 0.0)
        p = 0.0;
    if (p > 100.0)
        p = 100.0;
    *pct = p;
    return 0;
}

/* ------------------------------------------------------------------ */
/* WiFi: cuenta líneas con MAC en la salida de `iwinfo <iface> assoclist`.
 * Cada estación asociada produce una línea que comienza con la MAC
 * (xx:xx:xx:xx:xx:xx); las líneas de detalle (RX/TX/throughput) van
 * indentadas y NO se cuentan.                                       */
/* ------------------------------------------------------------------ */
static int count_mac_lines(const char *buf)
{
    int count = 0;
    const char *p = buf;

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);

        if (len >= 17 && p[0] != ' ' && p[0] != '\t') {
            int colons = 0;
            for (size_t i = 0; i < 17; i++) {
                if (p[i] == ':')
                    colons++;
            }
            if (colons == 5)
                count++;
        }

        if (!eol)
            break;
        p = eol + 1;
    }

    return count;
}

static int iface_is_wireless(const char *name)
{
    return strncmp(name, "wlan", 4) == 0 ||
           strncmp(name, "ra",   2) == 0 ||
           strncmp(name, "ath",  3) == 0;
}

/* Devuelve 0 y *users = nº de clientes asociados al AP si se pudo
 * determinar; *users = 0 si no hay ninguna interfaz AP (router en modo
 * STA); -1 si no se pudo determinar (iwinfo ausente). */
static int wifi_users(int *users)
{
    char list[512];

    if (read_cmd_line(CMD_LS_NET, list, sizeof(list)) != 0) {
        *users = -1;
        return -1;
    }

    int total = 0;
    int found_master = 0;
    char *save = NULL;

    for (char *tok = strtok_r(list, " \t\r\n", &save);
         tok != NULL;
         tok = strtok_r(NULL, " \t\r\n", &save)) {

        if (!iface_is_wireless(tok))
            continue;

        char cmd[160];
        char info[512];

        snprintf(cmd, sizeof(cmd), "/usr/bin/iwinfo %s info", tok);
        if (read_cmd_line(cmd, info, sizeof(info)) != 0)
            continue;

        if (strstr(info, "Mode: Master") || strstr(info, "Mode: AP")) {
            found_master = 1;

            /* 8 KiB: cada estación genera ~4 líneas (~250 bytes); con 1024
             * bytes solo cabían ~4 clientes y se subcontaba con >4 (I-001).
             * 8 KiB soporta el máximo del plan (50 clientes). */
            char al[8192];
            snprintf(cmd, sizeof(cmd), "/usr/bin/iwinfo %s assoclist", tok);
            if (read_cmd_line(cmd, al, sizeof(al)) == 0)
                total += count_mac_lines(al);
        }
    }

    if (found_master) {
        *users = total;
        return 0;
    }

    /* Sin interfaz AP (p. ej. RUT200 en modo STA): cero clientes asociados.
     * Se informa 0 (dato determinado), no null. Ver nota de plataforma. */
    *users = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Recolección de métricas                                             */
/* ------------------------------------------------------------------ */
struct metrics {
    int    signal;       int signal_valid;
    int    users;        int users_valid;
    double cpu;          int cpu_valid;
    double temp;         int temp_valid;
    long   ts;
};

static void collect_metrics(struct metrics *m)
{
    char buf[256];

    memset(m, 0, sizeof(*m));
    m->ts = (long)time(NULL);

    /* --- Señal celular (RSSI, dBm) --- */
    if (read_cmd_line(CMD_GSM_SIGNAL, buf, sizeof(buf)) == 0) {
        if (parse_rssi(buf, &m->signal) == 0)
            m->signal_valid = 1;
    }

    /* --- Clientes WiFi (asociados al AP) --- */
    {
        int u = -1;
        if (wifi_users(&u) == 0 && u >= 0) {
            m->users = u;
            m->users_valid = 1;
        }
    }

    /* --- CPU (%) --- */
    if (cpu_percent(&m->cpu) == 0)
        m->cpu_valid = 1;

    /* --- Temperatura del módem (°C) --- */
    if (read_cmd_line(CMD_GSM_TEMP, buf, sizeof(buf)) == 0) {
        if (parse_temp_tenths(buf, &m->temp) == 0)
            m->temp_valid = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Ensamblado del JSON de estado (una línea, sin espacios)             */
/*   {"signal":-47,"users":0,"cpu":12.3,"temp":25.0,"ts":1758000000}  */
/* ------------------------------------------------------------------ */
static void build_status_json(const struct metrics *m, char *out, size_t n)
{
    char sig[16], usr[16], cpu[24], tmp[24];

    if (m->signal_valid)
        snprintf(sig, sizeof(sig), "%d", m->signal);
    else
        snprintf(sig, sizeof(sig), "null");

    if (m->users_valid)
        snprintf(usr, sizeof(usr), "%d", m->users);
    else
        snprintf(usr, sizeof(usr), "null");

    if (m->cpu_valid)
        snprintf(cpu, sizeof(cpu), "%.1f", m->cpu);
    else
        snprintf(cpu, sizeof(cpu), "null");

    if (m->temp_valid)
        snprintf(tmp, sizeof(tmp), "%.1f", m->temp);
    else
        snprintf(tmp, sizeof(tmp), "null");

    snprintf(out, n,
             "{\"signal\":%s,\"users\":%s,\"cpu\":%s,\"temp\":%s,\"ts\":%ld}",
             sig, usr, cpu, tmp, m->ts);
}

/* ------------------------------------------------------------------ */
/* Publica ambos reportes. Devuelve 0 si OK, -1 si hay que reconectar. */
/* ------------------------------------------------------------------ */
static int publish_reports(int fd)
{
    struct metrics m;
    char json[320];

    /* 1) report/online -> "true" (compatibilidad V1 / control web) */
    if (mqtt_publish(fd, g_topic_online, REPORT_PAYLOAD, REPORT_PAYLOAD_LEN) != 0)
        return -1;
    log_msg("PUBLICANDO", "%s = \"%s\"", g_topic_online, REPORT_PAYLOAD);

    /* 2) report/status -> JSON de telemetría */
    collect_metrics(&m);
    build_status_json(&m, json, sizeof(json));

    if (mqtt_publish(fd, g_topic_status, json, strlen(json)) != 0)
        return -1;
    log_msg("PUBLICANDO", "%s = %s", g_topic_status, json);

    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    int backoff = BACKOFF_INITIAL_S;

    init_signals();

    /* V2.1: cargar credenciales/parámetros desde el archivo de
     * configuración ANTES de construir tópicos y de abrir la conexión.
     * Si el archivo no existe, config_load() loguea WARNING y g_cfg
     * conserva los defaults compilados (user/pass vacíos). */
    config_load(&g_cfg);

    if (g_cfg.user[0] == '\0' || g_cfg.pass[0] == '\0')
        log_msg("ERROR", "credenciales MQTT no configuradas "
                "(falta %s)", CONFIG_PATH);

    snprintf(g_topic_online, sizeof(g_topic_online), "%s/%s",
             g_cfg.client_id, TOPIC_ONLINE_SUFFIX);
    snprintf(g_topic_status, sizeof(g_topic_status), "%s/%s",
             g_cfg.client_id, TOPIC_STATUS_SUFFIX);

    log_msg("INICIO", "lp104mqttv2 — broker %s:%d, intervalos %ds",
            g_cfg.broker, g_cfg.port, REPORT_INTERVAL_S);
    log_msg("INICIO", "usuario MQTT: %s", g_cfg.user[0] ? g_cfg.user : "(vacío)");
    log_msg("INICIO", "topic online: %s", g_topic_online);
    log_msg("INICIO", "topic status: %s", g_topic_status);

    while (g_running) {
        int fd = tcp_connect(g_cfg.broker, g_cfg.port);
        if (fd < 0) {
            backoff_sleep(&backoff);
            continue;
        }

        if (mqtt_send_connect(fd) != 0 || mqtt_read_connack(fd) != 0) {
            close(fd);
            backoff_sleep(&backoff);
            continue;
        }

        log_msg("CONECTADO", "sesión MQTT establecida con %s:%d",
                g_cfg.broker, g_cfg.port);

        /* Publicación inmediata al conectar. */
        if (publish_reports(fd) != 0) {
            close(fd);
            backoff_sleep(&backoff);
            continue;
        }
        backoff = BACKOFF_INITIAL_S;

        time_t last_tx = time(NULL);
        time_t next_publish = last_tx + REPORT_INTERVAL_S;

        /* --- Bucle de sesión --- */
        while (g_running) {
            time_t now = time(NULL);
            time_t next_ping = last_tx + PING_THRESHOLD_S;
            time_t next_event = next_publish < next_ping ? next_publish : next_ping;
            int timeout_ms = (int)((next_event - now) * 1000);
            if (timeout_ms < 0)
                timeout_ms = 0;

            struct pollfd pfd = { fd, POLLIN, 0 };
            int pr = poll(&pfd, 1, timeout_ms);
            if (pr < 0) {
                if (errno == EINTR)
                    continue;
                log_msg("ERROR", "poll falló: %s", strerror(errno));
                break;
            }

            if (pr > 0) {
                /* Hay datos entrantes: procesarlos (PINGRESP, cierre, etc.). */
                if (drain_incoming(fd) != 0)
                    break;
            }

            now = time(NULL);

            if (now >= next_publish) {
                if (publish_reports(fd) != 0)
                    break;
                last_tx = now;
                next_publish = now + REPORT_INTERVAL_S;
                backoff = BACKOFF_INITIAL_S;
                continue;
            }

            if (now - last_tx >= PING_THRESHOLD_S) {
                if (mqtt_send_pingreq(fd) != 0)
                    break;
                log_msg("PING", "PINGREQ enviado (keepalive)");
                last_tx = now;
            }
        }

        close(fd);
        if (!g_running)
            break;

        log_msg("ERROR", "sesión MQTT perdida");
        backoff_sleep(&backoff);
    }

    log_msg("FIN", "lp104mqttv2 detenido limpiamente");
    return 0;
}
