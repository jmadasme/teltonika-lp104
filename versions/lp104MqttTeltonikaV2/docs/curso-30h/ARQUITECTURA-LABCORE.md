# ARQUITECTURA DE `labcore` — LA LIBRERÍA NÚCLEO

## La infraestructura compartida por las 15 aplicaciones

---

## 1. ¿POR QUÉ UNA LIBRERÍA NÚCLEO?

Cada una de las 15 aplicaciones necesita resolver **exactamente los mismos 8 problemas**:

| Problema | Apps que lo necesitan | Solución compartida |
|---|---|---|
| Leer configuración de un archivo | 15/15 | `labcore_config` |
| Enviar alertas por MQTT | 15/15 | `labcore_mqtt` |
| Construir JSON sin librerías | 15/15 | `labcore_json` |
| Loguear con timestamp y niveles | 15/15 | `labcore_log` |
| Evaluar umbrales sin generar ruido | 12/15 | `labcore_threshold` |
| Recordar el estado entre reinicios | 8/15 | `labcore_state` |
| Detectar tráfico de red | 5/15 | `labcore_netmon` |
| Ejecutar comandos externos de forma segura | 8/15 | `labcore_cmd` |

Si cada app reimplementara esto, tendríamos 15 copies del cliente MQTT y 15 copies de los bugs
del cliente MQTT. **La librería es la decisión de arquitectura que hace el curso escalable.**

---

## 2. MÓDULOS

```
labcore/
├── labcore.h              # API pública (el único include que las apps usan)
├── labcore_internal.h     # tipos compartidos internos
├── labcore_config.c       # parser clave=valor
├── labcore_log.c          # logging estructurado
├── labcore_mqtt.c         # cliente MQTT 3.1.1
├── labcore_json.c         # constructor de JSON
├── labcore_threshold.c    # motor de umbrales con histéresis
├── labcore_state.c        # persistencia de estado
├── labcore_netmon.c       # lector de /proc/net/nf_conntrack
└── labcore_cmd.c          # popen seguro con guard de buffer
```

---

## 3. `labcore_config` — Parser de configuración

### 3.1 Formato

```ini
# /etc/labNN.conf
# Formato clave=valor. Comentarios con #. Claves case-sensitive.

# --- conexión MQTT (compartida por las 15 apps) ---
broker     = www.smartage.cl
port       = 1886
user       = YOUR_MQTT_USER
pass       = YOUR_MQTT_PASS
client_id  = rut200-lab-01

# --- específicos de la app ---
interval   = 5
threshold  = 80
```

### 3.2 Por qué NO usamos UCI

Teltonika usa **UCI** (`uci set`, `uci get`) para su propia configuración. Este curso usa un
archivo `clave=valor` propio. Comparación:

| | UCI (`/etc/config/*`) | `clave=valor` (nuestro) |
|---|---|---|
| Herramienta nativa | `uci set/get/commit` | `vi` |
| Validación de tipos | parcial | manual (en `labcore_config`) |
| Secciones y listas | ✓ nativo | ✗ |
| Legible por humanos | ✓ | ✓✓ |
| Parseable desde C sin shell | ✗ (necesita invocar `uci`) | ✓ `fopen` directo |
| Interoperable con el WebUI de RutOS | ✓ | ✗ |
| Ideal para... | configuración del sistema | **config de una app** |

**Decisión:** configuración del sistema → UCI (para que el WebUI la vea).
Configuración de la app → `clave=valor` (parseable en C sin invocar procesos).
**Es una decisión documentada, no un descuido.**

### 3.3 API

```c
typedef struct {
    /* conexión MQTT — comunes */
    char broker[64];
    int  port;
    char client_id[64];
    char user[64];
    char pass[128];

    /* específicos — el llamador los rellena */
    int   interval;
    double threshold;
    int   extra_int;
    char   extra_str[128];
} app_config_t;

int  lc_config_load(const char *path, app_config_t *cfg);
int  lc_config_get_int(app_config_t *cfg, const char *key, long dflt);
double lc_config_get_double(app_config_t *cfg, const char *key, double dflt);
const char *lc_config_get_str(app_config_t *cfg, const char *key, const char *dflt);
```

