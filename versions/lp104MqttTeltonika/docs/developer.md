# Developer Guide — lp104MqttTeltonika

Guía de desarrollo del cliente MQTT 3.1.1 en C puro para Teltonika RUT200: arquitectura del código, implementación del protocolo a mano, manejo de errores, cross-compilación y extensión.

---

## 1. Arquitectura del código

### 1.1 Vista general

El programa es **un único hilo** con un bucle principal infinito y `poll()` con timeouts. No usa hilos ni librerías externas: la capa MQTT (CONNECT/CONNACK/PUBLISH/PINGREQ) está implementada a mano sobre sockets POSIX.

```
┌─────────────────────────────────────────────────────────────┐
│  main()                                                     │
│  ├─ init_signals()                                          │
│  ├─ snprintf(g_topic, "%s/%s", MQTT_CLIENT, "report/online")│
│  └─ while (g_running)                                       │
│      ├─ tcp_connect()        ← DNS + socket + connect NB    │
│      ├─ mqtt_send_connect()  ← paquete CONNECT              │
│      ├─ mqtt_read_connack()  ← validación del broker        │
│      ├─ mqtt_publish()       ← PUBLISH inmediato            │
│      ├─ while (g_running)    ← bucle de sesión              │
│      │   ├─ poll()           ← timeout = próximo evento     │
│      │   ├─ drain_incoming() ← respuesta entrante/EOF       │
│      │   ├─ (hora) mqtt_publish()  cada 30 s                │
│      │   └─ (hora) mqtt_send_pingreq() si >30 s sin TX      │
│      ├─ close(fd)                                           │
│      └─ backoff_sleep()      ← 5 s → 60 s exponencial       │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 Estados de la máquina de sesión

| Estado | Transición de entrada | Transición de salida |
|---|---|---|
| `INICIO` | arranque (`main`) | `CONECTANDO` |
| `CONECTANDO` | `tcp_connect()` OK | `AUTENTICANDO` / error → backoff |
| `AUTENTICANDO` | CONNECT enviado | `ACTIVO` (CONNACK rc=0) / error → backoff |
| `ACTIVO` | CONNACK OK | publish inmediato `true` → `SESIÓN` |
| `SESIÓN` | publish inmediato OK | publish cada 30 s; error/EOF → close + backoff |
| `APAGADO` | SIGINT/SIGTERM | salida 0 |

---

## 2. Archivos del proyecto

| Archivo | Rol |
|---|---|
| `lp104mqtt.c` | Código fuente principal (~590 líneas) |
| `mqtt_config.h` | Credenciales reales (no versionado) |
| `mqtt_config.h.example` | Plantilla versionada con placeholders |
| `Makefile` | Cross-compilación con la toolchain del SDK + install/test remoto |
| `init.d/lp104mqtt` | Script de arranque procd |
| `.gitignore` | Excluye binario, credenciales, SDK, docs de trabajo |

---

## 3. Descripción de funciones

### 3.1 Logging y señales

| Función | Propósito |
|---|---|
| `log_msg(level, fmt, ...)` | Log timestamped a `stderr` con formato `[YYYY-MM-DD HH:MM:SS] [LEVEL] msg`. Usa `localtime_r` + `strftime` + `vfprintf` + `fflush` (log inmediato, importante bajo procd). |
| `on_signal(sig)` | Handler de señales: pone `g_running = 0` (variable `volatile sig_atomic_t`). |
| `init_signals()` | Registra `SIGINT`/`SIGTERM` con `sigaction` e **ignora `SIGPIPE`** (escribir en un socket cerrado debe dar `EPIPE`, no matar el proceso). |

### 3.2 Capa MQTT

| Función | Propósito |
|---|---|
| `mqtt_remaining_length(n, out)` | Codifica el *remaining length* de MQTT como entero de longitud variable (1–4 bytes, bit 0x80 = continuar). Devuelve bytes escritos. |
| `mqtt_send_connect(fd)` | Construye y envía el paquete **CONNECT** 3.1.1 completo. Ver detalle en sección 4.2. |
| `mqtt_read_connack(fd)` | Lee el **CONNACK** (4 bytes) con `poll(POLLIN)` + deadline de 10 s. Devuelve 0 si return code = 0; -1 en cualquier otro caso (timeout, EOF, error, código rechazado). Traduce códigos 1–5 a texto legible. |
| `mqtt_publish(fd, topic, payload, plen)` | Construye y envía **PUBLISH QoS 0** (`0x30`), sin packet id ni retain. Añade guards de tamaño del buffer. |
| `mqtt_send_pingreq(fd)` | Envía **PINGREQ** (`0xC0 0x00`) |

### 3.3 Transporte

| Función | Propósito |
|---|---|
| `tcp_connect(host, port)` | `getaddrinfo()` + bucle sobre las direcciones resueltas: `socket()`, `fcntl(O_NONBLOCK)` antes de `connect()`, manejo de `EINPROGRESS` con `poll(POLLOUT)` + deadline `CONNECT_TIMEOUT_S` (10 s), validación real con `getsockopt(SO_ERROR)`. Al éxito aplica `TCP_NODELAY`. Devuelve fd o -1. |
| `write_all(fd, buf, len)` | Envía todo el buffer en un bucle `send()`. Maneja `EINTR` (reintenta), `EAGAIN`/`EWOULDBLOCK` (espera `poll(POLLOUT)` hasta `WRITE_TIMEOUT_S` = 5 s y reintenta), error/timeout real → -1. |
| `drain_incoming(fd)` | Drena datos entrantes con `recv()` hasta `EAGAIN`. Devuelve 0 si OK, -1 si EOF (`recv()==0`) o error real (conexión perdida). Los PINGRESP/acks se descartan (cliente QoS 0 sin estado). |

### 3.4 Control del bucle principal

| Función | Propósito |
|---|---|
| `backoff_sleep(delay*)` | Duerme en pasos de 1 s comprobando `g_running` (para responder a señales durante la espera). Duplica `*delay` hasta `BACKOFF_MAX_S` (60 s). Devuelve el delay usado. |

---

## 4. Implementación del protocolo MQTT a mano

### 4.1 Formato general de un paquete MQTT

```
+──────────────────────+──────────────────────────+──────────+
|  Fixed header        |  Variable header         | Payload  |
|  (tipo + flags)      |  (según tipo)            | (según)  |
|  1 byte + RL varint  |                          |          |
+──────────────────────+──────────────────────────+──────────+
```

### 4.2 CONNECT (enviado por `mqtt_send_connect`)

```
Byte 0        : 0x10 (CONNECT, flags 0)
RL (varint)   : remaining length = 10 (variable header) + payload
Variable header (10 bytes):
  00 04 'M' 'Q' 'T' 'T'     ← protocol name "MQTT", len 2-byte BE
  04                        ← protocol level 4 = MQTT 3.1.1
  C2                        ← connect flags: clean session(0x02) |
                               username(0x80) | password(0x40)
  00 3C                     ← keepalive 60 s (big-endian)
