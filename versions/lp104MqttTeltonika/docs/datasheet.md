# Datasheet — lp104MqttTeltonika

Documentación de usuario del proyecto **lp104MqttTeltonika**: cliente MQTT 3.1.1 en C puro para el router Teltonika RUT200 que replica el reporte periódico de un firmware ESP8266, publicando `true` en `{MQTT_CLIENT}/report/online` cada 30 segundos.

---

## 1. Especificaciones técnicas

### 1.1 Router objetivo

| Especificación | Valor |
|---|---|
| Modelo | Teltonika **RUT200** (serie RUT2M) |
| SoC | MediaTek MT7628AN (MIPS 24KEc, little-endian) |
| Arquitectura | `mipsel_24kc` |
| Target OpenWrt | `ramips/mt76x8` |
| RAM | 122 MB |
| Versión RutOS | `RUT2M_R_00.07.22.3` (OpenWrt 21.02.0, kernel 5.15.197, musl) |
| Acceso SSH | `root@192.168.1.2:22` |
| Filesystem | `/` squashfs read-only; `/usr/local` y `/etc` overlay rw; `/tmp` tmpfs volátil |

### 1.2 Conectividad

- **SIM móvil**: Entel (APN automático)
- **Módem**: Quectel EC200A
- **Interfaz móvil**: `mob1s1a1` (proto `wwan`, `usb0`)
- El programa usa la conectividad IP del router tal cual (sin configuración de red adicional).

### 1.3 Comunicación MQTT

| Parámetro | Valor |
|---|---|
| Broker | `www.smartage.cl` (puerto TCP 1886) |
| Protocolo | MQTT 3.1.1 (nivel 4) sobre TCP, sin TLS |
| Tópico | `{MQTT_CLIENT}/report/online` |
| Payload | `true` (4 bytes) |
| QoS | 0, sin retain, sin subscribe |
| Keepalive | 60 s |
| Publicación | inmediata al conectar + cada 30 s |
| Client id | el definido por `MQTT_CLIENT` en `mqtt_config.h` |

### 1.4 Aplicación

| Parámetro | Valor |
|---|---|
| Binario | `lp104mqtt` (~92 KB, ELF32 MIPS, estático, stripped) |
| Ubicación en router | `/usr/local/bin/lp104mqtt` |
| Servicio | `/etc/init.d/lp104mqtt` (procd, `START=95`, `STOP=10`, `respawn 3600 5 5`) |
| Librerías externas | **ninguna** (MQTT implementado a mano sobre sockets POSIX) |
| Hilos | 1 solo hilo con `poll()` |

---

## 2. Qué hace el programa

`lp104mqtt` es un demonio de un único propósito que emula al dispositivo ESP8266 original:

1. Resuelve el DNS del broker con `getaddrinfo()`.
2. Abre un socket TCP y conecta a `www.smartage.cl:1886` (connect no bloqueante con deadline de 10 s).
3. Envía el paquete **CONNECT** MQTT 3.1.1 (client id, username, password, clean session = 1, keepalive = 60 s).
4. Lee el **CONNACK** y verifica que el broker acepta la sesión (return code 0).
5. Publica **`true`** en `{MQTT_CLIENT}/report/online` inmediatamente y luego cada **30 segundos**.
6. Mantiene la sesión viva: las propias publicaciones (cada 30 s < keepalive 60 s) cumplen el keepalive; además envía **PINGREQ** si transcurren más de 30 s sin tráfico (defensa ante bloqueos).
7. Ante cualquier fallo (broker caído, red cortada, CONNACK rechazado, timeout) cierra el socket y **reconecta con backoff exponencial 5 s → 60 s**, sin límite de intentos.
8. Se detiene limpiamente con `SIGINT`/`SIGTERM` (código de salida 0).

**No se suscribe a ningún tópico** y **no usa TLS** (igual que el firmware original).

---

## 3. Requisitos y cómo obtenerlos

### 3.1 SDK de cross-compilación

- **SDK oficial**: OpenWrt **21.02.0** para **`ramips-mt76x8`**.
- Ruta de referencia en este proyecto:
  `/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64`
- **Toolchain**:
  `staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin/mipsel-openwrt-linux-musl-gcc` (gcc 8.4.0, **musl**).
- **Strip**: `mipsel-openwrt-linux-musl-strip`.

**Dónde obtenerlo**:

| Recurso | URL |
|---|---|
| SDK RutOS (wiki Teltonika) | https://wiki.teltonika-networks.com/view/Software_Development_Kit |
| GPL Code Center | https://wiki.teltonika-networks.com/view/GPL_Code_Center |
| OpenWrt downloads (SDK 21.02) | https://downloads.openwrt.org/releases/21.02.0/targets/ramips/mt76x8/ |

Seleccionar el SDK de la serie **RUT2M** (RutOS 7, arquitectura `mipsel_24kc`, libc musl).
> No usar la toolchain de otra serie: RUTX es ARM (`ipq40xx`, `arm-openwrt-linux-musulgnueabi-gcc`), Legacy/ar71xx usa uclibc. Un binario compilado con la arquitectura equivocada falla con `Exec format error` en el router.