### 3.4 Reglas de seguridad (inviolables)

1. **NUNCA** loguear `pass`. Solo `user`.
2. El `.conf` en el router lleva `chmod 600`.
3. El `.conf` real **nunca** se versiona. Solo el `.conf.example` con placeholders.
4. Si falta el archivo → **WARNING visible** + defaults (el servicio arranca pero falla de
   forma visible, nunca silenciosa).
5. Si `user`/`pass` quedan vacíos → **ERROR visible** en el log.

---

## 4. `labcore_mqtt` — Cliente MQTT 3.1.1 a mano

### 4.1 Por qué sin librerías

| Opción | Problema |
|---|---|
| `libmosquitto` | requiere SDK completo de OpenWrt con paquetes; no está en el SDK oficial |
| `paho.mqtt.c` | igual: dependencia externa |
| **C puro, syscalls POSIX** | ✓ funciona con el SDK pelado, ✓ binario mínimo, ✓ sin versionado de ABI |

**El proyecto semilla ya lo probó y auditó. El curso parte de ahí.**

### 4.2 Paquetes implementados

| Paquete | Byte fijo | Para qué |
|---|---|---|
| `CONNECT` | `0x10` | conexión con credenciales, clean session, keepalive |
| `CONNACK` | `0x20` | confirmación del broker (6 códigos de retorno) |
| `PUBLISH` | `0x30` | enviar telemetría (QoS 0) |
| `SUBSCRIBE` | `0x82` | recibir comandos remotos (nuevo en el curso) |
| `SUBACK` | `0x90` | confirmación de suscripción |
| `PINGREQ` | `0xC0` | keepalive |
| `PINGRESP` | `0xD0` | confirmación de keepalive |

### 4.3 La codificación "Remaining Length"

Todo paquete MQTT tiene un campo de longitud codificado como **entero de longitud variable**
(base 128 con bit de continuación). Ejemplo: 200 bytes →

```
200 = 0b11001000
  → byte 1: 0xC8 | 0x80 = 0xC8  (200 | "hay más bytes")
  → byte 2: 200 >> 7 = 1  → 0x01
  → resultado: 0xC8 0x01
```

```c
static int mqtt_remaining_length(size_t n, unsigned char *out)
{
    int i = 0;
    do {
        unsigned char b = (unsigned char)(n % 128);
        n /= 128;
        if (n > 0) b |= 0x80;      /* bit de continuación */
        out[i++] = b;
    } while (n > 0 && i < 4);
    return i;
}
```

### 4.4 Máquina de estados de la sesión

```
              ┌──────────────┐
              │  RESOLVE DNS │  getaddrinfo()
              └──────┬───────┘
                     v
              ┌──────────────┐
              │ TCP CONNECT  │  connect() no bloqueante + poll(POLLOUT)
              └──────┬───────┘  timeout 10 s
                     v
              ┌──────────────┐
              │   CONNECT    │  mqtt_send_connect()
              └──────┬───────┘
                     v
              ┌──────────────┐
              │   CONNACK    │  rc == 0 ? sí → sesión
              └──────┬───────┘  rc != 0 → backoff
                     │ rc==0
                     v
    ┌───────────────────────────────────────────┐
    │            SESIÓN ACTIVA                 │
    │  ┌─────────┐  ┌─────────┐  ┌─────────┐  │
    │  │ PUBLISH │  │ PINGREQ │  │ RECIBIR │  │
    │  │ datos   │  │ keepalive│ │ (drain) │  │
    │  └────┬────┘  └────┬────┘  └────┬────┘  │
    │       └─────────────┴────────────┘       │
    │       poll() con timeout hasta el próximo evento
    └───────────────────┬───────────────────────┘
                        │ error de escritura / cierre
                        v
              ┌──────────────┐
              │   BACKOFF    │  5 s → 10 s → 20 s → 40 s → 60 s (máx)
              └──────┬───────┘  (duerme 1 s por paso,，以便 atender SIGTERM)
                     └──────────► vuelve a RESOLVE DNS
```