Payload (campos UTF-8: 2 bytes de longitud BE + contenido):
  00 <len> client_id
  00 <len> username
  00 <len> password
```

Detalles de implementación:

- El paquete se ensambla en dos buffers: `pkt[512]` (variable header + payload) y `out[600]` (fixed header + todo). Se usa `pos` como índice, no punteros.
- **Guards de overflow** (requisito I-004):
  ```c
  if (10 + 2 + cid_len + 2 + user_len + 2 + pass_len > sizeof(pkt)) { ... return -1; }
  if (1 + (size_t)rl_len + pl > sizeof(out)) { ... return -1; }
  ```
- **Atención al variable header**: `pl = pos` se toma **después** de escribir los 10 bytes del variable header, por lo que `remaining length = variable header + payload` (correcto según MQTT 3.1.1). Bug histórico evitado (ver `AUDIT_REPORT.md`).

### 4.3 CONNACK (leído por `mqtt_read_connack`)

```
Byte 0 : 0x20 (CONNACK)
Byte 1 : 0x02 (remaining length = 2)
Byte 2 : session present (0/1)
Byte 3 : return code (0 = aceptado)
```

Se leen 4 bytes con `poll(POLLIN)` y deadline `CONNECT_TIMEOUT_S`. Validaciones: tipo `0x20`, return code 0 → OK; códigos 1–5 → `log_msg` con texto legible y retorno -1 (reconexión con backoff).

### 4.4 PUBLISH QoS 0 (enviado por `mqtt_publish`)

```
Byte 0        : 0x30 (PUBLISH, QoS 0, retain 0)
RL (varint)   : 2 + strlen(topic) + payload_len
00 <len> topic      ← topic UTF-8 (2 bytes BE de longitud)
<payload>           ← "true" (4 bytes, sin '\0')
```

Sin packet identifier (solo existe con QoS ≥ 1). Guard de tamaño: `if (rem + 2 + 4 > sizeof(pkt))`.

### 4.5 PINGREQ / PINGRESP

- **PINGREQ**: `0xC0 0x00` (2 bytes) — enviado por `mqtt_send_pingreq`.
- **PINGRESP**: `0xD0 0x00` — se descarta en `drain_incoming()` (el cliente no mantiene estado de ping). La presencia de datos entrantes ya confirma que la conexión está viva.

---

## 5. Bucle principal y manejo de errores

### 5.1 Bucle de sesión (`while (g_running)` en `main`)

```c
time_t next_publish = last_tx + REPORT_INTERVAL_S;   /* 30 s */
time_t next_ping    = last_tx + PING_THRESHOLD_S;    /* 30 s */
timeout_ms = min(next_publish, next_ping) - now;

