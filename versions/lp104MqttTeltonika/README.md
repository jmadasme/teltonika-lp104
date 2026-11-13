# lp104MqttTeltonika

> Cliente MQTT 3.1.1 mínimo en C puro (socket TCP + protocolo implementado a mano, **sin librerías externas**) para router **Teltonika RUT200**. Replica el reporte periódico que hacía un firmware ESP8266 (`lp104_secure.txt`), publicando `true` en el tópico `{MQTT_CLIENT}/report/online` cada 30 segundos, directamente desde el router con conectividad por SIM móvil.

**Estado**: auditado y aprobado (0 incidencias). Binario estático MIPS stripped de ~92 KB.

---

## Tabla de contenidos

1. [Descripción del proyecto](#1-descripción-del-proyecto)
2. [Router objetivo y especificaciones](#2-router-objetivo-y-especificaciones)
3. [Programa y principio de funcionamiento](#3-programa-y-principio-de-funcionamiento)
4. [Forma de operar](#4-forma-de-operar)
5. [Requisitos previos](#5-requisitos-previos)
6. [Cross-compilación](#6-cross-compilación)
7. [Transferencia al router](#7-transferencia-al-router)
8. [Instalación como servicio](#8-instalación-como-servicio)
9. [Tabla de periféricos](#9-tabla-de-periféricos)
10. [Configuración](#10-configuración)
11. [Protocolo de comunicación](#11-protocolo-de-comunicación)
12. [Ejemplos de uso realistas](#12-ejemplos-de-uso-realistas)
13. [Advertencias y precauciones](#13-advertencias-y-precauciones)
14. [Referencias oficiales de Teltonika](#14-referencias-oficiales-de-teltonika)

---

## 1. Descripción del proyecto

El proyecto sustituye una **ESP8266** (que publicaba `report/online = "true"` cada 30 s a un broker MQTT) por el **router Teltonika RUT200**, que ya dispone de salida a Internet mediante SIM móvil. De este modo el reporte no depende de un WiFi externo ni de un dispositivo adicional: el router es el emisor.

`lp104mqtt` es un **demonio de un solo propósito** que:

- Resuelve el DNS del broker (`getaddrinfo()`).
- Abre un socket TCP y conecta a `www.smartage.cl:1886`.
- Envía el paquete MQTT **CONNECT** 3.1.1 (client id, username, password, clean session = 1, keepalive = 60 s).
- Lee el **CONNACK** y verifica que el broker acepta la sesión (código 0).
- Publica **inmediatamente** al conectar y luego **cada 30 s** el payload `true` en `{MQTT_CLIENT}/report/online` (QoS 0, sin retain).
- Mantiene viva la sesión con PINGREQ defensivo y se **reconecta solo** ante caídas con backoff exponencial 5 s → 60 s.

**No se suscribe a ningún tópico.** No usa TLS (igual que el firmware original).

---

## 2. Router objetivo y especificaciones

| Especificación | Valor |
|---|---|
| Modelo | Teltonika **RUT200** (serie RUT2M) |
| SoC | MediaTek **MT7628AN** — MIPS 24KEc **little-endian** |
| Arquitectura | `mipsel_24kc` (ELF32 MIPS32 rel2, o32) |
| Target OpenWrt | `ramips/mt76x8` |
| RAM | 122 MB (~59 MB visibles en `/tmp` como tmpfs) |
| Flash | Overlay rw en `/usr/local` y `/etc` (squashfs root read-only) |
| Versión RutOS | `RUT2M_R_00.07.22.3` |
| OpenWrt / kernel | OpenWrt **21.02.0**, kernel 5.15.197 |
| libc | **musl** |
| Acceso SSH | `root@192.168.1.2` (puerto 22) |
| Conectividad | SIM Entel (APN automático), módem Quectel EC200A, interfaz móvil `mob1s1a1` (proto `wwan`, `usb0`) |
| Broker MQTT | `www.smartage.cl:1886` |

Verificación en el router:

```sh
uname -m                # → mips
cat /etc/openwrt_release
cat /etc/openwrt_version
```

---

## 3. Programa y principio de funcionamiento

### 3.1 Main loop

```
main()
 └─ init_signals()            # SIGINT/SIGTERM → salida limpia; SIGPIPE ignorado
 └─ bucle infinito (g_running):
     ├─ tcp_connect(broker, port)      # getaddrinfo + socket O_NONBLOCK +
     │                                 #   connect no bloqueante + poll(POLLOUT) 10 s
     ├─ mqtt_send_connect(fd)          # CONNECT MQTT 3.1.1 (variable header + payload)
     ├─ mqtt_read_connack(fd)          # 0 = aceptado; -1 = rechazado/timeout
     ├─ publish inmediato              # {MQTT_CLIENT}/report/online = "true"
     ├─ bucle de sesión (poll con timeout):
     │   ├─ poll(POLLIN, timeout hasta próximo publish/ping)
     │   ├─ drain_incoming(fd)         # PINGRESP/acks se descartan; EOF/error → reconectar
     │   ├─ si toca publicar → mqtt_publish()
     │   └─ si inactivo > keepalive/2 (30 s) → mqtt_send_pingreq()
     └─ ante cualquier fallo → close(fd) + backoff_sleep(5 s → 60 s)
```

### 3.2 Periféricos involucrados

El programa **no usa GPIO, UART, I2C ni ADC**. Solo red (socket TCP), resolución DNS, reloj del sistema y terminal para logs. Esto lo hace muy portable dentro de la familia RutOS.

### 3.3 Comunicación con el mundo exterior

| Elemento | Detalle |
|---|---|
| Protocolo | MQTT 3.1.1 sobre TCP, puerto 1886, **sin TLS** |
| Tópico | `{MQTT_CLIENT}/report/online` (el client id lo define `MQTT_CLIENT`) |
| Payload | literal `true` (4 bytes, sin `\0` en el paquete) |
| QoS | 0 (sin packet identifier, sin confirmación) |
| Retain | 0 |
| Keepalive | 60 s (publicación cada 30 s mantiene viva la sesión) |
| PINGREQ | defensivo si no hay tráfico > 30 s |

---

## 4. Forma de operar

**Resumen del ciclo de vida:**

1. **Compilar** el binario estático MIPS con el SDK (sección 6).
2. **Transferir** al router con `make install` o `scp` (sección 7).
3. **Probar** con `make test` o ejecución manual (sección 7).
4. **Instalar como servicio** con init.d/procd para arranque automático (sección 8).
5. **Verificar** llegada del reporte al broker y logs con `logread | grep lp104mqtt`.

Operación normal esperada:

- Al encender el router, el servicio arranca solo (START=95).
- Cuando el módem móvil da red, el programa resuelve DNS, conecta y publica `true` de inmediato.
- Cada 30 s publica de nuevo `true`. Si el broker cae o la red se corta, reintenta con backoff 5→60 s **sin límite de intentos**.
- El proceso es supervisado por procd (`respawn 3600 5 5`), que lo reinicia si muere.

---

## 5. Requisitos previos

### 5.1 SDK oficial de OpenWrt/RutOS

- **SDK**: OpenWrt **21.02.0** para target **`ramips-mt76x8`** (arquitectura `mipsel_24kc`).
- Ruta usada en este proyecto:
  `/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64`
- Descarga desde el **GPL Code Center / SDK de OpenWrt** correspondiente a la serie RUT2M (RutOS 7). Referencias en la sección 14.

### 5.2 Toolchain y arquitectura

| Elemento | Valor |
|---|---|
| Toolchain | `staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin/mipsel-openwrt-linux-musl-gcc` |
| gcc | 8.4.0 |
| libc | **musl** (RutOS 7). En la serie RutOS 7 usa musl; Legacy/ar71xx usa uclibc. |
| Enlace | **Estática** (`-static`) — el router no necesita librerías extra vía `opkg` |
| Strip | `mipsel-openwrt-linux-musl-strip` (binario final ~92 KB) |

> **Ojo con la arquitectura**: RUT2M es `mipsel_24kc` (MIPS little-endian). Compilar con ARM (RUTX, `ipq40xx`) o x86-64 produce `Exec format error` al ejecutar en el router.

### 5.3 Acceso SSH al router

- IP: `192.168.1.2` — usuario: `root` — puerto: 22.
- La password se pasa **solo** por variable de entorno `ROUTER_PASS` (nunca se escribe en archivos del repositorio). Alternativa: autenticación por clave SSH (dejar `ROUTER_PASS` vacío).

---

## 6. Cross-compilación

```bash
# 1) Exportar el staging dir del SDK (obligatorio para la toolchain de OpenWrt)
export STAGING_DIR=/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64/staging_dir
export PATH=$PATH:/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin/

# 2) Asegurarse de que existen las credenciales:
cp mqtt_config.h.example mqtt_config.h   # y editar mqtt_config.h con valores reales

# 3) Compilar
cd /home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika
make

# 4) Verificar arquitectura (debe ser MIPS, NUNCA x86-64)
file lp104mqtt
# Salida esperada:
# lp104mqtt: ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV),
#            statically linked, stripped
```

Si el SDK está en otra ruta:

```bash
make SDK_DIR=/ruta/al/sdk
# o
export SDK_DIR=/ruta/al/sdk && make
```

Comandos auxiliares del Makefile:

```bash
make check-toolchain   # verifica que la toolchain existe
make clean             # borra binario y .o
```

---

## 7. Transferencia al router

### 7.1 Con `make install` (recomendado)

```bash
export ROUTER_PASS='LA_PASSWORD_DEL_ROUTER'   # solo en el entorno, nunca en el repo
make install
# → scp lp104mqtt root@192.168.1.2:/usr/local/bin/lp104mqtt
# → chmod +x en el router
```

`ROUTER_DEST` por defecto es `/usr/local/bin/lp104mqtt` (overlay rw persistente), la misma ruta que usa el servicio init.d.

### 7.2 Manual (comandos exactos)

```bash
# Copiar el binario al router
scp lp104mqtt root@192.168.1.2:/usr/local/bin/lp104mqtt

# Dar permiso de ejecución
ssh root@192.168.1.2 "chmod +x /usr/local/bin/lp104mqtt"

# Ejecutar en primer plano (para probar; Ctrl-C termina)
ssh root@192.168.1.2 "/usr/local/bin/lp104mqtt"

# O hacerlo en segundo plano
ssh root@192.168.1.2 "/usr/local/bin/lp104mqtt &"
```

> **`make test`** ejecuta el binario remotamente y muestra `exit=?` al terminar. Para logs inmediatos use ejecución manual con `ssh`.

---

## 8. Instalación como servicio

El servicio usa **procd** (sistema de init de OpenWrt): arranca al encender el router y reinicia el proceso si muere (`respawn 3600 5 5`: máx 5 reinicios en 3600 s, espera 5 s entre intentos).

```bash
# 1) Copiar el script init.d al router
scp init.d/lp104mqtt root@192.168.1.2:/etc/init.d/

# 2) Activar y arrancar
ssh root@192.168.1.2 "chmod +x /etc/init.d/lp104mqtt && \
  /etc/init.d/lp104mqtt enable && \
  /etc/init.d/lp104mqtt start"

# 3) Verificar estado
ssh root@192.168.1.2 "/etc/init.d/lp104mqtt enabled && echo ENABLED"

# 4) Estado del proceso supervisado por procd
ssh root@192.168.1.2 "pgrep -f lp104mqtt"
```

Para detener/desinstalar:

```bash
ssh root@192.168.1.2 "/etc/init.d/lp104mqtt stop && /etc/init.d/lp104mqtt disable"
```

---

## 9. Tabla de periféricos

Este proyecto **no usa GPIO, UART, I2C ni ADC**. Los únicos recursos del sistema involucrados:

| Recurso | Ruta / API | Uso | Notas |
|---|---|---|---|
| Red (socket TCP) | `socket()`, `connect()`, `send()`, `recv()`, `poll()` | Conexión con el broker MQTT | Nada que montar; usa la red móvil `mob1s1a1` |
| Resolución DNS | `getaddrinfo()` | Resolver `www.smartage.cl` | Usa el resolver del router (con SIM da Internet) |
| Reloj | `time()` para intervalos y timestamps | Intervalos de 30 s, backoff, logs | No requiere NTP para funcionar |
| Terminal/logs | `stdout`/`stderr` + `logger` vía procd | Log del programa | visible con `logread` |
| Filesystem persistente | `/usr/local/bin` (overlay rw) | Binario instalado | `/` es squashfs read-only; `/tmp` es volátil |

---

## 10. Configuración

Las credenciales se **compilan dentro del binario** desde un header **no versionado**: `mqtt_config.h`. No hay parsing de archivos en runtime (el binario es estático y autónomo).

### 10.1 Archivos

| Archivo | Versionado | Propósito |
|---|---|---|
| `mqtt_config.h.example` | ✅ Sí | Plantilla con placeholders |
| `mqtt_config.h` | ❌ No (gitignored) | Credenciales reales (copiar de la plantilla y rellenar) |

### 10.2 Formato de `mqtt_config.h`

```c
/*
 * mqtt_config.h — Credenciales MQTT reales (NO VERSIONAR / gitignored)
 */
#ifndef MQTT_CONFIG_H
#define MQTT_CONFIG_H

#define MQTT_BROKER   "YOUR_BROKER_HOST"   /* ej. www.smartage.cl */
#define MQTT_PORT     1886                 /* puerto TCP del broker */
#define MQTT_USERNAME "YOUR_USERNAME"      /* usuario del broker   */
#define MQTT_PASSWORD "YOUR_PASSWORD"      /* password del broker  */
#define MQTT_CLIENT   "YOUR_CLIENT_ID"     /* client id / prefijo del tópico */

#endif /* MQTT_CONFIG_H */
```

### 10.3 Creación

```bash
cp mqtt_config.h.example mqtt_config.h
# editar mqtt_config.h con los valores reales del broker
make
```

### 10.4 Constantes de comportamiento (en `lp104mqtt.c`, no requieren edición)

| Constante | Valor | Descripción |
|---|---|---|
| `REPORT_INTERVAL_S` | 30 | intervalo de publicación (s) |
| `MQTT_KEEPALIVE_S` | 60 | keepalive declarado en CONNECT (s) |
| `PING_THRESHOLD_S` | 30 | PINGREQ si inactivo > keepalive/2 (s) |
| `CONNECT_TIMEOUT_S` | 10 | timeout DNS/connect/CONNACK (s) |
| `BACKOFF_INITIAL_S` | 5 | delay inicial de reconexión (s) |
| `BACKOFF_MAX_S` | 60 | delay máximo de reconexión (s) |
| `REPORT_TOPIC_SUFFIX` | `report/online` | sub-tópico (se antepone `MQTT_CLIENT/`) |
| `REPORT_PAYLOAD` | `true` | payload publicado |

> **Importante**: nunca versiones `mqtt_config.h`. Está en `.gitignore` junto con `config.h`, `*.config`, `.env`, `*.key`, etc.

---

## 11. Protocolo de comunicación

### 11.1 Diagrama de la sesión

```
lp104mqtt (RUT200)                    Broker MQTT (www.smartage.cl:1886)
     │  TCP connect (puerto 1886)              │
     │─────────────────────────────────────────>│
     │  CONNECT  (client id, user, pass,       │
     │           clean session=1, keepalive=60)│
     │─────────────────────────────────────────>│
     │  CONNACK  (0x20, return code 0 = OK)     │
     │<─────────────────────────────────────────│
     │  PUBLISH  "true" -> {CLIENT}/report/online
     │─────────────────────────────────────────>│
     │  ... cada 30 s ... PUBLISH "true"        │
     │─────────────────────────────────────────>│
     │  (si inactivo > 30 s) PINGREQ            │
     │─────────────────────────────────────────>│
     │  PINGRESP                                 │
     │<─────────────────────────────────────────│
     │  ante fallo/EOF → reconexión con backoff  │
```

### 11.2 Paquetes MQTT emitidos (formato en bytes)

| Paquete | Fixed header | Resto | Notas |
|---|---|---|---|
| CONNECT | `0x10` + remaining length | Nombre protocolo `"MQTT"` (0x00 0x04 0x4D 0x51 0x54 0x54), nivel `0x04`, flags `0xC2` (clean session + username + password), keepalive big-endian `0x00 0x3C` (60 s), luego client id / user / pass como cadenas UTF-8 con prefijo de longitud (2 bytes big-endian) | Variable header completo = 10 bytes |
| CONNACK (entrante) | `0x20` (2 bytes: `0x20 0x02`) | session present + return code (2 bytes) | Código 0 = aceptado; 1–5 traducidos a texto legible |
| PUBLISH | `0x30` + remaining length | topic len (2 bytes BE) + topic + payload | QoS 0, sin packet id, sin retain |
| PINGREQ | `0xC0 0x00` | — | 2 bytes exactos |
| PINGRESP (entrante) | `0xD0 0x00` | — | se descarta en `drain_incoming()` |

### 11.3 Tópico y payload

- Tópico completo: `MQTT_CLIENT + "/report/online"` (ej. `6aaa978111a8892fd2927dcc/report/online`).
- Payload: literal `"true"` (4 bytes; el `\0` terminador de C **no** se envía).
- QoS 0, retain 0. Sin suscripciones, sin LWT.

---

## 12. Ejemplos de uso realistas

### 12.1 Ejecución manual (probando conectividad)

```text
$ ssh root@192.168.1.2 "/usr/local/bin/lp104mqtt"
[2026-09-16 10:00:00] [INICIO] lp104mqtt — broker www.smartage.cl:1886, topic 6aaa...7dcc/report/online, intervalo 30s
[2026-09-16 10:00:01] [CONECTADO] sesión MQTT establecida con www.smartage.cl:1886
[2026-09-16 10:00:01] [PUBLICANDO] 6aaa...7dcc/report/online = "true"
[2026-09-16 10:00:31] [PUBLICANDO] 6aaa...7dcc/report/online = "true"
[2026-09-16 10:01:01] [PUBLICANDO] 6aaa...7dcc/report/online = "true"
...
^C
[2026-09-16 10:02:30] [FIN] lp104mqtt detenido limpiamente
```

### 12.2 Caída de red y reconexión

```text
[2026-09-16 10:05:00] [PUBLICANDO] 6aaa...7dcc/report/online = "true"
[2026-09-16 10:05:03] [ERROR] socket: conexión cerrada por el broker
[2026-09-16 10:05:03] [RECONECTANDO] en 5 s
[2026-09-16 10:05:08] [ERROR] TCP: no se pudo conectar a www.smartage.cl:1886
[2026-09-16 10:05:08] [RECONECTANDO] en 10 s
[2026-09-16 10:05:18] [ERROR] TCP: timeout de connect a www.smartage.cl:1886 (10s)
[2026-09-16 10:05:18] [RECONECTANDO] en 20 s
...                       ← la red vuelve ...
[2026-09-16 10:08:00] [CONECTADO] sesión MQTT establecida con www.smartage.cl:1886
[2026-09-16 10:08:00] [PUBLICANDO] 6aaa...7dcc/report/online = "true"   ← publicación inmediata tras reconectar
```

### 12.3 Credenciales incorrectas

```text
[2026-09-16 10:10:00] [CONNECT] fallo al enviar paquete   (o bien)
[2026-09-16 10:10:01] [CONNACK rechazado (código 4)] usuario o password incorrectos
[2026-09-16 10:10:01] [RECONECTANDO] en 5 s
```

### 12.4 Verificación en el broker / plataforma

En la plataforma/web del usuario, el dispositivo cuyo ID coincide con `MQTT_CLIENT` debe aparecer **online** y recibir `{MQTT_CLIENT}/report/online = true` cada 30 s. Para pruebas rápidas con un broker local:

```bash
# En una máquina con mosquitto (herramienta de desarrollo, no requerida en el router)
mosquitto_sub -h localhost -p 1886 -t '#' -v
```

### 12.5 Verificación de logs del servicio (procd)

```bash
ssh root@192.168.1.2 "logread | grep lp104mqtt"
# o en vivo:
ssh root@192.168.1.2 "logread -f | grep lp104mqtt"
```

---

## 13. Advertencias y precauciones

1. **Arquitectura correcta**: el binario debe ser `ELF 32-bit LSB ... MIPS ... statically linked`. Si compilas con la toolchain equivocada (ARM para RUTX, o x86-64), el router devolverá **`Exec format error`** al ejecutarlo. Verifica siempre con `file lp104mqtt`.
2. **Credenciales nunca versionadas**: `mqtt_config.h` contiene el usuario/password del broker. Está en `.gitignore`; **solo** se versiona `mqtt_config.h.example` con placeholders. No subas el binario a un repo público sin recompilarlo con placeholders (las credenciales quedan incrustadas en el binario estático, extraíbles con `strings`).
3. **`/tmp` es volátil**: no instales el servicio desde `/tmp`; los reinicios del router lo borran. La ruta persistente es `/usr/local/bin/lp104mqtt` (overlay rw) y el init.d va en `/etc/init.d/`.
4. **Un solo cliente con el mismo client id**: MQTT con clean session permite solo **una** sesión activa por client id. Si ejecutas `lp104mqtt` a la vez desde dos procesos (por ejemplo, uno manual en ssh y otro como servicio), el segundo desconectará al primero. Detén el servicio antes de probar manualmente, o viceversa.
5. **Reinicios y watchdog**: el servicio usa `respawn 3600 5 5` de procd. Si el binario no existe o no es ejecutable, el servicio no arranca (el init.d lo registra con `logger`). Tras `make install`, asegúrate de que el binario está en la misma ruta que `PROG` del init.d.
6. **Sin TLS**: las credenciales viajan en claro sobre TCP (igual que en el firmware original). Si el broker lo soporta, una mejora futura sería añadir TLS (requeriría librerías).
7. **Reloj del sistema**: los intervalos usan `time()` (wall clock); un ajuste de NTP puede adelantar/retrasar una publicación puntual. No afecta a la funcionalidad.
8. **Password del router**: pásala solo por `ROUTER_PASS` en el entorno (el Makefile la usa con `sshpass`, nunca la escribe en archivos).

---

## 14. Referencias oficiales de Teltonika

- **SDK / Software Development Kit (RutOS)**: https://wiki.teltonika-networks.com/view/Software_Development_Kit
- **GPL Code Center (código fuente y SDKs por serie)**: https://wiki.teltonika-networks.com/view/GPL_Code_Center
- **Manual de RutOS (instrucciones de sistema, ssh, servicios)**: https://wiki.teltonika-networks.com/view/RUT2M_Manual
- **RUT200 wiki**: https://wiki.teltonika-networks.com/view/RUT200
- **Documentación OpenWrt (21.02, procd, init.d)**: https://openwrt.org/docs/techref/initscripts
- **Especificación MQTT 3.1.1 (OASIS)**: https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html

---

## Estructura de archivos

```
lp104MqttTeltonika/
├── README.md               ← este archivo
├── PLAN.md                 ← plan de implementación (no versionado)
├── AUDIT_REPORT.md         ← auditoría ciclo 1 (no versionado)
├── lp104mqtt.c             ← código fuente (MQTT 3.1.1 a mano, C puro)
├── mqtt_config.h           ← credenciales reales (NO versionar)
├── mqtt_config.h.example   ← plantilla con placeholders (versionar)
├── Makefile                ← cross-compile estático + install/test por scp
├── .gitignore
├── init.d/lp104mqtt        ← servicio procd (START=95, respawn 3600 5 5)
├── lp104mqtt               ← binario estático MIPS (no versionar)
└── docs/
    ├── datasheet.md        ← documentación de usuario
    └── developer.md        ← documentación de desarrollo
```