### 4.5 Decisiones de robustez

| Problema | Solución |
|---|---|
| `connect()` puede colgarse para siempre (no respeta `SO_SNDTIMEO`) | modo no bloqueante + `poll(POLLOUT)` con deadline |
| `send()` devuelve `EAGAIN` si el buffer está lleno | `write_all()` reintenta con `poll(POLLOUT)` y timeout de 5 s |
| `SIGPIPE` mata el proceso al escribir en un socket cerrado | `signal(SIGPIPE, SIG_IGN)` |
| `sleep(60)` ignora SIGTERM | `backoff_sleep()` duerme de a 1 s y chequea `g_running` |
| Paquetes MQTT pequeños acumulan latencia (Nagle) | `TCP_NODELAY` |
| Paquetes de `popen` pueden ser enormes | `read_cmd_line()` con límite de buffer |

---

## 5. `labcore_json` — Construcción de JSON segura

### 5.1 La regla de oro

```c
/* MAL — desbordamiento esperando que pase algo */
char buf[32];
strcpy(buf, valor_largo);

/* BIEN — siempre con límite */
snprintf(buf, sizeof(buf), "%s", valor_largo);
```

**En `labcore` no existe una sola vez `strcpy`, `strcat` ni `sprintf` sobre un buffer de
tamaño fijo.** Es una invariante de la librería, verificada por el agente auditor.

### 5.2 Escapado

```c
static const char *json_escape(const char *in, char *out, size_t n)
{
    size_t used = 0;
    for (size_t i = 0; in[i] && used + 7 < n; i++) {
        unsigned char c = (unsigned char)in[i];
        switch (c) {
        case '"':  out[used++]='\\'; out[used++]='"';  break;
        case '\\': out[used++]='\\'; out[used++]='\\'; break;
        case '\n': out[used++]='\\'; out[used++]='n';  break;
        case '\r': out[used++]='\\'; out[used++]='r';  break;
        case '\t': out[used++]='\\'; out[used++]='t';  break;
        default:
            if (c < 0x20) { /* \u00XX */ }
            else out[used++] = (char)c;
        }
    }
    out[used] = '\0';
    return out;
}
```

**Por qué importa:** un SSID de WiFi con comillas (`"Mi Red"`) rompería el JSON y,
peor, un SSID con `\n` inyectaría campos falsos en la alerta.

### 5.3 `null` vs `0` — la diferencia semántica

```json
{"signal": -72, "users": null, "cpu": 12.3, "temp": null}
```

- `users: 0` → "sé que hay 0 clientes" (dato determinado)
- `users: null` → "no pude determinarlo" (dato desconocido)

**Confundirlos es un bug de calidad**: un dashboard que muestre "0 clientes" cuando en realidad
el sensor falló está mintiendo. `labcore` usa una struct con flags de validez:

```c
struct metrics {
    int    signal;       int signal_valid;    /* <— el flag */
    int    users;        int users_valid;
    double cpu;          int cpu_valid;
    double temp;         int temp_valid;
    long   ts;
};
```

---

## 6. `labcore_threshold` — El motor de alertas

> **Este es el módulo que más valor aporta y el que más se subestima.**

### 6.1 El problema sin histéresis

Supongamos `threshold = 80% CPU`. Si la CPU oscila entre 79.5 y 80.5 alrededor del umbral:

```
CPU=79.8  → normal        (sin alerta)
CPU=80.2  → ¡ALERTA! ────────┐
CPU=79.9  → normal        ───┘  5 s después
CPU=80.1  → ¡ALERTA! ────────┐
CPU=79.7  → normal        ───┘
           ... 60 alertas por minuto, sin que pase nada
```