poll(&pfd, 1, timeout_ms);        /* POLLIN */
if (pr > 0) drain_incoming(fd);   /* procesa entrada/EOF */

if (now >= next_publish)          /* publish prevalece sobre ping */
    mqtt_publish(...); last_tx = now; backoff = BACKOFF_INITIAL_S;
else if (now - last_tx >= PING_THRESHOLD_S)
    mqtt_send_pingreq(...); last_tx = now;
```

Notas:

- El orden `publish` antes que `ping` hace que en estado estable el PINGREQ **nunca** se dispare (la publicación de cada 30 s mantiene viva la sesión y resetea `last_tx`). El PINGREQ es una defensa ante bloqueos de publicación.
- Cualquier retorno `-1` de `publish`/`ping`/`drain_incoming` hace `break` del bucle de sesión → `close(fd)` → `backoff_sleep()`.
- `poll()` con `EINTR` simplemente `continue` (la señal pudo haber cambiado `g_running`).

### 5.2 Manejo de errores — matriz

| Fallo | Dónde se detecta | Acción |
|---|---|---|
| DNS no resuelve | `tcp_connect` (`getaddrinfo` != 0) | log + retorno -1 → backoff |
| connect() lento/colgado | `poll(POLLOUT)` con deadline 10 s | log `timeout de connect` → -1 → backoff |
| connect() rechazado | `getsockopt(SO_ERROR)` != 0 | log con `strerror` → -1 → backoff |
| CONNECT sin enviar (buffer lleno) | `write_all` → `poll(POLLOUT)` 5 s | -1 → backoff |
| CONNACK timeout/EOF/rechazo | `mqtt_read_connack` | log + -1 → backoff |
| Publicación fallida | `mqtt_publish` / `write_all` | log + break sesión → backoff |
| Broker cierra conexión | `recv() == 0` en `drain_incoming` | log + return -1 → backoff |
| Señal SIGINT/SIGTERM | `g_running == 0` | salida limpia con código 0 |

### 5.3 Connect no bloqueante (requisito I-001)

`SO_SNDTIMEO/SO_RCVTIMEO` **no** afectan a `connect()`, por lo que la implementación usa:

```c
fcntl(fd, F_SETFL, flags | O_NONBLOCK);      /* antes de connect() */
rc = connect(fd, addr, addrlen);
if (rc < 0 && errno == EINPROGRESS) {
    poll(&pfd /*POLLOUT*/, 1, CONNECT_TIMEOUT_S * 1000);
    if (pr > 0 && (pfd.revents & POLLOUT) && !(pfd.revents & (POLLERR|POLLHUP))) {
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &serr, &elen); /* resultado real */
        if (serr == 0) break;                                /* éxito */
    }
}
```

### 5.4 `write_all` robusto (requisito I-002)

```c
if (errno == EINTR) continue;
if (errno == EAGAIN || errno == EWOULDBLOCK) {
    poll(&pfd /*POLLOUT*/, 1, WRITE_TIMEOUT_S * 1000);   /* 5 s */
    if (pr > 0 && (pfd.revents & POLLOUT) && !(pfd.revents & (POLLERR|POLLHUP)))
        continue;                                          /* reintentar send */
    if (pr == 0) errno = ETIMEDOUT;
    return -1;                                             /* error/timeout real */
}
```

### 5.5 Seguridad y robustez adicional

- `snprintf` acotado para el tópico (`g_topic[256]`).
- Guards de tamaño antes de cada `memcpy` en CONNECT y PUBLISH.
- Sin `system()`/`popen()`; sin paths dinámicos.
- FDs cerrados en todas las rutas de error; sin fugas visibles (auditoría: 0 incidencias).
- Proceso de un solo hilo: sin condiciones de carrera sobre `g_running` (accedida solo desde el main loop y el handler de señales, declarada `volatile sig_atomic_t`).

---

## 6. Diagrama de flujo de la lógica principal

```
                        ┌──────────────┐
                        │  main()      │
                        └──────┬───────┘
                               ▼
                     init_signals(); armar g_topic
                               ▼
                     log INICIO (broker, topic, intervalo)
                               ▼
              ┌──────────── while (g_running) ────────────┐
              │                                            │
              │   tcp_connect() ── falla? ──► backoff_sleep() ──┐
              │        │ OK                                    │
              │        ▼                                       │
              │   mqtt_send_connect() ── falla? ──► close + backoff ──┐
              │        │ OK                                          │
              │        ▼                                             │
              │   mqtt_read_connack() ── rc != 0? ──► close + backoff ─┐
              │        │ rc == 0                                        │
              │        ▼                                               │
              │   log CONECTADO                                        │
              │        ▼                                               │
              │   publish inmediato (#1) ── falla? ──► close + backoff  │
              │        │ OK                                             │
              │        ▼                                               │
              │   ┌── while (g_running) ── bucle de sesión ──┐         │
              │   │   poll(POLLIN, timeout=próximo evento)   │         │
              │   │        ▼                                │         │
              │   │   si datos → drain_incoming() ── error? ►──┐       │
              │   │        ▼                                │  │       │
              │   │   si toca publish → mqtt_publish() ── error?─►──┐  │
              │   │        ▼                                │  │       │
              │   │   si inactivo >30s → PINGREQ ── error? ►──┘       │
              │   └──────────────┬─────────────────────────┘          │
              │                  │ break (error/EOF)                  │
              │                  ▼                                    │
              │            close(fd); log "sesión perdida"            │
              │            backoff_sleep() ◄──────────────────────────┘
              │                                   ▲
              └───────────────────────────────────┘
                               ▼
                   log FIN; return 0
```

---

## 7. Guía de modificación y extensión

### 7.1 Cambiar el intervalo de publicación / keepalive

Editar las macros en `lp104mqtt.c`:

```c
#define REPORT_INTERVAL_S   30   /* intervalo de publicación (s) */
#define MQTT_KEEPALIVE_S    60   /* keepalive declarado en CONNECT (s) */
#define PING_THRESHOLD_S    30   /* PINGREQ si inactivo > keepalive/2 */
#define CONNECT_TIMEOUT_S   10   /* timeout DNS/connect/CONNACK (s) */
#define BACKOFF_INITIAL_S   5    /* delay inicial de reconexión (s) */
#define BACKOFF_MAX_S       60   /* delay máximo de reconexión (s) */
```

Recompilar y reinstalar (sección 8).

### 7.2 Cambiar tópico o payload

- Tópico: `REPORT_TOPIC_SUFFIX "report/online"`; el tópico completo es `MQTT_CLIENT/` + sufijo (construido en `main`).
- Payload: `REPORT_PAYLOAD "true"` y `REPORT_PAYLOAD_LEN 4`. Si el payload cambia de longitud, actualizar ambas macros (o calcular con `strlen`).

### 7.3 Agregar más datos al reporte

Ejemplo: publicar también el uptime o la IP móvil. En el punto de publicación (`main`, ambas llamadas a `mqtt_publish`) construir un buffer:

```c
char payload[64];
snprintf(payload, sizeof(payload), "true|uptime=%ld", (long)uptime);
mqtt_publish(fd, g_topic, payload, strlen(payload));
```

### 7.4 Agregar suscripciones (QoS 0)

- Implementar envío de **SUBSCRIBE**: fixed header `0x82` + packet id (2 bytes) + topic + requested QoS.
- Implementar parseo de **SUBACK** (`0x90`) y de **PUBLISH entrante** (`0x30`) en `drain_incoming()` (actualmente descarta todo).
- Vigilar que el bucle `poll` procese ambos: datos entrantes y eventos de tiempo (ya lo hace).

### 7.5 Agregar QoS 1 (publicaciones confirmadas)

- Añadir packet id (2 bytes) a `mqtt_publish`.
- Esperar **PUBACK** (`0x40`) del broker en `drain_incoming()` (hoy se descarta).
- Manejar retransmisión si no llega PUBACK en un timeout.

### 7.6 Agregar TLS

Requiere librerías (mbedTLS/libcoap no incluidas en el router) o estáticas contra `libmbedtls`. Implicaría: wrap del fd con la sesión TLS, certificate verification, más RAM/Flash. Es la única mejora que rompe el requisito "sin librerías externas" — evaluar con `opkg` o SDK.

### 7.7 Migrar a archivo de configuración en runtime

Si se quiere cambiar credenciales sin recompilar: parsear `/etc/lp104mqtt.conf` (p. ej. `clave=valor` por línea) con `fopen`/`strtok` al arrancar, y usar las variables en `mqtt_send_connect()`. Mantener la misma estructura de datos. Requiere que el binario siga estático (sin `getenv` de libc dinámica — musl estático lo soporta igual).

### 7.8 Agregar logging a syslog en vez de stderr

Reemplazar `log_msg` por `openlog`/`syslog` (libc, sin dependencias) o dejar stderr y confiar en la redirección de procd (`procd_set_param stdout 1; stderr 1`) — opción actual, ya integrada.

---

## 8. Makefile y cross-compilación

### 8.1 Variables clave

```make
SDK_DIR ?= /home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64
TOOLCHAIN_DIR = $(SDK_DIR)/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl
CC    = $(TOOLCHAIN_DIR)/bin/mipsel-openwrt-linux-musl-gcc
STRIP = $(TOOLCHAIN_DIR)/bin/mipsel-openwrt-linux-musl-strip
CFLAGS  ?= -static -Os -Wall -Wextra
LDFLAGS ?= -static
export STAGING_DIR = $(SDK_DIR)/staging_dir

TARGET = lp104mqtt
ROUTER_IP   ?= 192.168.1.2
ROUTER_PORT ?= 22
ROUTER_DEST ?= /usr/local/bin/$(TARGET)   # ruta persistente (overlay rw)
```

### 8.2 Por qué `STAGING_DIR` es obligatorio

La toolchain de OpenWrt localiza su sysroot (headers de musl, libc) mediante `STAGING_DIR`. Sin la variable exportada, `gcc` falla al encontrar `crt1.o`/`libc.a`.

### 8.3 Targets

| Target | Qué hace |
|---|---|
| `all` (default) | Verifica que `$(CC)` existe y compila + **strip** (`STRIP` tras el link) |
| `check-toolchain` | Lista `$(TOOLCHAIN_DIR)/bin/` (diagnóstico) |
| `install` | `ssh mkdir -p /usr/local/bin` + `scp` del binario + `chmod +x` |
| `test` | Ejecuta `$(ROUTER_DEST)` en el router y muestra `exit=?` |
| `clean` | Borra `$(TARGET)` y `*.o` |

### 8.4 Detalles de instalación remota

```make
SSH_OPTS = -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $(ROUTER_PORT)
SCP_OPTS = -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P $(ROUTER_PORT)
SSH = $(if $(ROUTER_PASS),sshpass -p '$(ROUTER_PASS)' ssh,ssh)
SCP = $(if $(ROUTER_PASS),sshpass -p '$(ROUTER_PASS)' scp,scp)
```

- `ssh` usa `-p` (minúscula); `scp` usa `-P` (mayúscula) para el puerto.
- `ROUTER_PASS` se lee del entorno y se inyecta con `sshpass`; si está vacía se usa clave SSH.
- `ROUTER_DEST` → `/usr/local/bin/lp104mqtt`, alineado con `PROG` del init.d (requisito I-005). `/tmp` es volátil y **no** es destino de instalación.

### 8.5 Flujo de build completo

```bash
export STAGING_DIR=$SDK/staging_dir
export PATH=$PATH:$SDK/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin/
make                       # compila y strippea
file lp104mqtt             # verifica ELF MIPS estático
export ROUTER_PASS='...'
make install               # scp a /usr/local/bin/lp104mqtt
make test                  # ejecución remota
```

---

## 9. init.d / procd

El script `init.d/lp104mqtt` usa el framework `/etc/rc.common` de OpenWrt:

```sh
START=95                    # arranca tarde, después de red y firewall
STOP=10
USE_PROCD=1                 # supervisión procd

PROG=/usr/local/bin/lp104mqtt

start_service() {
    [ -x "$PROG" ] || { logger -t lp104mqtt "binario no encontrado..."; return 1; }
    procd_open_instance
    procd_set_param command "$PROG"
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_set_param respawn 3600 5 5
    procd_close_instance
}
```

| Parámetro | Significado |
|---|---|
| `stdout 1` / `stderr 1` | Redirige la salida del proceso al log del sistema (`logread`) |
| `respawn 3600 5 5` | Máx 5 reinicios en ventana de 3600 s; espera 5 s entre reinicios. Si el proceso muere 6 veces en la ventana, procd espera a que expire |

Instalación:

```bash
scp init.d/lp104mqtt root@192.168.1.2:/etc/init.d/
ssh root@192.168.1.2 "chmod +x /etc/init.d/lp104mqtt && /etc/init.d/lp104mqtt enable && /etc/init.d/lp104mqtt start"
```

---

## 10. Pruebas sugeridas al modificar el código

1. **Compilación limpia**: `make` sin warnings con `-Wall -Wextra`.
2. **Arquitectura**: `file lp104mqtt` → `ELF 32-bit LSB, MIPS, MIPS32 rel2, statically linked, stripped`.
3. **Publish inmediato + cada 30 s**: ejecución manual; verificar `[CONECTADO]` y `[PUBLICANDO]` en t=0, t=30, t=60...
4. **Reconexión**: cortar la red móvil (o detener el broker); verificar backoff 5→10→20→40→60 s y publicación inmediata al recuperar.
5. **CONNACK rechazado**: credenciales erróneas → mensaje legible (código 4/5) y reintento sin cuelgue.
6. **Señales**: `kill -TERM <pid>` → `[FIN]` y exit 0.
7. **Doble instancia**: dos procesos con el mismo client id → el broker desconecta a uno (verificar que el otro sigue publicando).
8. **Uptime/respawn**: matar el proceso supervisado → procd lo reinicia (< 5 s) y re-publica.

---

## 11. Referencias para desarrolladores

- [MQTT 3.1.1 specification (OASIS)](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)
- [OpenWrt — Initscripts / procd](https://openwrt.org/docs/techref/initscripts)
- [Teltonika — Software Development Kit](https://wiki.teltonika-networks.com/view/Software_Development_Kit)
- [Teltonika — GPL Code Center](https://wiki.teltonika-networks.com/view/GPL_Code_Center)
- [OpenWrt musl toolchain docs](https://openwrt.org/docs/guide-developer/toolchain)