/*
 * lp104mqtt.c — Cliente MQTT 3.1.1 mínimo (solo PUBLISH) para Teltonika RUT200
 *
 * Replica el reporte del firmware ESP8266 `lp104_secure.txt`:
 *   publica "true" en {MQTT_CLIENT}/report/online cada 30 s.
 *
 * - MQTT 3.1.1 implementado a mano sobre socket TCP (sin librerías externas).
 * - QoS 0, sin retain, sin subscribe.
 * - Keepalive 60 s con PINGREQ si no hay tráfico.
 * - Reconexión con backoff exponencial 5 s -> 60 s.
 * - Enlace estático (musl) para el router.
 *
 * Cross-compile: ver Makefile (SDK OpenWrt 21.02.0 ramips/mt76x8, mipsel_24kc).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
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

#define REPORT_TOPIC_SUFFIX "report/online"
#define REPORT_PAYLOAD      "true"
#define REPORT_PAYLOAD_LEN  4       /* strlen("true"), sin '\0'          */

#define RXBUF_SIZE          512
#define WRITE_TIMEOUT_S     5       /* poll(POLLOUT) breve al escribir     */

/* ------------------------------------------------------------------ */
/* Estado global                                                       */
/* ------------------------------------------------------------------ */
static volatile sig_atomic_t g_running = 1;
static char g_topic[256];           /* {MQTT_CLIENT}/report/online */

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

        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0)
            break;              /* éxito inmediato */

        if (rc < 0 && errno == EINPROGRESS) {
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
        } else if (rc < 0) {
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
    size_t vh, pl, rem;
    unsigned char rl[4];
    int rl_len;

    const char *cid  = MQTT_CLIENT;
    const char *user = MQTT_USERNAME;
    const char *pass = MQTT_PASSWORD;
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
    vh = pos;   /* por ahora pos == 10 (longitud variable header) */

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

    (void)vh;
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

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    int backoff = BACKOFF_INITIAL_S;

    init_signals();

    snprintf(g_topic, sizeof(g_topic), "%s/%s", MQTT_CLIENT, REPORT_TOPIC_SUFFIX);

    log_msg("INICIO", "lp104mqtt — broker %s:%d, topic %s, intervalo %ds",
            MQTT_BROKER, MQTT_PORT, g_topic, REPORT_INTERVAL_S);

    while (g_running) {
        int fd = tcp_connect(MQTT_BROKER, MQTT_PORT);
        if (fd < 0) {
            backoff_sleep(&backoff);
            continue;
        }

        if (mqtt_send_connect(fd) != 0 || mqtt_read_connack(fd) != 0) {
            close(fd);
            backoff_sleep(&backoff);
            continue;
        }

        log_msg("CONECTADO", "sesión MQTT establecida con %s:%d", MQTT_BROKER, MQTT_PORT);

        /* Publicación inmediata al conectar. */
        if (mqtt_publish(fd, g_topic, REPORT_PAYLOAD, REPORT_PAYLOAD_LEN) == 0) {
            log_msg("PUBLICANDO", "%s = \"%s\"", g_topic, REPORT_PAYLOAD);
            backoff = BACKOFF_INITIAL_S;
        } else {
            close(fd);
            backoff_sleep(&backoff);
            continue;
        }

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
                if (mqtt_publish(fd, g_topic, REPORT_PAYLOAD, REPORT_PAYLOAD_LEN) != 0)
                    break;
                log_msg("PUBLICANDO", "%s = \"%s\"", g_topic, REPORT_PAYLOAD);
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

    log_msg("FIN", "lp104mqtt detenido limpiamente");
    return 0;
}