Resultado: el usuario **desactiva la alerta** (y con ella, todas las demás). La app es un
clásico falso positivo industrial.

### 6.2 La solución: umbral de entrada + umbral de salida

```
                ENTRADA (warn_high = 80%)
    ────────────────┐                    ┌────────────
   NORMAL          │ ╔═══════════════╗   │
                    ║    ALERTA      ║   │
                    ╚═══════════════╝   │
                                    SALIDA (warn_low = 70%)
   ◄───────────────────────────────────┘
```

- Para **entrar** en alerta: hay que cruzar **80%**
- Para **salir** de alerta: hay que bajar a **70%**

La "zona muerta" entre 70 y 80 es el margen que absorbe la oscilación normal.

### 6.3 Debounce temporal

Incluso con histéresis, un valor que cruza el umbral de entrada y sale inmediatamente puede
generar eventos repetidos. `labcore` añade un **debounce**:

```c
typedef struct {
    double warn_high;
    double warn_low;
    int    debounce_s;       /* segs que el valor debe permanecer
                                fuera de la zona muerta para confirmar */

    int    state;            /* LC_STATE_NORMAL / LC_STATE_ALERTING / LC_STATE_ALERT */
    int    candidate_state;
    time_t candidate_since;
    time_t last_publish;
} lc_threshold_t;
```

Máquina de estados:

```
                    (valor >= warn_high)
        ┌─────────┐   durante debounce_s   ┌──────────┐
        │ NORMAL  │ ─────────────────────► │ ALERTING │
        │         │ ◄───────────────────── │          │
        └─────────┘   durante debounce_s   └────┬─────┘
              ▲                                  │ publica
              │ (valor <= warn_low)             ▼
              │                             ┌──────────┐
              └──────────────────────────── │  ALERT   │
                                            └──────────┘
```

### 6.4 API

```c
void lc_threshold_init(lc_threshold_t *t, double high, double low, int debounce_s);

typedef enum {
    LC_THRESHOLD_NORMAL,     /* nada que hacer */
    LC_THRESHOLD_ENTERING,   /* valor cruzó el umbral, esperando debounce */
    LC_THRESHOLD_ALERT,      /* ¡ALERTA! publicar */
    LC_THRESHOLD_CLEARED     /* se recuperó, publicar "recovered" */
} lc_threshold_result_t;

lc_threshold_result_t lc_threshold_eval(lc_threshold_t *t, double value);
```

---

## 7. `labcore_state` — Persistencia de estado

### 7.1 El problema del "dispositivo nuevo"

`WiFi New User Alert` necesita saber qué MACs ya vio. `Disk Usage Alert` necesita su línea
base. `System File Monitor` necesita sus hashes.

Todo eso debe sobrevivir **al reinicio del router**.

### 7.2 ¿Dónde se guarda?

| Ruta | Tipo | ¿Persiste? | Uso |
|---|---|---|---|
| `/tmp` | tmpfs (RAM) | **NO** | ❌ nunca para estado |
| `/etc/<app>` | overlay rw | sí | config (leído por humanos) |
| `/usr/local/lib/<app>/` | overlay rw | sí | ✓ **estado de la app** |
| `/overlay` | raíz del overlay | sí |Backups del sistema |

**Regla dura:** el estado de una app **nunca** va en `/tmp`. Si va, tras un reinicio todas
las MACs son "nuevas" y el usuario recibe una avalancha de falsas alarmas. Es un bug de
producción real que el curso previene.

### 7.3 API

```c
int  lc_state_open(lc_state_t *st, const char *path);
int  lc_state_save(lc_state_t *st);
void lc_state_close(lc_state_t *st);

/* helpers */
int  lc_state_set_str(lc_state_t *st, const char *k, const char *v);
int  lc_state_get_str(lc_state_t *st, const char *k, char *out, size_t n);
int  lc_state_add_unique(lc_state_t *st, const char *k, const char *value);
```