### 3.2 Acceso SSH al router

- IP: `192.168.1.2`, puerto 22, usuario `root`.
- La password se pasa **solo** por variable de entorno `ROUTER_PASS` (el Makefile la usa con `sshpass`) o se usa autenticación por clave SSH.
- Verificación del entorno:

```sh
ssh root@192.168.1.2 "uname -m && cat /etc/openwrt_release"
```

### 3.3 Credenciales del broker

- Se compilan dentro del binario desde `mqtt_config.h` (NO versionado).
- La plantilla `mqtt_config.h.example` muestra el formato con placeholders.
- Para crear el header real:

```bash
cd /home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika
cp mqtt_config.h.example mqtt_config.h
# editar mqtt_config.h con el broker, usuario, password y client id reales
```

---

## 4. Guía de instalación y despliegue

### 4.1 Compilar

```bash
export STAGING_DIR=/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64/staging_dir
export PATH=$PATH:/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl/bin/

cd /home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika
make

file lp104mqtt
# ELF 32-bit LSB executable, MIPS, MIPS32 rel2, statically linked, stripped
```

### 4.2 Transferir al router

Opción A — con el Makefile (requiere `ROUTER_PASS` o clave SSH):

```bash
export ROUTER_PASS='...'
make install
```

Opción B — manual:

```bash
scp lp104mqtt root@192.168.1.2:/usr/local/bin/lp104mqtt
ssh root@192.168.1.2 "chmod +x /usr/local/bin/lp104mqtt"
```

### 4.3 Probar

```bash
make test
# o
ssh root@192.168.1.2 "/usr/local/bin/lp104mqtt"
```

Salida esperada (primeros segundos):

```text
[2026-09-16 10:00:00] [INICIO] lp104mqtt — broker www.smartage.cl:1886, topic 6aaa.../report/online, intervalo 30s
[2026-09-16 10:00:01] [CONECTADO] sesión MQTT establecida con www.smartage.cl:1886
[2026-09-16 10:00:01] [PUBLICANDO] 6aaa.../report/online = "true"
[2026-09-16 10:00:31] [PUBLICANDO] 6aaa.../report/online = "true"
```

### 4.4 Instalar como servicio (arranque automático)

```bash
scp init.d/lp104mqtt root@192.168.1.2:/etc/init.d/
ssh root@192.168.1.2 "chmod +x /etc/init.d/lp104mqtt && \
  /etc/init.d/lp104mqtt enable && \
  /etc/init.d/lp104mqtt start"
```

El servicio:

- Usa procd (`USE_PROCD=1`).
- Arranca al encender el router (`START=95`).
- Supervisa el proceso y lo reinicia si muere (`respawn 3600 5 5`).
- Ejecuta `/usr/local/bin/lp104mqtt` (`PROG`).
- Registra eventos con `logger -t lp104mqtt`.

### 4.5 Verificación

```bash
# ¿El proceso está corriendo?
ssh root@192.168.1.2 "pgrep -f lp104mqtt"

# Logs del programa (procd redirige stdout/stderr al log del sistema)
ssh root@192.168.1.2 "logread | grep lp104mqtt"

# Logs del servicio en vivo
ssh root@192.168.1.2 "logread -f | grep lp104mqtt"

# Verificar en el broker: el dispositivo MQTT_CLIENT debe recibir
# {MQTT_CLIENT}/report/online = true cada 30 s
```

---

## 5. Configuración

| Archivo | ¿Se versiona? | Descripción |
|---|---|---|
| `mqtt_config.h.example` | Sí | Plantilla con placeholders |
| `mqtt_config.h` | No (gitignored) | Credenciales reales compiladas en el binario |

Campos de `mqtt_config.h`:

| Macro | Ejemplo (placeholder) | Descripción |
|---|---|---|
| `MQTT_BROKER` | `"YOUR_BROKER_HOST"` | Hostname del broker MQTT |
| `MQTT_PORT` | `1886` | Puerto TCP del broker |
| `MQTT_USERNAME` | `"YOUR_USERNAME"` | Usuario MQTT |
| `MQTT_PASSWORD` | `"YOUR_PASSWORD"` | Password MQTT |
| `MQTT_CLIENT` | `"YOUR_CLIENT_ID"` | Client id y prefijo del tópico |

Constantes de comportamiento (en `lp104mqtt.c`): intervalo de publicación 30 s, keepalive 60 s, umbral de PINGREQ 30 s, timeout de conexión 10 s, backoff 5→60 s, tópico `report/online`, payload `true`.

---

## 6. Solución de problemas

### 6.1 `Exec format error` al ejecutar en el router

**Causa**: el binario se compiló con la arquitectura/toolchain equivocada (x86-64 del PC, ARM de RUTX, etc.).

**Solución**:

```bash
file lp104mqtt
```

