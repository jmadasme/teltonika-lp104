# LP104 MQTT Teltonika V2 — Guía para Desarrolladores

---

## Tabla de contenidos

- [Arquitectura general](#arquitectura-general)
- [Estructura del proyecto](#estructura-del-proyecto)
- [Explicación del código fuente](#explicación-del-código-fuente)
- [Funciones principales](#funciones-principales)
- [Protocolo MQTT byte a byte](#protocolo-mqtt-byte-a-byte)
- [Sistema de telemetría](#sistema-de-telemetría)
- [Banner SSH](#banner-ssh)
- [Makefile](#makefile)
- [Init.d (procd)](#initd-procd)
- [Guía de modificación y extensión](#guía-de-modificación-y-extensión)
- [Diagrama de flujo de la lógica principal](#diagrama-de-flujo-de-la-lógica-principal)
- [Buenas prácticas](#buenas-prácticas)

---

## Arquitectura general

El programa sigue una arquitectura de **mono-hilo con event loop basado en `poll()`**. No hay bibliotecas externas — toda la implementación MQTT se realiza con syscall POSIX directas.

```
┌─────────────────────────────────────────────────────────────┐
│                    lp104mqttv2                               │
│                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │   Señales    │    │   Telemetría │    │    Logger    │  │
│  │  (SIGTERM,   │    │  (gsmctl,    │    │  (stderr →   │  │
│  │   SIGPIPE)   │    │   iwinfo,    │    │   procd)     │  │
│  └──────┬───────┘    │   /proc/stat)│    └──────────────┘  │
│         │            └──────┬───────┘                       │
│         ▼                   ▼                               │
│  ┌──────────────────────────────────────┐                   │
│  │          main() — Event Loop         │                   │
│  │  ┌─────────────────────────────────┐ │                   │
│  │  │  tcp_connect() → DNS + TCP      │ │                   │
│  │  │  mqtt_send_connect()            │ │                   │
│  │  │  mqtt_read_connack()            │ │                   │
│  │  │  publish_reports()              │ │                   │
│  │  │  ┌────────────────────────────┐ │ │                   │
│  │  │  │  poll() — 30s timeout      │ │ │                   │
│  │  │  │  drain_incoming()          │ │ │                   │
│  │  │  │  publish_reports() @ 30s   │ │ │                   │
│  │  │  │  mqtt_send_pingreq() @ 30s │ │ │                   │
│  │  │  └────────────────────────────┘ │ │                   │
│  │  └─────────────────────────────────┘ │                   │
│  │  backoff_sleep() → reconexión        │                   │
│  └──────────────────────────────────────┘                   │
│                                                             │
│  Socket TCP ────────▶ Broker MQTT ────────▶ Dashboard Web   │
└─────────────────────────────────────────────────────────────┘
```

### Dependencias del sistema

| Dependencia | Tipo | Uso |
|---|---|---|
| `socket()`, `connect()`, `read()`, `write()` | POSIX syscall | Comunicación TCP/MQTT |
| `getaddrinfo()` | POSIX libc | Resolución DNS |
| `poll()` | POSIX | Multiplexación de E/S con timeout |
| `popen()` / `pclose()` | POSIX libc | Ejecución de comandos externos |
| `sigaction()` | POSIX | Manejo de señales |
| `snprintf()` | C stdio | Formateo de JSON |
| `strtoull()` | C stdio | Parsing de enteros 64-bit (/proc/stat) |
| `gsmctl` | Binario RutOS | Señal celular y temperatura del módem |
| `/sys/class/net/` | sysfs | Interfaces de red y WiFi |
| `/proc/stat` | procfs | Contadores de CPU |

No hay dependencias de librerías externas (sin libmosquitto, sin paho, sin libcurl).

---

## Estructura del proyecto

```
lp104MqttTeltonikaV2/
├── lp104mqttv2.c              # Código fuente principal (1153 líneas)
├── mqtt_config.h              # Defaults de conexión (GITIGNORED, no sensible)
├── mqtt_config.h.example      # Plantilla de defaults
├── lp104mqttv2.conf.example   # Plantilla del .conf del router (placeholders) — V2.1
├── lp104mqttv2.conf           # Credenciales reales del despliegue (GITIGNORED, SOLO local)
├── Makefile                   # Cross-compilación y despliegue
├── init.d/
│   └── lp104mqttv2            # Script procd para init.d
├── banner/
│   └── 99-lp104-banner.sh     # Banner SSH (ASCII art)
├── .gitignore                 # Exclusiones del repositorio
├── PLAN.md                    # Plan de implementación
├── AUDIT_REPORT.md            # Reporte de auditoría
├── docs/
│   ├── datasheet.md           # Hoja de datos y guía de usuario
│   ├── developer.md           # Esta guía
│   └── zerotier-guide.md      # Guía ZeroTier para flota
└── README.md                  # Documentación principal
```

---

## Explicación del código fuente

### lp104mqttv2.c — Estructura general

El archivo se organiza en las siguientes secciones:

```
Líneas 1-30:     Includes, defines, macros
Líneas 31-60:    Constantes de configuración
Líneas 61-90:    Variables globales (incluye struct mqtt_cfg g_cfg)
Líneas 91-140:   Funciones auxiliares (logging, señales, config_load)
Líneas 141-200:  Funciones MQTT de bajo nivel (encoding, write_all)
Líneas 201-320:  Funciones de conexión TCP y paquetes MQTT
Líneas 321-380:  Funciones de telemetría (parsing de comandos)
Líneas 381-500:  Funciones de recopilación de métricas
Líneas 501-560:  Funciones de serialización JSON
Líneas 561-620:  Función de publicación de reportes
Líneas 621-1153: main() — config_load() + loop principal
```

### Inclusión de configuración

```c
#include "mqtt_config.h"
```

Los defines que aporta (`MQTT_BROKER`, `MQTT_PORT`, `MQTT_CLIENT`) son **defaults de conexión
NO sensibles**: se usan si el `.conf` del router no los sobreescribe.

> **V2.1**: `mqtt_config.h` ya **NO contiene** `MQTT_USERNAME` ni `MQTT_PASSWORD`. Las
> credenciales se cargan en runtime desde `/etc/lp104mqttv2.conf` mediante `config_load()`.

### Constantes de configuración

```c
#define REPORT_INTERVAL_S   30   // Segundos entre publicaciones
#define MQTT_KEEPALIVE_S    60   // Keepalive MQTT (segundos)
#define PING_THRESHOLD_S    30   // Umbral para envío de PINGREQ
#define CONNECT_TIMEOUT_S   10   // Timeout conexión TCP/CONNACK
#define BACKOFF_INITIAL_S   5    // Backoff inicial de reconexión
#define BACKOFF_MAX_S       60   // Backoff máximo
#define WRITE_TIMEOUT_S     5    // Timeout de escritura

#define CMD_GSM_SIGNAL  "/usr/sbin/gsmctl -q"
#define CMD_GSM_TEMP    "/usr/sbin/gsmctl -c"
#define CMD_LS_NET      "/bin/ls /sys/class/net/"
#define PATH_PROC_STAT  "/proc/stat"
```

### Variables globales

```c
static volatile sig_atomic_t g_running = 1;       // Flag de ejecución
static char g_topic_online[256];                   // Tópico report/online
static char g_topic_status[256];                   // Tópico report/status
static struct mqtt_cfg g_cfg;                      // Configuración MQTT (V2.1)
```

```c
// struct mqtt_cfg — configuración MQTT cargada en runtime (V2.1)
typedef struct {
    char broker[CFG_BROKER_LEN];    // Host del broker (default de mqtt_config.h)
    int  port;                      // Puerto del broker (default de mqtt_config.h)
    char client_id[CFG_CLIENT_LEN]; // Client ID  (default de mqtt_config.h)
    char user[CFG_USER_LEN];        // Usuario MQTT — desde /etc/lp104mqttv2.conf
    char pass[CFG_PASS_LEN];        // Password MQTT — desde /etc/lp104mqttv2.conf
} mqtt_cfg_t;
// (CFG_*_LEN: 64 / 64 / 64 / 128 respectivamente)
```

- `g_running`: Se pone en 0 cuando se recibe SIGTERM/SIGINT, causando que el loop principal termine graceful
- `g_topic_online/status`: Se construyen al inicio concatenando `{g_cfg.client_id}/report/online` y `{g_cfg.client_id}/report/status`
- `g_cfg`: se inicializa con los defaults de `mqtt_config.h` y `user`/`pass` vacíos; después `config_load()` lo sobreescribe con el contenido del `.conf`

---

## Funciones principales

### Funciones auxiliares

#### `log_msg(level, fmt, ...)`

```c
static void log_msg(const char *level, const char *fmt, ...);
```

Imprime mensajes de log con timestamp a stderr. Procd captura stdout/stderr y los redirige a `logread`. Formato:

```
Sep 16 10:00:01 router lp104mqttv2[1234]: [INFO] Mensaje aquí
```

**Implementación**: Usa `gettimeofday()` para timestamp, `vsnprintf()` para formateo variádico, `fprintf(stderr, ...)` para salida.

#### `on_signal(sig)`

```c
static void on_signal(int sig);
```

Handler para SIGTERM y SIGINT. Simplemente pone `g_running = 0`, lo que causa que el loop principal termine en la próxima iteración.

#### `init_signals()`

```c
static void init_signals(void);
```

Registra handlers de señales con `sigaction()`:
- SIGTERM → `on_signal()`
- SIGINT → `on_signal()`
- SIGPIPE → `SIG_IGN` (ignorar — previene crash si el socket se cierra)

#### `config_load(cfg)`  ← NUEVO en V2.1

```c
static int config_load(struct mqtt_cfg *cfg);
```

Carga la configuración MQTT desde la ruta `CONFIG_PATH` (por defecto
`/etc/lp104mqttv2.conf`). Formato `clave=valor`, una por línea; los espacios alrededor de
clave y valor se recortan (`trim_ws`); las líneas vacías y los comentarios `#` se ignoran.

**Claves reconocidas (case-sensitive):**

| Clave | Destino | Validación |
|---|---|---|
| `user` | `cfg->user` | copia con límite `CFG_USER_LEN` |
| `pass` | `cfg->pass` | copia con límite `CFG_PASS_LEN` |
| `client_id` | `cfg->client_id` | copia con límite `CFG_CLIENT_LEN` |
| `broker` | `cfg->broker` | copia con límite `CFG_BROKER_LEN` |
| `port` | `cfg->port` | `strtol` + rango 1–65535; inválido → WARNING y se conserva el default |

**Manejo de errores:**

- Línea sin `=` o clave desconocida → `WARNING`, se ignora
- Línea > 255 bytes → `WARNING` (se drena el resto de la línea)
- `fopen()` falla → `WARNING` `no existe /etc/lp104mqttv2.conf (...); usando defaults compilados`, retorna `-1`
- **NUNCA loguea la password** (solo el usuario y los otros campos)

**Retorno**: `0` en éxito, `-1` si no se pudo abrir el archivo (el proceso continúa con defaults).

**Uso en `main()`** (lo primero después de las señales):

```c
init_signals();
if (config_load(&g_cfg) != 0) {
    log_msg("WARNING", "...");          // ya logueado por config_load
}
if (g_cfg.user[0] == '\0' || g_cfg.pass[0] == '\0') {
    log_msg("ERROR",
            "credenciales MQTT no configuradas (falta /etc/lp104mqttv2.conf)");
}
// El arranque continúa: el broker rechazará el CONNECT (fallo visible)
```

La ruta del `.conf` se sobreescribe con la variable de entorno `LP104MQTTV2_CONF` o con el flag
de compilación `-DCONFIG_PATH="..."`.

> **Diseño (V2.1)**: el binario deja de contener secretos. El mecanismo de fallo es *fail
> visible*: sin `.conf` el proceso arranca, pero el broker responde CONNACK con código ≠ 0 y el
> error queda en `logread`. La password jamás aparece en logs.

---

### Funciones MQTT de bajo nivel

#### `mqtt_remaining_length(n, out)`

```c
static int mqtt_remaining_length(int n, unsigned char *out);
```

Codifica la longitud variable-length MQTT en 1-4 bytes. El protocolo MQTT usa un encoding especial donde cada byte tiene 7 bits de dato y 1 bit de continuación.

**Formato**:
- Si n < 128: 1 byte (0xxxxxxx)
- Si n < 16384: 2 bytes (1xxxxxxx 0xxxxxxx)
- Si n < 2097152: 3 bytes
- Si n < 268435456: 4 bytes

**Ejemplo**:
```c
unsigned char buf[4];
int len = mqtt_remaining_length(12, buf);
// buf = {0x0C}, len = 1 (12 < 128, un solo byte)
```

#### `write_all(fd, buf, len)`

```c
static ssize_t write_all(int fd, const void *buf, size_t len);
```

Escribe `len` bytes en el file descriptor, manejando:
- `EAGAIN`/`EWOULDBLOCK`: Espera con `poll()` y reintenta
- `EINTR`: Reintenta automáticamente
- Timeout: Si `poll()` no está listo en `WRITE_TIMEOUT_S`, retorna error

**Retorno**: Número total de bytes escritos, o -1 en error.

#### `tcp_connect(host, port)`

```c
static int tcp_connect(const char *host, int port);
```

Establece conexión TCP al broker:
1. `getaddrinfo()` para resolución DNS (soporta IPv4 e IPv6)
2. `socket()` no-bloqueante
3. `connect()` con deadline via `poll()` (timeout 10s)
4. `setsockopt(TCP_NODELAY)` para reducir latencia

**Retorno**: File descriptor del socket conectado, o -1 en error.

#### `mqtt_send_connect(fd)`

```c
static int mqtt_send_connect(int fd);
```

Construye y envía el paquete MQTT CONNECT byte a byte:

```
Byte 0:     0x10 (CONNECT, clean session)
Byte 1:     Remaining length (variable)
Bytes 2-3:  0x00 0x04 "MQTT"
Byte 4:     0x04 (protocol level 3.1.1)
Byte 5:     0x02 (clean session flag)
Bytes 6-7:  0x00 0x3C (keepalive = 60s)
Bytes 8+:   Client ID + flags de usuario/contraseña
```

Los campos sensitivos se leen del struct `g_cfg` (V2.1):
- Client ID → `g_cfg.client_id` (default `mqtt_config.h` o `client_id=` del `.conf`)
- Username → `g_cfg.user` (del `.conf`; vacío si no existe)
- Password → `g_cfg.pass` (del `.conf`; vacío si no existe)

Los flags de usuario y contraseña se activan **siempre** (`0x02 | 0x80 | 0x40`); si `user`/`pass`
están vacíos, el broker rechaza el CONNECT de forma explícita (fallo visible).

**Nota**: La contraseña se envía en texto plano (sin TLS) — el `.conf` debe tener permisos
`600` y el binario nunca la loguea.

#### `mqtt_read_connack(fd)`

```c
static int mqtt_read_connack(int fd);
```

Espera la respuesta CONNACK del broker con timeout:
1. Lee 4 bytes (2 fijo + 2 variable)
2. Valida que el byte 0 sea `0x20` (CONNACK)
3. Valida que el byte 3 sea `0x00` (rc=0, éxito)

**Retorno**: 0 en éxito, -1 en error.

#### `mqtt_publish(fd, topic, payload, plen)`

```c
static int mqtt_publish(int fd, const char *topic,
                        const char *payload, size_t plen);
```

Publica un mensaje MQTT QoS 0 sin retain:

```
Byte 0:     0x30 (PUBLISH, QoS 0, no retain)
Byte 1:     Remaining length
Bytes 2-3:  Topic length (big-endian)
Bytes 4+:   Topic string
Bytes N+:   Payload
```

**Parámetros**:
- `topic`: String del tópico (ej: `"6aaa97.../report/status"`)
- `payload`: Datos a publicar (JSON o string)
- `plen`: Longitud del payload

#### `mqtt_send_pingreq(fd)`

```c
static int mqtt_send_pingreq(int fd);
```

Envía PINGREQ (2 bytes fijos):
```
Byte 0: 0xC0 (PINGREQ)
Byte 1: 0x00
```

#### `drain_incoming(fd)`

```c
static int drain_incoming(int fd);
```

Lee y descarta datos entrantes del socket (PINGRESP, etc.) con `poll()` no bloqueante. Esto es necesario porque el broker puede enviar PINGRESP en cualquier momento.

---

### Funciones de backoff

#### `backoff_sleep(delay)`

```c
static void backoff_sleep(int delay);
```

Espera `delay` segundos con sleep segundo a segundo. Esto permite que SIGTERM interrumpa el sleep. El delay se duplica en cada llamada (backoff exponencial: 5, 10, 20, 40, 60, 60...).

**Implementación**:
```c
for (int i = 0; i < delay && g_running; i++) {
    sleep(1);
}
```

---

## Sistema de telemetría

### Funciones de parsing

#### `read_cmd_line(cmd, out, n)`

```c
static int read_cmd_line(const char *cmd, char *out, size_t n);
```

Ejecuta un comando del sistema via `popen()` y captura la primera línea de salida. Los comandos están hardcodeados (no hay interpolación de usuarios — prevención de inyección de comandos).

**Ejemplo de uso**:
```c
char buf[256];
read_cmd_line("/usr/sbin/gsmctl -q", buf, sizeof(buf));
// buf contiene: "-75\n" o "-75 dBm\n"
```

#### `parse_rssi(out, *rssi)`

```c
static int parse_rssi(const char *out, int *rssi);
```

Extrae el valor RSSI numérico de la salida de `gsmctl -q`. Busca el primer número entero en la cadena.

**Salida típica de gsmctl -q**:
```
-75
```
o
```
-75 dBm
```

#### `parse_temp_tenths(out, *temp)`

```c
static int parse_temp_tenths(const char *out, float *temp);
```

Extrae la temperatura del módem de la salida de `gsmctl -c`. La temperatura viene en décimas de °C (ej: `352` → 35.2°C).

#### `cpu_read(sample)`

```c
static int cpu_read(uint64_t sample[7]);
```

Lee `/proc/stat` y extrae los contadores de CPU (user, nice, system, idle, iowait, irq, softirq). Usa `strtoull()` (64-bit) para manejar contadores que pueden desbordar 32 bits en sistemas con uptime prolongado.

**Formato de /proc/stat**:
```
cpu  12345 678 9012 345678 90 12 34 0 0 0
     user  nice sys idle  iow irq sirq steal guest guest_nice
```

#### `cpu_percent(pct)`

```c
static int cpu_percent(float *pct);
```

Calcula el porcentaje de CPU entre dos muestras. La primera llamada hace una pausa de 200ms (`usleep(200000)`) para obtener un delta significativo.

**Cálculo**:
```
delta_total = (user2+nice2+sys2+idle2+...) - (user1+nice1+sys1+idle1+...)
delta_idle  = idle2 - idle1
pct = (1.0 - (float)delta_idle / delta_total) * 100.0
```

#### `wifi_users(*users)`

```c
static int wifi_users(int *users);
```

Cuenta los clientes WiFi asociados al router:
1. Ejecuta `ls /sys/class/net/` para enumerar interfaces
2. Para cada interfaz, verifica si es inalámbrica (prefijo `wlan`, `ra`, o `ath`)
3. Lee `/sys/class/net/{iface}/wireless/assoclist`
4. Cuenta líneas que comienzan con dirección MAC (formato `xx:xx:xx:xx:xx:xx`)

**Buffer**: 8192 bytes para el assoclist (suficiente para ~130 clientes, dado que cada MAC ocupa ~20 bytes).

#### `iface_is_wireless(name)`

```c
static int iface_is_wireless(const char *name);
```

Verifica si una interfaz de red es inalámbrica buscando los prefijos:
- `wlan` — driver wlan genérico
- `ra` — driver Ralink/MediaTek
- `ath` — driver Atheros

#### `count_mac_lines(buf)`

```c
static int count_mac_lines(const char *buf);
```

Cuenta líneas en el buffer que comienzan con una dirección MAC válida. Una MAC se reconoce por el patrón `xx:xx:xx:xx:xx:xx` al inicio de la línea.

### Función de recolección

#### `collect_metrics(m)`

```c
typedef struct {
    int   signal;   // RSSI en dBm
    int   users;    // Clientes WiFi
    float cpu;      // % CPU
    float temp;     // Temperatura °C
    time_t ts;      // Timestamp epoch
} metrics_t;

static int collect_metrics(metrics_t *m);
```

Llena la estructura `metrics_t` llamando a las funciones de parsing individuales. Cada métrica se recopila independientemente — si una falla, las demás continúan.

### Serialización JSON

#### `build_status_json(m, out, n)`

```c
static int build_status_json(const metrics_t *m, char *out, size_t n);
```

Construye el JSON del payload status usando `snprintf()`:

```json
{"signal":-75,"users":3,"cpu":12.5,"temp":35.2,"ts":1695000000}
```

**Nota**: No se usa librería JSON externa. El formato es estático y conocido, por lo que `snprintf()` es suficiente.

### Publicación

#### `publish_reports(fd)`

```c
static int publish_reports(int fd);
```

Publica ambos reportes MQTT:
1. `mqtt_publish(fd, g_topic_online, "true", 4)`
2. `mqtt_publish(fd, g_topic_status, json, json_len)`

**Retorno**: 0 en éxito, -1 si alguna publicación falla (indica que se debe reconectar).

---

## Protocolo MQTT byte a byte

### Paquete CONNECT enviado

```
Offset  Hex       Descripción
------  --------  -----------
0       0x10      Tipo: CONNECT (0x01) + Flags: 0000
1       VarLen    Remaining length (variable-length encoding)
2       0x00      Protocol Name — MSB
3       0x04      Protocol Name — LSB (longitud = 4)
4       0x4D      'M'
5       0x51      'Q'
6       0x54      'T'
7       0x54      'T'
8       0x04      Protocol Level: 4 = MQTT 3.1.1
9       0x02      Connect Flags: Clean Session = 1
10      0x00      Keep Alive — MSB
11      0x3C      Keep Alive — LSB (60 segundos)
12      0x00      Client ID — MSB (longitud)
13      Len       Client ID — LSB
14+     ...       Client ID (string UTF-8)
+N      0x80      Username Flag (si hay usuario)
+N+1    Len MSB   Username length
+N+2    Len LSB   Username length
+N+3+   ...       Username (string UTF-8)
+M      0x80      Password Flag (si hay contraseña)
+M+1    Len MSB   Password length
+M+2    Len LSB   Password length
+M+3+   ...       Password (string UTF-8)
```

### Paquete PUBLISH enviado (QoS 0)

```
Offset  Hex       Descripción
------  --------  -----------
0       0x30      Tipo: PUBLISH (0x03) + Flags: 0000 (QoS 0, no retain)
1       VarLen    Remaining length
2       0x00      Topic length — MSB
3       Len       Topic length — LSB
4+      ...       Topic (string UTF-8)
+N      ...       Payload (JSON o string)
```

### Paquete PINGREQ enviado

```
Offset  Hex       Descripción
------  --------  -----------
0       0xC0      Tipo: PINGREQ (0x0C) + Flags: 0000
1       0x00      Remaining length: 0
```

### Variable-length encoding

El protocolo MQTT usa un encoding de longitud variable:

```
Si n < 128:
  1 byte: 0xxxxxxx (7 bits de dato)

Si 128 ≤ n < 16384:
  2 bytes: 1xxxxxxx 0xxxxxxx

Si 16384 ≤ n < 2097152:
  3 bytes: 1xxxxxxx 1xxxxxxx 0xxxxxxx

Si 2097152 ≤ n < 268435456:
  4 bytes: 1xxxxxxx 1xxxxxxx 1xxxxxxx 0xxxxxxx
```

El bit más significativo (MSB) de cada byte indica si hay más bytes (1 = continuación, 0 = último byte).

---

## Banner SSH

### Estructura

El banner se implementa como un script shell en `/etc/profile.d/`:

```bash
#!/bin/sh
# 99-lp104-banner.sh
# Se ejecuta al login SSH via /etc/profile → /etc/profile.d/*.sh

cat <<'LP104_BANNER_EOF'

        *
       /|\
      / | \
     /  |  \
    /   |   \
   /    |    \
  /_____|_____\
  
  LP104 — Telemetría MQTT activa
  Contacto: jmadasme@gmail.com
  
  Métricas: señal celular | WiFi | CPU | temperatura
  Client ID: 6aaa978111a8892fd2927dcc
  Tópicos: {id}/report/online | {id}/report/status
LP104_BANNER_EOF
```

### Cómo funciona

1. OpenWrt ejecuta `/etc/profile` al iniciar cada sesión SSH
2. `/etc/profile` carga todos los scripts de `/etc/profile.d/*.sh`
3. `99-lp104-banner.sh` ejecuta `cat` con un heredoc
4. El arte ASCII y la información se imprimen al usuario
5. No hay `exec`, no hay `read`, no hay pausa — el login continúa normalmente

### Diseño del banner

- **Sin nombre del programa**: No expone `lp104mqttv2` ni la ruta `/usr/local/bin/`
- **Sin ubicación**: No revela dónde está instalado el binario
- **Con client id**: Útil para diagnóstico cuando se reportan problemas
- **Con topics**: Facilita la depuración de suscripciones MQTT
- **Contacto**: Permite al usuario reportar problemas

### Fallback

Si `/etc/profile.d/` no funciona (configuración no estándar), el banner se puede añadir directamente a `/etc/banner`:
```bash
cat banner/99-lp104-banner.sh >> /etc/banner
```

---

## Makefile

### Estructura del Makefile

```makefile
# ─── Configuración del SDK ──────────────────────
SDK_DIR    ?= /home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64
TOOLCHAIN  = $(SDK_DIR)/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin
CC         = $(TOOLCHAIN)/mipsel-openwrt-linux-musl-gcc

# ─── Flags de compilación ───────────────────────
CFLAGS     = -static -Os -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS    = -static -Wl,--gc-sections

# ─── Configuración del router ──────────────────
ROUTER_IP   ?= 192.168.1.2
ROUTER_PORT ?= 22
ROUTER_USER ?= root
ROUTER_PASS ?=

# ─── Archivos del router ──────────────────────
ROUTER_BIN  = /usr/local/bin/lp104mqttv2
ROUTER_INIT = /etc/init.d/lp104mqttv2
ROUTER_BANNER = /etc/profile.d/99-lp104-banner.sh
ROUTER_V2_PATH ?= /usr/local/home/root/$(TARGET)_v2   # ruta temporal V2.1 (sin ejecutar)

# ─── V1 (a eliminar) ─────────────────────────
V1_BIN  = /usr/local/bin/lp104mqtt
V1_INIT = /etc/init.d/lp104mqtt
```

### Targets

| Target | Comandos | Descripción |
|---|---|---|
| `all` | `$(CC) ... -o lp104mqttv2 lp104mqttv2.c` | Compilación estándar |
| `install` | `make all` + `scp` × 3 + `ssh` × 3 | Despliegue completo (servicio) |
| `deploy-v2` | `make all` + `scp` binario + `.conf` | **V2.1**: despliega a `ROUTER_V2_PATH` SIN ejecutar |
| `test` | `scp` + `ssh` (ejecuta una vez) | Prueba rápida |
| `log` | `ssh` + `logread` | Muestra logs |
| `clean` | `rm -f lp104mqttv2 *.o` | Limpia artefactos |
| `uninstall-v1` | `ssh` (disable + rm) | Elimina versión anterior |

### Target `deploy-v2` (NUEVO en V2.1)

Flujo:

1. Compila el binario (igual que `all`)
2. `scp` del binario → `/usr/local/home/root/lp104mqttv2_v2` (`ROUTER_V2_PATH`)
3. Configuración del `.conf` del router:
   - Si existe `lp104mqttv2.conf` **local** (gitignored, credenciales reales) → `scp` a
     `/etc/lp104mqttv2.conf`
   - Si no existe → copia la plantilla `lp104mqttv2.conf.example` con un aviso para que el
     usuario la edite
4. En el router: `chmod +x` del binario y `chmod 600` del `.conf`
5. **NO ejecuta el binario, NO toca init.d, NO reinicia el servicio** — el usuario lo arranca
   manualmente: `/usr/local/home/root/lp104mqttv2_v2`

> Seguridad: el Makefile **no contiene credenciales reales** (incidencia I-1 resuelta). Las
> credenciales viven solo en el `.conf` local gitignored o se editan a mano en el router.

### Cross-compilación detallada

El Makefile ejecuta:

```bash
mipsel-openwrt-linux-musl-gcc \
    -static \                    # Enlace estático — sin .so
    -Os \                        # Optimización para tamaño
    -Wall -Wextra \              # Warnings estrictos
    -ffunction-sections \        # Secciones por función
    -fdata-sections \           # Secciones por variable
    -Wl,--gc-sections \          # Eliminar secciones no usadas
    -o lp104mqttv2 \             # Binario de salida
    lp104mqttv2.c                # Fuente
```

**Por qué estas flags**:
- `-static`: El binario no depende de librerías compartidas del sistema. Funciona en cualquier RUT200 sin instalar paquetes adicionales
- `-Os`: Minimiza el tamaño del binario (~95KB) — importante para flash de 16MB
- `-ffunction-sections -fdata-sections -Wl,--gc-sections`: Elimina código y datos no utilizados, reduciendo aún más el tamaño

### Variables del router

```bash
# Para usar con contraseña (sshpass):
export ROUTER_PASS="tu_password"
make install

# Para usar con SSH key (sin contraseña):
# Asegúrate de que tu clave SSH esté configurada
make install ROUTER_PASS=""
```

El Makefile usa `sshpass` si `ROUTER_PASS` está definido, o SSH key si está vacío.

---

## Init.d (procd)

### Script: `init.d/lp104mqttv2`

```bash
#!/bin/sh /etc/rc.common
# LP104 MQTT V2 — Telemetry service
# Installed to /etc/init.d/lp104mqttv2

START=95
STOP=10
USE_PROCD=1

PROG=/usr/local/bin/lp104mqttv2

start_service() {
    procd_open_instance
    procd_set_param command "$PROG"
    procd_set_param respawn 3600 5 5
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_close_instance
}

stop_service() {
    : # procd maneja SIGTERM automáticamente
}
```

### Parámetros procd

| Parámetro | Valor | Descripción |
|---|---|---|
| `START=95` | 95 | Orden de inicio (después de red, antes de servicios finales) |
| `STOP=10` | 10 | Orden de parada (antes que la mayoría de servicios) |
| `USE_PROCD=1` | 1 | Usa el framework procd para gestión del proceso |
| `respawn 3600 5 5` | — | Máx 3600s, delay inicial 5s, retry cada 5s |
| `stdout 1` | 1 | Redirige stdout a log del sistema |
| `stderr 1` | 1 | Redirige stderr a log del sistema |

### Comportamiento de respawn

```
Tiempo 0:   Inicio → proceso arranca
Tiempo 5:   Si el proceso murió → reintento tras 5s
Tiempo 10:  Si murió de nuevo → reintento tras 5s
...
Tiempo 3600: Si lleva 3600s con crashes → procd deja de reiniciarlo
```

### Gestión de señales

- `SIGTERM`: Enviado por procd al hacer `stop` → `on_signal()` → `g_running=0` → cierre graceful
- `SIGINT`: Ctrl+C en modo manual → mismo comportamiento
- `SIGPIPE`: Ignorado (evita crash si el broker cierra la conexión)

---

## Guía de modificación y extensión

### Cambiar el intervalo de publicación

Editar `REPORT_INTERVAL_S` en `lp104mqttv2.c`:

```c
#define REPORT_INTERVAL_S   60   // Cambiar de 30 a 60 segundos
```

Recompilar y redesplegar.

### Agregar una nueva métrica

1. **Agregar campo al struct** `metrics_t`:
```c
typedef struct {
    int   signal;
    int   users;
    float cpu;
    float temp;
    float new_metric;   // Nueva métrica
    time_t ts;
} metrics_t;
```

2. **Crear función de recolección**:
```c
static int collect_new_metric(float *value) {
    // Ejemplo: leer un archivo del sistema
    FILE *f = fopen("/sys/some/path", "r");
    if (!f) return -1;
    fscanf(f, "%f", value);
    fclose(f);
    return 0;
}
```

3. **Llamar en `collect_metrics()`**:
```c
collect_new_metric(&m->new_metric);
```

4. **Agregar al JSON en `build_status_json()`**:
```c
snprintf(out, n, "{\"signal\":%d,\"users\":%d,\"cpu\":%.1f,"
        "\"temp\":%.1f,\"new\":%.1f,\"ts\":%ld}",
        m->signal, m->users, m->cpu, m->temp,
        m->new_metric, (long)m->ts);
```

5. **Recompilar y redesplegar**.

### Cambiar el broker MQTT

**Sin recompilar (V2.1)**: editar `/etc/lp104mqttv2.conf` en el router:

```ini
broker=nuevo-broker.ejemplo.com
port=1883
```

y reiniciar el proceso.

**Default compilado** (se usa si el `.conf` no lo sobreescribe): editar `mqtt_config.h`:

```c
#define MQTT_BROKER   "nuevo-broker.ejemplo.com"
#define MQTT_PORT     1883
```

y recompilar.

### Cambiar credenciales MQTT (sin recompilar, V2.1)

Las credenciales viven en `/etc/lp104mqttv2.conf` del router, no en el código:

```ini
user=YOUR_MQTT_USERNAME
pass=YOUR_MQTT_PASSWORD
```

- Editar el archivo en el router y reiniciar el proceso
- `config_load()` las lee en cada arranque — no hace falta tocar `mqtt_config.h` ni el binario
- Recordar `chmod 600 /etc/lp104mqttv2.conf`
- La plantilla versionada es `lp104mqttv2.conf.example` (placeholders `YOUR_*`)

### Cambiar los tópicos MQTT

Los tópicos se construyen en `main()`:

```c
snprintf(g_topic_online, sizeof(g_topic_online),
         "%s/report/online", MQTT_CLIENT);
snprintf(g_topic_status, sizeof(g_topic_status),
         "%s/report/status", MQTT_CLIENT);
```

Para cambiar el formato, editar estas líneas en `main()`.

### Agregar soporte MQTT con TLS

El cliente actual no soporta TLS. Para agregarlo:

1. **Instalar OpenSSL en el SDK**:
```bash
cd $SDK_DIR
make menuconfig  # Habilitar libopenssl
make package/openssl/compile V=s
```

2. **Modificar `tcp_connect()`** para usar `SSL_new()`, `SSL_connect()` después del `connect()` TCP

3. **Reemplazar `write()`/`read()`** con `SSL_write()`/`SSL_read()`

4. **Agregar `-lssl -lcrypto` al Makefile**:
```makefile
LDFLAGS += -lssl -lcrypto
```

> **Nota**: Esto aumentará significativamente el tamaño del binario (~500KB+).

### Cambiar la libc (de musl a uclibc)

No recomendado. El SDK OpenWrt 21.02.0 usa musl por defecto. Si se necesita uclibc (routers legacy):

1. Usar SDK de OpenWrt anterior (19.07.x o anterior)
2. Cambiar la ruta del toolchain en el Makefile
3. Verificar compatibilidad del binario

---

## Diagrama de flujo de la lógica principal

```
main()
│
├─ init_signals()
│   ├─ sigaction(SIGTERM, on_signal)
│   ├─ sigaction(SIGINT, on_signal)
│   └─ signal(SIGPIPE, SIG_IGN)
│
├─ config_load(&g_cfg)                    ← V2.1
│   ├─ fopen /etc/lp104mqttv2.conf (CONFIG_PATH)
│   ├─ parse user / pass / client_id / broker / port
│   ├─ si falta el archivo → WARNING (usa defaults) + ERROR visible
│   └─ g_cfg queda con los valores finales (pass nunca se loguea)
│
├─ Construir tópicos (con g_cfg.client_id):
│   ├─ g_topic_online = "{g_cfg.client_id}/report/online"
│   └─ g_topic_status = "{g_cfg.client_id}/report/status"
│
├─ log_msg("lp104mqttv2 arrancando")
│
└─ BUCLE PRINCIPAL (while g_running)
    │
    ├─ tcp_connect(g_cfg.broker, g_cfg.port)    ← usa g_cfg (V2.1)
    │   ├─ getaddrinfo() → DNS resolve
    │   ├─ socket() no-bloqueante
    │   ├─ connect() con poll() timeout 10s
    │   └─ TCP_NODELAY
    │
    ├─ mqtt_send_connect(fd)                    ← usa g_cfg.client_id/user/pass
    │   └─ Construir paquete CONNECT byte a byte
    │
    ├─ mqtt_read_connack(fd)
    │   └─ Esperar 4 bytes, validar rc=0 (rc≠0 → ERROR "CONNACK rechazado")
    │
    ├─ publish_reports(fd)
    │   ├─ mqtt_publish(topic_online, "true")
    │   └─ mqtt_publish(topic_status, JSON)
    │
    ├─ BUCLE DE SESIÓN (while g_running)
    │   │
    │   ├─ poll(fd, POLLIN, timeout=30s)
    │   │
    │   ├─ Si hay datos entrantes:
    │   │   └─ drain_incoming(fd)
    │   │       └─ Leer/descartar PINGRESP
    │   │
    │   ├─ Si poll timeout (30s sin tráfico):
    │   │   ├─ collect_metrics()
    │   │   │   ├─ read_cmd_line("gsmctl -q") → parse_rssi()
    │   │   │   ├─ wifi_users() → count_mac_lines()
    │   │   │   ├─ cpu_percent() → cpu_read()
    │   │   │   ├─ read_cmd_line("gsmctl -c") → parse_temp_tenths()
    │   │   │   └─ time(NULL)
    │   │   │
    │   │   ├─ build_status_json()
    │   │   │
    │   │   └─ publish_reports(fd)
    │   │       ├─ mqtt_publish(topic_online, "true")
    │   │       └─ mqtt_publish(topic_status, JSON)
    │   │
    │   ├─ Si hay tráfico entrante (no timeout):
    │   │   └─ mqtt_send_pingreq(fd)
    │   │
    │   └─ Si error en poll/write:
    │       └─ break (salir del bucle de sesión)
    │
    ├─ close(fd)
    │
    ├─ log_msg("Desconectado, backoff...")
    │
    └─ backoff_sleep(delay)
        └─ delay = min(delay * 2, BACKOFF_MAX_S)
```

---

## Buenas prácticas

### Al modificar el código

1. **No hardcodear credenciales** en el código fuente — V2.1: van en `/etc/lp104mqttv2.conf`
   del router (leídas por `config_load()`). `mqtt_config.h` solo define defaults no sensibles
2. **Mantener los comandos del sistema hardcodeados** — no interpoles输入 del usuario en `popen()`
3. **Usar `strtoull()` en lugar de `strtoul()`** para contadores de 64-bit
4. **Manejar EINTR y EAGAIN** en todas las operaciones de E/S
5. **Probar con `file` después de compilar** — verificar que el binario es MIPS

### Al agregar métricas

1. **Mantener el formato JSON** — el dashboard web espera un formato específico
2. **No usar librerías JSON** — el formato es simple y estático, `snprintf()` es suficiente
3. **Manejar errores de parsing** — si `gsmctl` falla, reportar 0 en lugar de crashear

### Al modificar el Makefile

1. **Nunca cambiar el toolchain por defecto** sin verificar la arquitectura
2. **Mantener `-static`** — el binario no debe depender de .so del sistema
3. **Probar con `file`** después de cada cambio en las flags

### Al modificar init.d

1. **Mantener `USE_PROCD=1`** — el framework procd es el estándar en OpenWrt moderno
2. **No cambiar START/STOP** sin entender las dependencias de arranque
3. **Mantener el respawn** — previene que el servicio desaparezca tras un crash