### 7.4 Invalidación

Si el archivo de estado está corrupto (corte de energía durante la escritura), la app debe
**detectar** la corrupción:

```c
/* Cabecera con magic + versión + checksum */
struct state_header {
    uint32_t magic;     /* 0x4C42434Cu */
    uint32_t version;
    uint32_t entries;
    uint32_t checksum;
};
```

Si el magic no coincide o el checksum falla → **FAIL-SAFE**: regenerar estado vacío, alertar
"estado reinicializado", seguir funcionando.

---

## 8. `labcore_netmon` — Lector de conntrack

### 8.1 El formato

`/proc/net/nf_conntrack` — una línea por conexión:

```
ipv4     2 tcp      6 431999 ESTABLISHED src=192.168.1.50 dst=192.168.1.1 sport=54321 dport=80 src=192.168.1.1 dst=192.168.1.50 sport=80 dport=54321 [ASSURED] mark=0 use=1
```

No es JSON, no son campos fijos. Es posicional, y el kernel lo cambia entre versiones.

### 8.2 Estrategia de parseo robusto

**Nunca** parsear por posición. **Siempre** buscar la clave:

```c
/* Buscar "src=" y extraer lo que sigue hasta el espacio */
static int nct_get(const char *line, const char *key, char *out, size_t n)
{
    char pat[16];
    snprintf(pat, sizeof(pat), " %s=", key);   /* espacio antes → no matchea "xsrc=" */
    const char *p = strstr(line, pat);
    if (!p) return -1;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != ' ' && i < n - 1) out[i++] = *p++;
    out[i] = '\0';
    return 0;
}
```

> **Por qué el espacio inicial:** `strstr(line, "src=")` también matchearía `dst=` mal parseado
> o cualquier clave que terminen en `src=`. El espacio lo ancla.

### 8.3 API

```c
typedef struct {
    char  src_ip[16];
    char  dst_ip[16];
    int   proto;          /* IPPROTO_TCP / IPPROTO_ICMP */
    int   src_port;
    int   dst_port;
    char  state[16];      /* SYN_SENT, ESTABLISHED, TIME_WAIT... */
} lc_conn_t;

typedef int (*lc_conn_cb)(const lc_conn_t *c, void *userdata);

/* Itera sobre todas las conexiones del kernel. */
int lc_netmon_foreach(lc_conn_cb cb, void *userdata);
```

---

## 9. `labcore_cmd` — Ejecución segura de comandos

### 9.1 La decisión: `popen` sí, `system` no

| Función | Veredicto | Razón |
|---|---|---|
| `system()` | ❌ NUNCA | ejecuta `/bin/sh -c` e interpreta todo: `;`, `|`, `$()`, backticks |
| `popen()` | ⚠ CONDICIONAL | también pasa por `sh -c`, pero **solo** si la cadena es constante |

**Regla de `labcore`:**

```c
/* ✅ PERMITIDO: comando constante, sin datos externos */
#define CMD_GSM_SIGNAL "/usr/sbin/gsmctl -q"
if (read_cmd_line(CMD_GSM_SIGNAL, buf, sizeof(buf)) != 0) { /* ... */ }

/* ❌ PROHIBIDO: datos externos o de red en la línea de comando */
snprintf(cmd, sizeof(cmd), "iwinfo %s assoclist", iface_de_red);   /* ← INYECCIÓN */
```

El nombre de la interfaz **se valida contra una whitelist** antes de interpolarlo, o mejor,
se descubre dinámicamente y solo se acepta si cumple el patrón esperado (`wlan*`, `ra*`,
`ath*`, `eth*`).

### 9.2 Guard de buffer

