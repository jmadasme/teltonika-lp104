# LP104 MQTT Teltonika V2 — Hoja de Datos y Guía de Usuario

---

## Tabla de contenidos

- [Resumen del producto](#resumen-del-producto)
- [Especificaciones técnicas](#especificaciones-técnicas)
- [Requisitos](#requisitos)
- [Guía de instalación](#guía-de-instalación)
- [Guía de despliegue](#guía-de-despliegue)
- [Configuración](#configuración)
- [Operación](#operación)
- [Monitoreo y logs](#monitoreo-y-logs)
- [Guía de solución de problemas](#guía-de-solución-de-problemas)
- [Mantenimiento](#mantenimiento)
- [Seguridad](#seguridad)

---

## Resumen del producto

**lp104mqttv2** es un servicio de telemetría MQTT para el router Teltonika RUT200. Recolecta periódicamente (cada 30 segundos) datos de salud del dispositivo — señal celular, usuarios WiFi, carga de CPU y temperatura del módem — y los publica vía MQTT hacia un broker remoto.

El cliente MQTT está implementado directamente en C sin dependencias de librerías externas, con enlace estático completo para máxima portabilidad dentro de la plataforma OpenWrt/RutOS.

---

## Especificaciones técnicas

### Hardware del router

| Componente | Especificación |
|---|---|
| **Modelo** | Teltonika RUT200 |
| **Serie** | RUT2xx |
| **Procesador** | MediaTek MT7628AN (MIPS24KEc, 580 MHz) |
| **RAM** | 128 MB DDR2 |
| **Flash** | 16 MB NOR |
| **Red** | WiFi 2.4 GHz 802.11n, 2x Ethernet 10/100, 4G LTE Cat 1 |
| **Antenas** | 2x LTE ( SMA), 1x WiFi ( RP-SMA) |
| **GPIO disponibles** | 2x entradas digitales, 2x salidas digitales (vía RutOS) |
| **UART** | Disponible para consola (115200 baud) |
| **Alimentación** | 9-30 VDC, poe 802.3af (puerto WAN) |

### Software

| Componente | Versión |
|---|---|
| **RutOS** | RUT2M_R_00.07.22.3 |
| **OpenWrt base** | 21.02.0 |
| **Kernel** | Linux 5.4.x (ramips) |
| **libc** | musl 1.2.x |
| **Compilador SDK** | GCC 8.4.0 |

### Binario lp104mqttv2

| Propiedad | Valor |
|---|---|
| **Formato** | ELF 32-bit LSB executable |
| **Arquitectura** | MIPS, MIPS32 rel2 |
| **Enlace** | Estático (`-static`) |
| **Tamaño típico** | ~95 KB |
| **Uso de RAM** | ~1.5 MB (residente) |
| **Uso de CPU** | Negligible (< 0.1% en idle) |
| **Uso de disco** | ~95 KB en `/usr/local/bin/` |
| **Dependencias** | Ninguna (estáticamente enlazado) |

### Métricas recolectadas

| Métrica | Fuente | Comando/Ruta | Frecuencia | Tipo | Unidad |
|---|---|---|---|---|---|
| Señal celular | Módem LTE | `gsmctl -q` | 30s | Entero | dBm |
| Usuarios WiFi | Interfaces AP | `/sys/class/net/*/wireless/assoclist` | 30s | Entero | cantidad |
| Uso de CPU | Kernel | `/proc/stat` | 30s | Flotante | % |
| Temperatura | Módem LTE | `gsmctl -c` | 30s | Flotante | °C (décimas) |
| Timestamp | Sistema | `time(NULL)` | 30s | Entero | epoch |

### Parámetros MQTT

| Parámetro | Valor |
|---|---|
| **Protocolo** | MQTT 3.1.1 |
| **QoS** | 0 (at most once) |
| **Retain** | No |
| **Keepalive** | 60 segundos |
| **Intervalo de publicación** | 30 segundos |
| **Timeout de conexión TCP** | 10 segundos |
| **Timeout de CONNACK** | 10 segundos |
| **Timeout de escritura** | 5 segundos |
| **Reconexión** | Automática con backoff exponencial (5s → 60s) |

### Tópicos MQTT

| Tópicos | Payload | Retain | Descripción |
|---|---|---|---|
| `{client_id}/report/online` | `"true"` | No | Indicador de dispositivo online |
| `{client_id}/report/status` | JSON | No | Telemetría completa (ver abajo) |

### Formato JSON del payload status

```json
{
  "signal": -75,      // int: RSSI en dBm
  "users": 3,         // int: clientes WiFi asociados
  "cpu": 12.5,        // float: uso de CPU en %
  "temp": 35.2,       // float: temperatura del módem en °C
  "ts": 1695000000    // int: timestamp epoch (segundos)
}
```

---

## Requisitos

### Para compilar

| Requisito | Detalle | Cómo obtener |
|---|---|---|
| **SDK OpenWrt 21.02.0** | ramips/mt76x8, gcc-8.4.0, musl | [OpenWrt Downloads](https://downloads.openwrt.org/releases/21.02.0/targets/ramips/mt76x8/) o [GPL Code Center Teltonika](https://wiki.teltonika-networks.com/view/GPL_Code_Center) |
| **Linux x86-64** | Para ejecutar el toolchain (cross-compilación) | Cualquier distro Linux moderna (Ubuntu 20.04+, Debian 11+, etc.) |
| **GNU Make** | Para usar el Makefile | `sudo apt install build-essential` |
| **sshpass** (opcional) | Para despliegue automatizado con contraseña | `sudo apt install sshpass` |

### Para desplegar

| Requisito | Detalle |
|---|---|
| **Acceso SSH** | `root@192.168.1.2` (usuario root, IP por defecto del RUT200) |
| **Conectividad** | El host de compilación debe poder hacer ping al router |
| **Credenciales MQTT** | Usuario/contraseña en `/etc/lp104mqttv2.conf` del router (plantilla: `lp104mqttv2.conf.example`) |

### Descarga del SDK

```bash
# Opción 1: Desde el sitio oficial de OpenWrt
cd /tmp/opencode/
wget https://downloads.openwrt.org/releases/21.02.0/targets/ramips/mt76x8/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64.tar.xz
tar xJf openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64.tar.xz

# Opción 2: Desde el GPL Code Center de Teltonika
# https://wiki.teltonika-networks.com/view/GPL_Code_Center
# Descargar el SDK correspondiente a RUT2M_R_00.07.22.3
```

---

## Guía de instalación

### Paso 1: Preparar el entorno de compilación

```bash
# Definir ruta del SDK
SDK_DIR=/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64

# Verificar que el toolchain existe
ls $SDK_DIR/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin/mipsel-openwrt-linux-musl-gcc

# Exportar variables (alternativa al Makefile)
export STAGING_DIR=$SDK_DIR/staging_dir
export PATH=$PATH:$STAGING_DIR/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin/
```

### Paso 2: Configurar `mqtt_config.h` (solo defaults NO sensibles)

```bash
cd /home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonikaV2/

# Copiar plantilla
cp mqtt_config.h.example mqtt_config.h

# Editar SOLO los defaults de conexión (NO incluye credenciales)
nano mqtt_config.h
```

Valores a configurar (defaults no sensibles):
- `MQTT_BROKER` — Hostname del broker MQTT (default: `www.smartage.cl`)
- `MQTT_PORT` — Puerto (default: 1886)
- `MQTT_CLIENT` — Client ID único (default: `6aaa978111a8892fd2927dcc`)

> **V2.1**: `mqtt_config.h` ya **NO contiene** `MQTT_USERNAME` ni `MQTT_PASSWORD`.
> Las credenciales se configuran en el router en `/etc/lp104mqttv2.conf` (Paso 2b).

> **IMPORTANTE**: Nunca versiones `mqtt_config.h`. Está excluido del repositorio.

### Paso 2b: Configurar credenciales en el router (`/etc/lp104mqttv2.conf`)

```bash
# Local: crear el .conf con credenciales reales (gitignored) o editar la plantilla
cp lp104mqttv2.conf.example lp104mqttv2.conf
nano lp104mqttv2.conf                 # user=..., pass=...

# Transferir al router (o dejar que `make deploy-v2` lo haga automáticamente)
scp lp104mqttv2.conf root@192.168.1.2:/etc/lp104mqttv2.conf
ssh root@192.168.1.2 "chmod 600 /etc/lp104mqttv2.conf"
```

Formato del archivo (placeholders — nunca credenciales reales en el repo):

```ini
user=YOUR_MQTT_USERNAME
pass=YOUR_MQTT_PASSWORD
#client_id=YOUR_MQTT_CLIENT_ID     # opcional; si se omite usa el default compilado
#broker=YOUR_BROKER_HOST           # opcional
#port=1886                         # opcional (1-65535)
```

### Paso 3: Compilar

```bash
make clean
make
```

### Paso 4: Verificar

```bash
file lp104mqttv2
# Salida esperada: ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1, statically linked, stripped
```

---

## Guía de despliegue

### Despliegue automático (Makefile)

```bash
# Compilar e instalar en el router
make install

# Esto ejecuta:
# 1. make (compilación)
# 2. scp del binario a /usr/local/bin/lp104mqttv2
# 3. scp del init.d script a /etc/init.d/lp104mqttv2
# 4. scp del banner a /etc/profile.d/99-lp104-banner.sh
# 5. Deshabilitación de V1
# 6. Habilitación e inicio de V2
```

> ⚠️ Si el binario instalado es el **V2.1** (credenciales desde archivo), asegúrate de que
> `/etc/lp104mqttv2.conf` exista con `user=`/`pass=` antes de iniciar el servicio.

### Despliegue V2.1: coexistencia sin ejecutar (`make deploy-v2`)

> **NUEVO en V2.1**: target para desplegar el binario V2.1 a la ruta temporal
> `/usr/local/home/root/lp104mqttv2_v2` **sin ejecutarlo**, y copiar el `.conf` al router.

```bash
make deploy-v2 ROUTER_IP=192.168.194.229
```

Qué hace:

1. Compila el binario (estático mipsel)
2. `scp` del binario → `/usr/local/home/root/lp104mqttv2_v2` (NO lo ejecuta)
3. Si existe `lp104mqttv2.conf` local (gitignored, credenciales reales) lo sube a
   `/etc/lp104mqttv2.conf`; si no, sube la plantilla `lp104mqttv2.conf.example` y avisa
4. En el router: `chmod +x` del binario y `chmod 600` del `.conf`
5. **NO toca init.d ni el servicio activo** (`/usr/local/bin/lp104mqttv2` sigue como estaba)

Ejecución posterior manual:

```bash
ssh root@192.168.1.2
/usr/local/home/root/lp104mqttv2_v2          # foreground (Ctrl+C para salir)
# o en background:
/usr/local/home/root/lp104mqttv2_v2 >/tmp/lp104mqttv2_v2.log 2>&1 &
```

Sustitución controlada (cuando se valide la v2):

```bash
cp /usr/local/home/root/lp104mqttv2_v2 /usr/local/bin/lp104mqttv2
chmod +x /usr/local/bin/lp104mqttv2
/etc/init.d/lp104mqttv2 restart
```

### Despliegue manual

#### Transferencia de archivos

```bash
ROUTER=192.168.1.2

# Binario
scp lp104mqttv2 root@${ROUTER}:/tmp/lp104mqttv2

# Init.d script
scp init.d/lp104mqttv2 root@${ROUTER}:/etc/init.d/lp104mqttv2

# Banner SSH
scp banner/99-lp104-banner.sh root@${ROUTER}:/etc/profile.d/99-lp104-banner.sh
```

#### Instalación en el router

```bash
ssh root@192.168.1.2

# Permisos y copia del binario
chmod +x /tmp/lp104mqttv2
cp /tmp/lp104mqttv2 /usr/local/bin/lp104mqttv2

# Permisos del init.d
chmod +x /etc/init.d/lp104mqttv2

# Permisos del banner
chmod +x /etc/profile.d/99-lp104-banner.sh

# Deshabilitar V1 (si existe)
/etc/init.d/lp104mqtt disable 2>/dev/null
/etc/init.d/lp104mqtt stop 2>/dev/null
rm -f /usr/local/bin/lp104mqtt

# Habilitar e iniciar V2
/etc/init.d/lp104mqttv2 enable
/etc/init.d/lp104mqttv2 start

# Verificar
/etc/init.d/lp104mqttv2 status
logread | grep lp104mqttv2 | tail -10
```

### Verificación post-instalación

```bash
# 1. Verificar que el servicio está corriendo
ps | grep lp104mqttv2
# Debe mostrar algo como:
#  1234 root      1480 S    /usr/local/bin/lp104mqttv2

# 2. Verificar logs
logread | grep lp104mqttv2 | tail -5
# Debe mostrar:
# [CONECTADO] sesión MQTT establecida con www.smartage.cl:1886
# [PUBLICANDO] {id}/report/status = {"signal":-72,"users":0,"cpu":12.3,"temp":36.0,"ts":1758000000}

# 3. Verificar que el binario es MIPS
file /usr/local/bin/lp104mqttv2
# Debe decir: ELF 32-bit LSB executable, MIPS, ...

# 4. Probar el banner SSH (desde otra terminal)
ssh root@192.168.1.2
# Debe mostrar el arte ASCII con la información de telemetría
```

---

## Configuración

### `/etc/lp104mqttv2.conf` (V2.1 — credenciales en runtime)

Las credenciales MQTT ya **NO** se compilan en el binario: se leen al arrancar desde
`/etc/lp104mqttv2.conf` (formato `clave=valor`, una por línea; se ignoran vacíos y `#`).

| Clave | Obligatoria | Descripción |
|---|---|---|
| `user` | Sí | Usuario MQTT del broker |
| `pass` | Sí | Password MQTT (nunca se loguea) |
| `client_id` | No | Client ID; si se omite usa el default compilado |
| `broker` | No | Hostname/IP del broker; si se omite usa el default compilado |
| `port` | No | Puerto (1–65535, validado); si se omite usa el default compilado |

Ejemplo con placeholders:

```ini
user=YOUR_MQTT_USERNAME
pass=YOUR_MQTT_PASSWORD
#client_id=YOUR_MQTT_CLIENT_ID
#broker=YOUR_BROKER_HOST
#port=1886
```

Comportamiento si falta el archivo: `WARNING` + `ERROR` visibles en logs; el binario arranca
pero el broker rechaza el CONNECT (fallo visible, no silencioso). La ruta se sobreescribe con la
variable de entorno `LP104MQTTV2_CONF`. Plantilla versionada: `lp104mqttv2.conf.example`.

### mqtt_config.h (solo defaults NO sensibles)

| Campo | Tipo | Requerido | Descripción |
|---|---|---|---|
| `MQTT_BROKER` | `const char*` | Default | Hostname del broker (default `www.smartage.cl`) |
| `MQTT_PORT` | `int` | Default | Puerto TCP (default `1886`) |
| `MQTT_CLIENT` | `const char*` | Default | Client ID MQTT (default `6aaa978111a8892fd2927dcc`) |

> **V2.1**: `MQTT_USERNAME` y `MQTT_PASSWORD` fueron **eliminados** de `mqtt_config.h`. Ahora se
> configuran en `/etc/lp104mqttv2.conf`.

### Constantes del programa (en lp104mqttv2.c)

| Constante | Valor por defecto | Descripción |
|---|---|---|
| `REPORT_INTERVAL_S` | 30 | Segundos entre cada publicación |
| `MQTT_KEEPALIVE_S` | 60 | Keepalive MQTT (segundos) |
| `PING_THRESHOLD_S` | 30 | Umbral para envío de PINGREQ |
| `CONNECT_TIMEOUT_S` | 10 | Timeout de conexión TCP y CONNACK |
| `BACKOFF_INITIAL_S` | 5 | Backoff inicial de reconexión |
| `BACKOFF_MAX_S` | 60 | Backoff máximo de reconexión |
| `WRITE_TIMEOUT_S` | 5 | Timeout de escritura en socket |

> **Nota**: Las constantes del programa requieren recompilación para modificarse.

### Variables del Makefile

| Variable | Valor por defecto | Descripción |
|---|---|---|
| `SDK_DIR` | `/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64` | Ruta al SDK |
| `ROUTER_IP` | `192.168.1.2` | IP del router destino |
| `ROUTER_PORT` | `22` | Puerto SSH |
| `ROUTER_USER` | `root` | Usuario SSH |
| `ROUTER_PASS` | (variable de entorno) | Contraseña SSH (via `sshpass`) |

---

## Operación

### Comandos de servicio

```bash
# Estado
/etc/init.d/lp104mqttv2 status

# Iniciar
/etc/init.d/lp104mqttv2 start

# Detener
/etc/init.d/lp104mqttv2 stop

# Reiniciar
/etc/init.d/lp104mqttv2 restart

# Habilitar al arranque
/etc/init.d/lp104mqttv2 enable

# Deshabilitar al arranque
/etc/init.d/lp104mqttv2 disable
```

### Comportamiento esperado

1. **Arranque**: El servicio se inicia al completar el arranque del router (START=95)
2. **Conexión**: Se conecta al broker MQTT en los primeros segundos
3. **Publicación**: Cada 30 segundos publica `report/online` y `report/status`
4. **Keepalive**: Envía PINGREQ cada 30 segundos para mantener la conexión
5. **Reconexión**: Si la conexión se pierde, reconecta automáticamente con backoff
6. **Apagado**: Responde a SIGTERM cerrando la conexión MQTT gracefully

### Respawn

El servicio está configurado con respawn de procd:
- **Máximo de respawn**: 3600 segundos
- **Delay inicial**: 5 segundos
- **Delay entre reintentos**: 5 segundos

Esto significa que si el proceso falla, procd lo reiniciará automáticamente después de 5 segundos, hasta un máximo de 3600 segundos de uptime sin crash.

---

## Monitoreo y logs

### Ver logs

```bash
# Todos los logs del servicio
logread | grep lp104mqttv2

# Últimos 20 logs
logread | grep lp104mqttv2 | tail -20

# Logs en tiempo real (seguir con Ctrl+C)
logread -f | grep lp104mqttv2

# Desde el host (sin SSH al router)
ssh root@192.168.1.2 "logread | grep lp104mqttv2 | tail -20"
```

### Niveles de log

| Nivel | Prefijo | Descripción |
|---|---|---|
| INICIO | `[INICIO]` | Arranque, configuración cargada, tópicos |
| CONECTADO | `[CONECTADO]` | Sesión MQTT establecida |
| PUBLICANDO | `[PUBLICANDO]` | Publicación de reportes |
| PING | `[PING]` | Keepalive/PINGREQ |
| WARNING | `[WARNING]` | Avisos (falta `.conf`, se usa default, línea inválida) |
| ERROR | `[ERROR]` | Errores (CONNACK rechazado, credenciales no configuradas, escritura) |
| RECONECTANDO | `[RECONECTANDO]` | Backoff de reconexión |
| FIN | `[FIN]` | Apagado limpio (SIGTERM/SIGINT) |

### Mensajes típicos

```
[2026-09-21 10:00:01] [INICIO] lp104mqttv2 — broker www.smartage.cl:1886, intervalos 30s
[2026-09-21 10:00:01] [INICIO] usuario MQTT: YOUR_MQTT_USERNAME
[2026-09-21 10:00:01] [INICIO] topic online: {id}/report/online
[2026-09-21 10:00:01] [INICIO] topic status: {id}/report/status
[2026-09-21 10:00:02] [CONECTADO] sesión MQTT establecida con www.smartage.cl:1886
[2026-09-21 10:00:02] [PUBLICANDO] {id}/report/online = "true"
[2026-09-21 10:00:02] [PUBLICANDO] {id}/report/status = {"signal":-72,"users":3,"cpu":12.5,"temp":35.2,"ts":1758000000}
[2026-09-21 10:00:32] [PING] keepalive enviado
[2026-09-21 10:05:00] [ERROR] Error de escritura, desconectando
[2026-09-21 10:05:00] [RECONECTANDO] en 5 s
```

Caso especial V2.1 — falta el `.conf` (inicio correcto pero broker rechaza):

```
[2026-09-21 10:00:01] [WARNING] no existe /etc/lp104mqttv2.conf (...); usando defaults compilados
[2026-09-21 10:00:01] [ERROR] credenciales MQTT no configuradas (falta /etc/lp104mqttv2.conf)
[2026-09-21 10:00:01] [INICIO] usuario MQTT: (vacío)
[2026-09-21 10:00:02] [ERROR] CONNACK rechazado (código 4): usuario o password incorrectos
[2026-09-21 10:00:02] [RECONECTANDO] en 5 s
```

---

## Guía de solución de problemas

### Problema 1: `Exec format error`

**Síntoma**:
```
-sh: /usr/local/bin/lp104mqttv2: Exec format error
```

**Causa**: El binario fue compilado para la arquitectura incorrecta (x86-64 en lugar de MIPS).

**Solución**:
```bash
# Verificar la arquitectura del binario
file lp104mqttv2

# Si dice "x86-64" o "x86", recompilar con el toolchain del SDK:
export STAGING_DIR=/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64/staging_dir
export PATH=$PATH:$STAGING_DIR/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin/
make clean && make

# Verificar que ahora es MIPS
file lp104mqttv2
# Debe decir: ELF 32-bit LSB executable, MIPS, MIPS32 rel2
```

### Problema 2: No conecta al broker MQTT

**Síntoma**: Logs muestran errores de conexión o timeout.

**Causas posibles**:
1. El broker MQTT no está accesible desde la red del router
2. El firewall bloquea conexiones salientes al puerto 1886
3. Credenciales incorrectas o ausentes en `/etc/lp104mqttv2.conf` (V2.1)

**Solución**:
```bash
# 1. Verificar conectividad al broker
ping www.smartage.cl

# 2. Verificar que el puerto está abierto
nc -zv www.smartage.cl 1886

# 3. Verificar firewall
iptables -L OUTPUT -n

# 4. Probar con mosquitto_pub desde el router
# (instalar: opkg install mosquitto-client-nossl)
mosquitto_pub -h www.smartage.cl -p 1886 \
  -u "tu_usuario" -P "tu_contraseña" \
  -t "test/topic" -m "hello"

# 5. Verificar el .conf en el router (V2.1)
cat /etc/lp104mqttv2.conf          # user=/pass= correctos
chmod 600 /etc/lp104mqttv2.conf
# Si falta el archivo, crearlo desde la plantilla:
mcedit /etc/lp104mqttv2.conf        # user=YOUR_MQTT_USERNAME / pass=YOUR_MQTT_PASSWORD

# 6. Verificar logs
logread | grep lp104mqttv2 | tail -10
# Si aparece "CONNACK rechazado (código 4)" → usuario/password incorrectos
```

### Problema 3: GPIO no responde

**Síntoma**: No se puede controlar GPIOs del router.

**Causa**: El programa lp104mqttv2 no utiliza GPIOs directamente (lee métricas via `gsmctl` y archivos `/sys`). Si necesitas GPIO, debes interactuar directamente con `/sys/class/gpio/`.

**Solución para GPIO general**:
```bash
# Exportar GPIO (ejemplo: GPIO4)
echo 4 > /sys/class/gpio/export

# Configurar como salida
echo out > /sys/class/gpio/gpio4/direction

# Escribir valor
echo 1 > /sys/class/gpio/gpio4/value

# Leer valor
cat /sys/class/gpio/gpio4/value
```

> **Nota**: Consulta el [Teltonika Wiki GPIO](https://wiki.teltonika-networks.com/view/RUT200_GPIO) para los mapeos específicos del RUT200.

### Problema 4: Watchdog reinicia el router

**Síntoma**: El router se reinicia periódicamente.

**Causa**: No es causado por lp104mqttv2 directamente. El respawn de procd puede causar un loop si el binario falla constantemente (crash en cada inicio).

**Solución**:
```bash
# Detener el servicio
/etc/init.d/lp104mqttv2 stop

# Verificar logs del crash
logread | grep lp104mqttv2

# Ejecutar manualmente para ver el error
/usr/local/bin/lp104mqttv2

# Si el binario tiene problemas de arquitectura, reinstalar el correcto
# (ver Problema 1)
```

### Problema 5: No se ve el banner SSH

**Síntoma**: Al hacer SSH no aparece el arte ASCII.

**Causas posibles**:
1. El archivo no está en `/etc/profile.d/`
2. No tiene permisos de ejecución
3. La sesión SSH no carga `/etc/profile`

**Solución**:
```bash
# Verificar que el archivo existe
ls -la /etc/profile.d/99-lp104-banner.sh

# Verificar permisos
chmod +x /etc/profile.d/99-lp104-banner.sh

# Probar manualmente
source /etc/profile.d/99-lp104-banner.sh

# Verificar que /etc/profile carga /etc/profile.d/
grep -r "profile.d" /etc/profile
```

### Problema 6: El servicio no persiste tras reinicio

**Síntoma**: Después de reiniciar el router, lp104mqttv2 no arranca.

**Causa**: El servicio no está habilitado en rc.d.

**Solución**:
```bash
# Habilitar el servicio
/etc/init.d/lp104mqttv2 enable

# Verificar que está en rc.d
ls /etc/rc.d/S95lp104mqttv2
# Debe existir

# Si no existe, recrear el symlink
ln -s /etc/init.d/lp104mqttv2 /etc/rc.d/S95lp104mqttv2
```

### Problema 7: Temperatura reportada parece incorrecta

**Síntoma**: La temperatura es 0°C o un valor muy alto/bajo inesperado.

**Causa**: La temperatura proviene del módem LTE (`gsmctl -c`), no del procesador. El MT7628AN no expone sensor de CPU. Si el módem no está disponible (SIM no insertada, etc.), `gsmctl -c` puede fallar.

**Solución**:
```bash
# Verificar que gsmctl funciona
gsmctl -c

# Verificar que la SIM está insertada y el módem está activo
gsmctl -s

# Si gsmctl falla, verificar el servicio modem
/etc/init.d/gsmd status
```

---

## Mantenimiento

### Actualizaciones

```bash
# 1. Detener el servicio
/etc/init.d/lp104mqttv2 stop

# 2. Transferir nuevo binario
scp lp104mqttv2 root@192.168.1.2:/usr/local/bin/lp104mqttv2

# 3. Reiniciar
/etc/init.d/lp104mqttv2 start
```

### Desinstalación

```bash
ssh root@192.168.1.2 << 'EOF'
# Detener y deshabilitar
/etc/init.d/lp104mqttv2 stop
/etc/init.d/lp104mqttv2 disable

# Eliminar archivos
rm -f /usr/local/bin/lp104mqttv2
rm -f /etc/init.d/lp104mqttv2
rm -f /etc/profile.d/99-lp104-banner.sh

echo "lp104mqttv2 desinstalado completamente"
EOF
```

### Logs rotativos

Los logs de procd se almacenan en el buffer circular del sistema (`/tmp/log/messages`). En routers con poca memoria, los logs antiguos se sobreescriben. Para conservar logs, configura un logger remoto (syslog-ng, rsyslog) o descarga periódicamente con `logread`.

---

## Seguridad

### Credenciales MQTT (V2.1)

- Las credenciales viven en `/etc/lp104mqttv2.conf` del router (formato `user=`/`pass=`)
- La plantilla versionada `lp104mqttv2.conf.example` solo contiene **placeholders**
- El `lp104mqttv2.conf` local con credenciales reales está excluido del repositorio (`.gitignore`)
- `mqtt_config.h` ya **NO contiene** usuario/contraseña — solo defaults no sensibles
- Usa `chmod 600` al `.conf` del router
- El binario **nunca loguea la password**
- El binario V2.1 **no tiene credenciales embebidas** — puede distribuirse sin exponer secretos

### Conexión SSH

- Acceso root al router (necesario para instalación y mantenimiento)
- Recomendable: usar SSH key en lugar de contraseña
- Considerar: deshabilitar password root y usar solo key-based auth

### MQTT

- La conexión MQTT es en texto plano (sin TLS) si el broker usa el puerto 1886 sin SSL
- Para producción, considera usar MQTT con TLS (puerto 8883)
- El cliente MQTT 3.1.1 envía usuario y contraseña en el paquete CONNECT

### Firewall

```bash
# Verificar reglas de firewall
iptables -L -n

# El programa solo necesita conexiones salientes TCP al puerto del broker
# No abre puertos entrantes
```