Debe mostrar `ELF 32-bit LSB executable, MIPS, MIPS32 rel2 ... statically linked`. Si no, recompilar con el SDK `ramips-mt76x8` y la toolchain `mipsel-openwrt-linux-musl-gcc` (ver sección 3.1).

### 6.2 El programa no conecta con el broker MQTT

**Síntomas** en logs: `DNS: no se pudo resolver`, `TCP: timeout de connect`, `TCP: no se pudo conectar`.

**Causas y soluciones**:

1. **Sin red móvil**: comprobar que la SIM tiene datos y que el router tiene IP:
   ```sh
   ssh root@192.168.1.2 "ifstatus mob1s1a1 | grep -E 'up|address'"
   ```
2. **DNS roto**: probar `nslookup www.smartage.cl` desde el router.
3. **Puerto bloqueado**: el broker debe ser accesible en TCP 1886. Verificar con `nc -vz www.smartage.cl 1886` desde el router (si existe `nc`) o desde la LAN.
4. **Timeout de 10 s insuficiente** en redes móviles lentas: es un valor interno (`CONNECT_TIMEOUT_S`); si es recurrente, aumentarlo en `lp104mqtt.c` y recompilar.
5. **El broker está caído**: esperar, el backoff 5→60 s reintenta indefinidamente.

### 6.3 CONNACK rechazado (códigos 1–5)

| Código | Significado | Acción |
|---|---|---|
| 1 | Versión de protocolo no aceptada | El broker pide otra versión (raro; el firmware original usa 3.1.1) |
| 2 | Client identifier rechazado | Revisar `MQTT_CLIENT` |
| 3 | Servidor no disponible | Reintento automático; si persiste, avisar al administrador del broker |
| 4 | Usuario o password incorrectos | Revisar `MQTT_USERNAME` / `MQTT_PASSWORD` en `mqtt_config.h` y recompilar |
| 5 | No autorizado | Credenciales sin permiso sobre el tópico |

### 6.4 El servicio no arranca (init.d)

- Comprobar que el binario existe y es ejecutable:
  ```sh
  ssh root@192.168.1.2 "ls -l /usr/local/bin/lp104mqtt"
  ```
- Comprobar el registro del servicio:
  ```sh
  ssh root@192.168.1.2 "logread | grep lp104mqtt"
  ```
  → `binario no encontrado o no ejecutable` indica que falta `make install` o que `PROG` no coincide con la ruta real.
- Forzar el arranque: `/etc/init.d/lp104mqtt start`.

### 6.5 El router se reinicia o el proceso muere (watchdog/respawn)

- `respawn 3600 5 5` reinicia el proceso hasta 5 veces en 3600 s con 5 s de espera. Si el programa muere 6 veces en una hora, procd deja de reintentar hasta pasado el intervalo.
- Verificar con:
  ```sh
  ssh root@192.168.1.2 "logread | grep -E 'lp104mqtt|procd'"
  ```
- El programa en sí no usa watchdog de hardware; si el proceso muere por señal externa (p. ej. `kill`), el respawn de procd lo recupera.

### 6.6 Dos clientes con el mismo client id

Si hay dos instancias de `lp104mqtt` (servicio + prueba manual en ssh) con el mismo `MQTT_CLIENT`, el broker desconecta a una de ellas (sesión clean, una sola sesión activa por client id). **Solución**: detener el servicio antes de probar manualmente:

```sh
ssh root@192.168.1.2 "/etc/init.d/lp104mqtt stop"
# ... probar manualmente ...
ssh root@192.168.1.2 "/etc/init.d/lp104mqtt start"
```

### 6.7 Tras un reinicio del router el servicio no publica

Posibles causas:

1. El binario se instaló en `/tmp` (volátil) en vez de `/usr/local/bin`. Reinstalar con `make install`.
2. El init.d no está `enable`. Rehabilitar:
   ```sh
   ssh root@192.168.1.2 "/etc/init.d/lp104mqtt enable && /etc/init.d/lp104mqtt start"
   ```
3. La SIM aún no tiene red al arrancar el servicio: es normal; el programa reintenta con backoff hasta que la red aparece.

---

## 7. Seguridad y buenas prácticas

- **Nunca** versionar `mqtt_config.h` (contiene el usuario y password del broker en claro, y el binario estático puede extraerse con `strings`).
- **Nunca** escribir la password del router en archivos: solo `ROUTER_PASS` en el entorno.
- MQTT sin TLS: las credenciales viajan en claro por la red móvil/Internet. Evaluar TLS si el broker lo soporta.
- Mantener el router con firmware RutOS actualizado (la serie RUT2M usa OpenWrt 21.02 base).

---

## 8. Referencias

- [Teltonika — Software Development Kit](https://wiki.teltonika-networks.com/view/Software_Development_Kit)
- [Teltonika — GPL Code Center](https://wiki.teltonika-networks.com/view/GPL_Code_Center)
- [Teltonika RUT200 wiki](https://wiki.teltonika-networks.com/view/RUT200)
- [OpenWrt — Initscripts (procd)](https://openwrt.org/docs/techref/initscripts)
- [MQTT 3.1.1 — OASIS spec](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)