```c
static int read_cmd_line(const char *cmd, char *out, size_t n)
{
    if (n == 0) return -1;
    out[0] = '\0';

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        log_msg("ERROR", "popen('%s') falló: %s", cmd, strerror(errno));
        return -1;
    }

    size_t used = 0, got;
    /* NUNCA escribir más de n-1 bytes (dejar el '\0') */
    while (used + 1 < n && (got = fread(out + used, 1, n - 1 - used, fp)) > 0)
        used += got;
    out[used] = '\0';        /* terminación GARANTIZADA */

    int rc = pclose(fp);
    if (rc == -1) {
        log_msg("ERROR", "pclose('%s') falló: %s", cmd, strerror(errno));
        return -1;
    }
    return (WIFEXITED(rc) && WEXITSTATUS(rc) == 0) ? 0 : -1;
}
```

**El `used + 1 < n` en la condición es lo que evita el desbordamiento de una unidad.** Es
exactamente el tipo de error que el agente auditor busca.

### 9.3 Tamaño de buffer: dimensionar, no adivinar

| Comando | Salida típica | Buffer |
|---|---|---|
| `gsmctl -q` | 4 líneas, ~60 bytes | 256 |
| `gsmctl -c` | 5 bytes | 256 |
| `ls /sys/class/net/` | 100–300 bytes | 512 |
| `sha256sum <archivo>` | 70 bytes + nombre | 512 |
| **`iwinfo wlan0 assoclist`** | **~250 bytes × clientes** | **8192** |

> El último es la lección de **I-001**: con 1024 bytes solo caben ~4 clientes y el conteo se
> trunca silenciosamente. **Dimensionar el buffer a partir del peor caso real, no del caso
> típico.**

---

## 10. `labcore_log` — Logging

### 10.1 Por qué a `stderr` y con `fflush`

- `procd` captura `stdout`/`stderr` del proceso del servicio y los mete en el log del sistema
- Se lee con `logread`
- El `fflush` es **obligatorio**: sin él, los logs se pierden en un buffer si el proceso muere

### 10.2 Niveles

| Nivel | Uso | Ejemplo |
|---|---|---|
| `INICIO` | arranque del servicio | version, broker, config cargada |
| `INFO` | evento normal noteworthy | umbral cruzado, config recargada |
| `PING` | keepalive interno | cada PINGREQ |
| `ALERTA` | evento de la app | umbral excedido, ataque detectado |
| `WARNING` | algo va mal pero se sigue | archivo de config ausente |
| `ERROR` | error real | fallo de conexión MQTT, comando fallido |
| `FIN` | cierre limpio | SIGTERM recibido, saliendo |

### 10.3 Formato

```
[2026-10-02 14:23:07] [ALERTA] port-scan-detector: src=192.168.1.77 ports=24 ventana=2s umbral=10
```

Con timestamp para correlación con `logread`, nivel para filtrado, y prefijo con el nombre de
la app para que 15 servicios coexistiendo sean distinguibles.

---

## 11. INTEGRACIÓN: EL ESQUELETO DE UNA APP

Todas las 15 apps tienen la misma forma:

```c
/* ---- app.c ---- */
#include "labcore.h"

#define APP_NAME "port-scan-detector"
#define CONF_PATH "/etc/port-scan-detector.conf"
#define STATE_DIR "/usr/local/lib/port-scan-detector/"

static lc_config_t g_cfg;
static lc_state_t  g_state;
static lc_threshold_t g_thr;

static void on_alert(void)
{
    char json[512];
    /* construir el payload específico de la app */
    lc_json_begin(json, sizeof(json));
    lc_json_str(&json, sizeof(json), "app", APP_NAME, 1);
    lc_json_int(&json, sizeof(json), "ports", g_n_ports);
    lc_json_end(json);

    char topic[128];
    snprintf(topic, sizeof(topic), "teltonika/alerts/%s", APP_NAME);
    lc_mqtt_publish(&g_mqtt, topic, json);
    lc_log("ALERTA", "%s", json);
}

int main(void)
{
    lc_signal_init();                          /* SIGTERM, SIGINT, SIGPIPE */

    if (lc_config_load(CONF_PATH, &g_cfg) != 0)
        lc_log("WARNING", "no se pudo leer %s; usando defaults", CONF_PATH);

    if (!g_cfg.user[0] || !g_cfg.pass[0])
        lc_log("ERROR", "credenciales MQTT no configuradas (%s)", CONF_PATH);
        /* NO abortar: el fallo debe ser visible, no silencioso */

    lc_mqtt_init(&g_mqtt, g_cfg.broker, g_cfg.port,
                 g_cfg.client_id, g_cfg.user, g_cfg.pass);
    lc_state_open(&g_state, STATE_DIR "state");

    lc_log("INICIO", "%s v%s — broker %s:%d, intervalo %ds",
           APP_NAME, APP_VERSION, g_cfg.broker, g_cfg.port, g_cfg.interval);

    lc_mqtt_connect(&g_mqtt);                 /* bloquea hasta conectar o fallar */

    while (lc_running()) {
        int n = collect(&data);                /* específico de cada app */

        lc_threshold_result_t r = lc_threshold_eval(&g_thr, data.value);
        if (r == LC_THRESHOLD_ALERT)
            on_alert();
        if (r == LC_THRESHOLD_CLEARED)
            lc_log("INFO", "recuperado");

        if (g_mqtt.needs_ping())
            lc_mqtt_ping(&g_mqtt);
        lc_mqtt_drain(&g_mqtt);                /* procesar PINGRESP / mensajes */

        lc_sleep_interruptible(g_cfg.interval);  /* despierta al recibir SIGTERM */
    }

    lc_mqtt_disconnect(&g_mqtt);
    lc_state_close(&g_state);
    lc_log("FIN", "%s detenido limpiamente", APP_NAME);
    return 0;
}
```

**El `main()` de las 15 apps es idéntico salvo por `collect()` y `on_alert()`.** Esa es la
prueba de que la arquitectura funciona.

---

## 12. MATRIZ DE USO DE `labcore`

| App | config | log | mqtt | json | threshold | state | netmon | cmd |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Port Scan Detector | ✓ | ✓ | ✓ | ✓ | — | — | ✓ | — |
| ICMP Flood Detector | ✓ | ✓ | ✓ | ✓ | — | — | ✓ | ✓ |
| Web Attack Detector | ✓ | ✓ | ✓ | ✓ | — | — | ✓ | — |
| SSH Bruteforce | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | ✓ |
| DoS Limiter | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ | — |
| WiFi Channel Optimizer | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ |
| CPU Monitor | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — |
| RAM Notifier | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — |
| Cellular Signal | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ |
| Cable Alert | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| WiFi New User | ✓ | ✓ | ✓ | ✓ | — | ✓ | — | ✓ |
| Login Monitor | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ |
| Disk Alert | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — |
| Temperature | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | ✓ |
| System File Monitor | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✓ |

---

## 13. COMPILACIÓN

```makefile
CC      = $(STAGING_DIR)/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin/mipsel-openwrt-linux-musl-gcc
CFLAGS  = -Os -Wall -Wextra -std=c11 -static
LDFLAGS = -static

all: $(APPS)

$(APPS): %: %.c labcore/labcore.a
	$(CC) $(CFLAGS) -o $@ $< labcore/labcore.a $(LDFLAGS)
	@echo "== verificando $@ ==" && file $@ | grep -q MIPS || \
		(echo "ERROR: no es MIPS"; exit 1)
```

> **El `| grep -q MIPS || exit 1` del `Makefile` es el guard más importante del curso.**
> Convierte la detección del error más común (`Exec format error` en el router) en un
> fallo de compilación en tu propia máquina, sin necesidad de tocar el router para
> descubrirlo.

---

**Siguiente documento:** [`AGENTES-IA.md`](./AGENTES-IA.md) — el protocolo completo de uso de
los agentes de IA.