# CATÁLOGO DE LABORATORIOS — FICHAS TÉCNICAS

## Las 15 mini-aplicaciones del curso, una por una

**Cada ficha es el enunciado completo del laboratorio.** Contiene: qué hace, de dónde saca los
datos, cómo los procesa, qué umbrales usa, qué alerta emite, cómo se prueba en vivo, y qué
concepto técnico enseña.

---

## ÍNDICE

**ACCIÓN (Módulo 5)**

| # | Laboratorio | Producto rutosapp | Página |
|---|---|---|---|
| 1 | Port Scan Detector | Port Scan Detector v1.0.0 | [↓](#a1) |
| 2 | ICMP Flood Detector | ICMP Flood Detector v1.0.0 | [↓](#a2) |
| 3 | Web Attack Detector | Web Attack Detector v1.0.0 | [↓](#a3) |
| 4 | SSH Brute-Force Protector | SSH Brute Force Protector v1.0.0 | [↓](#a4) |
| 5 | DoS Connection Limiter | DoS Connection Limiter v1.0.0 | [↓](#a5) |
| 6 | WiFi Channel Optimizer | WiFi Channel Optimizer v2.2.0 | [↓](#a6) |

**NOTIFICACIÓN (Módulo 6)**

| # | Laboratorio | Producto rutosapp | Página |
|---|---|---|---|
| 7 | CPU Monitor Alert | CPU Monitor Alert v1.2.0 | [↓](#n1) |
| 8 | RAM Usage Notifier | RAM Usage Notifier v1.4.2 | [↓](#n2) |
| 9 | Cellular Signal Monitor | Cellular Signal Monitor v1.0.0 | [↓](#n3) |
| 10 | Cable Connection Alert | Cable Connection Alert v1.0.5 | [↓](#n4) |
| 11 | WiFi New User Alert | WiFi New User Alert v1.3.1 | [↓](#n5) |
| 12 | Login Attempt Monitor | Login Attempt Monitor v2.0.1 | [↓](#n6) |
| 13 | Disk Usage Alert | Disk Usage Alert v1.1.3 | [↓](#n7) |
| 14 | Temperature Monitor Pro | Temperature Monitor Pro v1.5.0 | [↓](#n8) |
| 15 | System File Monitor | System File Monitor v1.0.0 | [↓](#n9) |

---
---

# SECCIÓN ACCIÓN (Módulo 5)

> **Contexto común de los labs A1–A5.** Los cinco son detectores de tráfico de red.
> Todos comparten la misma fuente de verdad: **`/proc/net/nf_conntrack`**, la tabla de
> conexiones que el kernel mantiene en memoria.
>
> ```
> /proc/net/nf_conntrack
> tcp 6 431999 ESTABLISHED src=192.168.1.50 dst=192.168.1.1 sport=54321 dport=80 src=192.168.1.1 dst=192.168.1.50 sport=80 dport=54321 [ASSURED] mark=0 ...
> ```
>
> Campos relevantes: `src=` (IP origen), `dport=` (puerto destino), `SYN_SENT` (conexión
> seulement iniciada, típico de escaneo).
>
> El agente `implementador-teltonika` entrega `labcore_netmon.c`, que ya parsea esto de forma
> segura. **El alumno no parte de cero en la parte difícil.**

---
---

<a name="a1"></a1>

# LAB 5.1 · PORT SCAN DETECTOR

**Producto de referencia:** Port Scan Detector · **Categoría:** Acción · **Versión:** v1.0.0

---

## Qué hace

Detecta actividad de escaneo de puertos sospechosa y alerta al administrador cuando se supera
el umbral configurado. Monitorea paquetes TCP SYN para identificar potenciales intentos de
reconocimiento de red.

## Fuente de datos

| Recurso | Ruta / comando | Qué se extrae |
|---|---|---|
| Conexiones del kernel | `/proc/net/nf_conntrack` | `src=`, `dport=`, estado `SYN_SENT` |

## Algoritmo

**Sliding window** (ventana deslizante) sobre los puertos destino contacted por IP origen:

```
POR CADA ciclo de muestreo (default: 1 s):

  1. Leer /proc/net/nf_conntrack línea a línea
  2. Filtrar: estado == SYN_SENT Y protocolo == tcp
  3. Extraer tuplas (ip_origen, puerto_destino)
  4. Para cada ip_origen:
       - eliminar del set los puertos con timestamp > ahora - window_s
       - agregar el puerto actual con timestamp = ahora
       - si |set| >= threshold_ports:
            DISPARAR ALERTA con la lista de puertos
```

**Complejidad:** O(n) sobre el número de líneas de conntrack (típicamente < 1000).
**Memoria:** un set de puertos por IP origen, acotado por `max_tracked_ips`.

## Configuración (`.conf`)

```ini
threshold_ports  = 10      # puertos distintos para alertar
window_seconds   = 2       # ventana de tiempo
sample_interval  = 1       # cada cuánto muestrear
whitelist_ips    = 10.0.0.0/8,192.168.1.0/24   # IPs que no alertan
alert_topic      = teltonika/alerts/port-scan
alert_cooldown   = 60      # no repetir la misma alerta antes de 60 s
```

## Alertas emitidas

```json
{
  "app": "port-scan-detector",
  "level": "WARNING",
  "src_ip": "192.168.1.77",
  "ports_touched": 24,
  "window_s": 2,
  "threshold": 10,
  "sample_ports": [21,22,23,25,53,80,110,143,443,445,993,1433,3306,3389,5432,5900,8080,8443],
  "ts": 1758000000
}
```

Niveles: `INFO` (5–9 puertos) · `WARNING` (≥10) · `CRITICAL` (≥50)

## Prueba en laboratorio

```bash
# Desde el PC de ataque (¡NO desde el router!)
nmap -sS -p- --min-rate 5000 192.168.1.1

# Verificar la alerta
mosquitto_sub -h broker -t 'teltonika/alerts/port-scan' -v
logread | grep port-scan-detector
```

**Resultado esperado:** alerta con la IP del atacante y la lista de puertos.
**Prueba negativa:** `nmap -sS -p 22,80,443` (3 puertos) → NO debe alertar.

## Qué enseña

- Sliding window: contar eventos en una ventana de tiempo, no acumular para siempre
- Conjunto de valores únicos (no hay `hash_set` en C: usar bitmap o array + dedupe)
- Parseo de formatos de kernel (`/proc/net/nf_conntrack` no es JSON, es posicional)
- Whitelist y cooldown: cómo NO generar ruido

---
---

<a name="a2"></a2>

# LAB 5.2 · ICMP FLOOD DETECTOR

**Producto de referencia:** ICMP Flood Detector · **Categoría:** Acción · **Versión:** v1.0.0

---

## Qué hace

Detecta y alerta sobre ataques ICMP ping flood excesivos. Rastrea solicitudes de ping
excesivas desde fuentes individuales para proteger la infraestructura de ataques DoS.

## Fuente de datos

| Recurso | Qué se extrae |
|---|---|
| `/proc/net/nf_conntrack` | entradas con protocolo `icmp`, `src=`, estado |
| `iptables` (opcional, instalado en deploy) | regla con `-m limit` paraAccounting eficiente |

## Algoritmo

```
POR CADA ciclo (default: 1 s):
  1. Contar entradas icmp con src=ip_origen en la ventana deslizante
  2. Si count >= threshold_pings EN window_seconds:
        ALERTA con ip_origen y tasa (pings/segundo)
```

**Mejora de diseño:** conntrack solo cuenta ICMP **en curso**. Para flood de paquetes rapido,
la vía robusta es una regla de iptables que cuente y loguee:

```sh
iptables -I INPUT -p icmp --icmp-type echo-request -j LOG --log-prefix "ICMPFLOOD "
```

El alumno debe **implementar ambas vías** y comparar cuál detecta mejor. Este es el ejercicio
de pensamiento crítico del módulo.

## Configuración

```ini
threshold_pings  = 20      # pings para alertar
window_seconds   = 2
use_iptables     = 1       # instalar regla LOG en deploy
log_prefix       = ICMPFLOOD
```

## Prueba en laboratorio

```bash
hping3 -c 200 -d 10 192.168.1.1        # flood
ping -c 100 -i 0.1 192.168.1.1        # lento: NO debe alertar
```

## Qué enseña

- **Tasa** vs **cantidad absoluta**: 20 pings en 2 s es ataque; 20 pings en 1 hora es normal
- Doble fuente de verdad y cuándo usar cada una
- iptables como capa de contabilidad (y por qué la app NO debe ser la única defensa)

---
---

<a name="a3"></a name>

# LAB 5.3 · WEB ATTACK DETECTOR

**Producto de referencia:** Web Attack Detector · **Categoría:** Acción · **Versión:** v1.0.0

---

## Qué hace

Detecta ataques web rápidos y patrones de tráfico HTTP sospechosos. Monitorea paquetes TCP SYN
al puerto 80 e identifica intentos de conexión rápidos que pueden indicar ataques automatizados
o escaneo de vulnerabilidades.

## Fuente de datos

`/proc/net/nf_conntrack` filtrando `dport=80` (y opcionalmente `dport=443`, `8080`).

## Algoritmo

**Detección de ráfaga** (burst):

```
POR CADA ciclo (1 s):
  1. Extraer (ip, marca de tiempo) de cada SYN hacia puertos web
  2. Para cada IP, contar SYNs en los últimos window_seconds
  3. SI count >= threshold_syncs Y variación > burst_factor:
        ALERTA (ataque en ráfaga)
```

**Detección de burst** (más precisa que un simple conteo): medir la **varianza** de los
intervalos entre paquetes. Un escáner automatizado dispara a intervalos regulares; un humano
hace pausas irregulares.

```
intervalos = [t2-t1, t3-t2, t4-t3, ...]
stddev     = desvío estándar de intervalos
SI count >= threshold Y stddev < burst_factor:
    ataque automatizado
```

## Configuración

```ini
threshold_syncs   = 10     # SYNs en la ventana
window_seconds    = 2
web_ports         = 80,443,8080
burst_factor      = 0.3    # desviación estándar máxima (s) = "automático"
```

## Prueba en laboratorio

```bash
# ATAQUE (ráfaga rápida)
for i in $(seq 1 30); do nc -z -w1 192.168.1.1 80 & done

# NAVEGACIÓN NORMAL (no debe alertar)
for i in $(seq 1 10); do curl -s http://192.168.1.1/ -o /dev/null; sleep 3; done
```

## Qué enseña

- **Detección de burst por varianza**: un detector de máquina, no un simple contador
- La diferencia entre "muchas conexiones" y "conexiones automatizadas"
- **Falsos positivos**: un monitor de uptime legítimo dispara esta alarma → whitelist es
  obligatoria, no opcional

---
---

<a name="a4"></a>

# LAB 5.4 · SSH BRUTE-FORCE PROTECTOR

**Producto de referencia:** SSH Brute Force Protector · **Categoría:** Acción · **Versión:** v1.0.0

---

## Qué hace

Detecta y previene intentos de fuerza bruta contra el puerto SSH (22). Monitorea intentos de
conexión rápidos desde una misma fuente y alerta automáticamente.

## Fuente de datos

| Recurso | Qué se extrae |
|---|---|
| `/proc/net/nf_conntrack` | SYN hacia `dport=22` |
| `logread` / `/var/log/messages` | `Failed password` / `Accepted password` de dropbear |

## Algoritmo (doble)

```
VÍA A — Conexiones (rápida, no requiere log):
  SYNs hacia dport=22 desde misma IP en window_seconds
  SI count >= threshold_attempts → ALERTA

VÍA B — Autenticación (precisa, requiere log):
  leer logread, extraer (ip, "Failed password", timestamp)
  contar por IP en window_seconds
  SI count >= threshold_failures → ALERTA
```

La **vía B es la gold standard**: no confunde un escáner de puertos con un ataque de
credenciales. La vía A detecta antes pero con más falsos positivos. El alumno debe **usar
ambas** y explicar cuándo va cada una.

## Configuración

```ini
threshold_attempts = 5      # intentos en 10 s
window_seconds     = 10
auth_log           = /var/log/messages
auth_patterns      = Failed password,authentication failure
protect            = 1      # activar bloqueo
ban_seconds        = 900    # ban de 15 min
whitelist_ips      = 192.168.1.0/24
```

## Bloqueo (opcional, "protect")

Cuando se dispara, la app inserta una regla iptables:

```sh
iptables -I INPUT -s $SRC_IP -p tcp --dport 22 -j DROP
```

Y la retira sola después de `ban_seconds` mediante un hilo/temporizador o en el siguiente ciclo.
**El alumno debe implementar la expiración del ban.**

## Prueba en laboratorio

```bash
hydra -l root -P /usr/share/wordlists/rockyou.txt -t 10 ssh://192.168.1.1

# Verificar ban
ssh root@192.168.1.1 "iptables -L INPUT -n | grep $IP_ATACANTE"
```

## Qué enseña

- Lectura de **logs del sistema** como fuente de datos de una app
- **Persistencia temporal** (el ban debe expirar)
- Inyección controlada de reglas de firewall
- La diferencia entre **alertar** (detectar) y **proteger** (bloquear)

---
---

<a name="a5"></a>

# LAB 5.5 · DoS CONNECTION LIMITER

**Producto de referencia:** DoS Connection Limiter · **Categoría:** Acción · **Versión:** v1.0.0

---

## Qué hace

Previene ataques DoS limitando el número de conexiones TCP simultáneas por dirección IP.
Detecta cuando una IP excede el umbral de conexión configurado y dispara alertas, manteniendo
la estabilidad del router.

## Fuente de datos

`/proc/net/nf_conntrack`: todas las entradas TCP agrupadas por `src=`.

## Algoritmo

```
POR CADA ciclo (default: 5 s):
  1. Contar conexiones TCP activas por IP origen
  2. Para cada IP con count > max_connections:
        - alertar
        - si block=1: instalar regla iptables DROP para esa IP
  3. Limpieza de sesiones:
        - para IPs con count == 0, retirar su regla iptables
        - (default: cleanup_after = 30 s)
```

## Configuración

```ini
max_connections = 20       # conexiones simultáneas por IP
sample_interval = 5
block_mode      = 1        # 0 = solo alerta, 1 = bloquea
cleanup_seconds = 30       # cuándo retirar el bloqueo
whitelist_ips   = 192.168.1.1
```

## Prueba en laboratorio

```bash
# Generar 60 conexiones simultáneas desde el PC
for i in $(seq 1 60); do nc -z 192.168.1.1 80 & done

# O con mejor control:
ab -n 200 -c 50 -q http://192.168.1.1/
```

## Qué enseña

- **La decisión de diseño clave del curso: DETECTAR vs BLOQUEAR**
  - `block_mode=0` → la app observa (segura, sin riesgo de cortar servicio legítimo)
  - `block_mode=1` → la app actúa (poderosa, con riesgo: si el umbral está mal, cortas tu
    propia sesión)
- **Limpieza de estado**: un bloqueador sin expiración es un DDoS contra ti mismo
-_ipersistencia_: qué pasa si la app se reinicia con reglas iptables huérfanas

---
---

<a name="a6"></a>

# LAB 5.6 · WiFi CHANNEL OPTIMIZER

**Producto de referencia:** WiFi Channel Optimizer · **Categoría:** Acción · **Versión:** v2.2.0

---

## Qué hace

Optimización automática e inteligente del canal WiFi para mejor rendimiento. Analiza el
espectro WiFi y cambia automáticamente al canal óptimo para reducir interferencias y maximizar
el rendimiento de red.

## ¿La más compleja del catálogo?

**Sí.** Es la única que **toma una decisión y la ejecuta** (las otras 14 detectan y notifican).
No es un detector pasivo: es un sistema de control.

## Fuentes de datos

| Comando | Qué entrega |
|---|---|
| `iw dev wlan0 survey dump` | Mapa del espectro: ruido y busy time por canal |
| `iwinfo wlan0 scan` | APs vecinos: canal, señal (RSSI), SSID |
| `iwinfo wlan0 info` | Canal actual y modo |

## Algoritmo

```
1. RECOLECTAR:
   - para cada canal 1..13:
        vecinos[canal] = # de APs en ese canal
        señal[canal]   = suma de RSSI de APs en ese canal (ponderada)
        ruido[canal]   = survey noise (o -95 dBm default si no hay survey)

2. PUNTUAR (menor score = mejor canal):
   score[canal] = W1*vecinos[canal]
                + W2*(señal[canal] / 10)
                + W3*(ruido[canal] < -90 ? 0 : 10)

   // penalización extra: preferir canales no solapados con 1,6,11

3. DECIDIR:
   mejor = argmin(score)
   SI score[mejor] > umbral_mejora:
        NO cambiar (no vale la pena，风险 de desconexión)
   SI mejor != canal_actual:
        cambiar

4. EJECUTAR:
   iw dev wlan0 set channel <mejor>
   (o vía UCI: uci set wireless.@wifi[0].channel=<mejor>; uci commit wireless)

5. VERIFICAR:
   releer iwinfo wlan0 info → confirmar canal nuevo
```

## Configuración

```ini
optimize_schedule = 03:00   # ejecutar optimización a las 3 AM
optimize_interval = 24h    # cada 24 horas
min_improvement   = 20      # no cambiar si la mejora es menor
weights           = 3,1,1   # W1, W2, W3
never_use         = 2,3,4   # canales solapados si usa 1/6/11
default_iface     = wlan0
```

## Prueba en laboratorio

```bash
# Ver espectro actual
ssh root@ROUTER 'iw dev wlan0 survey dump'
ssh root@ROUTER 'iwinfo wlan0 scan | grep -E "Channel|Signal"'
ssh root@ROUTER 'iwinfo wlan0 info | grep Channel'

# Forzar optimización
/etc/init.d/wifi-channel-optimizer run-once
```

## Qué enseña

- **Programación con decisión**: no solo reportar, sino **elegir y actuar**
- **Función objetivo**: convertir un problema difuso ("mejor rendimiento") en números
- **Trade-off explícito**: ¿cuánto riesgo de desconexión aceptas por cuánta mejora?
- Interacción con `iw`, `iwinfo` y la capa de configuración UCI
- Programación por **ventana horaria** (schedule-based, como pide el producto)

---
---

# SECCIÓN NOTIFICACIÓN (Módulo 6)

> **Contexto común de los labs N1–N9.** Todos son **monitores** de una métrica del sistema.
> Todos comparten el **motor de umbral de `labcore`** (histéresis + debounce) y el **formato
> de alerta estándar**. La diferencia entre un buen monitor y uno insoportable está **100% en
> el diseño del umbral**.

---
---

<a name="n1"></a>

# LAB 6.1 · CPU MONITOR ALERT

**Producto de referencia:** CPU Monitor Alert · **Categoría:** Notificación · **Versión:** v1.2.0

---

## Qué hace

Monitoreo en tiempo real del uso de CPU con alertas personalizables. Incluye gráficos
históricos y análisis de tendencia.

## Fuente de datos

`/proc/stat`, primera línea:
```
cpu  user nice system idle iowait irq softirq steal guest guest_nice
```

## Algoritmo

```
EN CADA ciclo (default: 5 s):
  1. Leer /proc/stat  →  total = Σ(user..steal), idle = idle + iowait
  2. SI es la primera muestra:
        esperar 200 ms, tomar segunda muestra
  3. cpu% = 100.0 * (1 - (Δidle / Δtotal))
  4. Aplicar motor de umbral (labcore_threshold):
        SI cpu% >= warn_high  → entrar en ALERTA
        SI cpu% <= warn_low   → salir de ALERTA (histéresis)
  5. SI hay cambio de estado O intervalo_reporte:
        publicar métrica
```

## ⚠ La trampa del módulo (obligatoria)

En **MIPS32, `unsigned long` es de 32 bits**, y **`strtoul` SATURA a `ULONG_MAX`** en
desbordamiento (no hace wrap modular). Con HZ=100, `/proc/stat` supera 2³² tras **~497 días**
de uptime → el delta se vuelve 0 → el CPU% queda **clavado en 0.0 para siempre**.

**El fallo es invisible durante meses y luego rompe el servicio silenciosamente.**

**Solución obligatoria:**
```c
unsigned long long f[10];
f[n++] = strtoull(q, &end, 10);     /* NO strtoul */
s->total = 0;
for (int i = 0; i < 8 && i < n; i++) s->total += f[i];
s->idle = f[3] + (n > 4 ? f[4] : 0ULL);
```

> Esta es una incidencia real detectada por el agente auditor en el proyecto semilla
> `lp104mqttv2`. **El alumno debe encontrarla él mismo** en su implementación antes de que
> el auditor se la reporte. Es el ejercicio de prevención activa.

## Configuración

```ini
sample_interval = 5
warn_high       = 80       # entrar en alerta
warn_low        = 70       # salir de alerta (histéresis)
report_interval = 60       # publicar aunque no haya cambio
trend_window    = 10       # muestras para tendencia
```

## Prueba en laboratorio

```bash
# Generar carga
ssh root@ROUTER 'while true; do dd if=/dev/zero bs=1M count=500 2>/dev/null | md5sum; done &'
# En otro terminal: ver subir el %
mosquitto_sub -h broker -t 'teltonika/alerts/cpu' -v
```

## Qué enseña

- Lectura y parseo de `/proc/stat`
- **Aritmética de 64 bits en plataformas de 32 bits** (error real, no teórico)
- Histéresis y por qué una app sin histéresis genera 200 alertas por minuto

---
---

<a name="n2"></a>

# LAB 6.2 · RAM USAGE NOTIFIER

**Producto de referencia:** RAM Usage Notifier · **Categoría:** Notificación · **Versión:** v1.4.2

---

## Qué hace

Alertas inteligentes de uso de RAM con optimización automática. Monitorea el uso de RAM,
envía alertas en niveles críticos, incluye sugerencias de optimización y liberación automática
de memoria.

## Fuente de datos

`/proc/meminfo`:
```
MemTotal:        126628 kB
MemFree:          48216 kB
MemAvailable:     89304 kB
Buffers:           4096 kB
Cached:           38120 kB
```

## ⚠ La clave: `MemAvailable`, no `MemFree`

**`MemFree` en Linux es ruido.** El kernel usa RAM libre como caché de buffers; que haya poca
RAM libre es **lo normal y lo correcto**. Medir "uso de RAM" con `MemFree` produce alertas
constantes y falsas.

**La métrica correcta es `MemAvailable`** (estimación de memoria disponible **sin swap**, ya
descontando el caché reclamable). Introducida en Linux 3.14; el RUT200 con kernel 5.15 la tiene.

```c
ram_used_pct = 100.0 * (1.0 - (double)mem_available_kb / (double)mem_total_kb);
```

## Algoritmo

```
EN CADA ciclo (default: 10 s):
  1. Parsear /proc/meminfo → MemTotal, MemAvailable
  2. used% = 100 * (1 - Available/Total)
  3. Motor de umbral con 3 niveles:
        < 75%  : NORMAL
        75–90% : WARNING   ("considerar reiniciar apps inactivas")
        > 90%  : CRITICAL  ("liberar memoria YA")
  4. Si WARNING/CRITICAL:
        - obtener top 5 procesos por RSS de /proc/*/status
        - incluir sugerencias en la alerta
  5. Si auto_release = 1 y CRITICAL:
        matar procesos RSS > max_rss_kb y no críticos
        (CON PRECAUCIÓN: whitelist de procesos de sistema)
```

## ⚠ Autoptimización: el feature más peligroso del catálogo

"Automatic memory release" implica **matar procesos**. El alumno debe:

1. Implementarlo con **whitelist estricta** de procesos de sistema críticos
2. **Nunca** matar: `procd`, `dropbear`, `gsmd`, `netifd`, ni el propio proceso
3. Documentar el riesgo en el datasheet

**Es el lab donde se aprende que "automático" y "seguro" son palabras diferentes.**

## Configuración

```ini
sample_interval = 10
warn_high       = 75
crit_high       = 90
release_high    = 80
auto_release    = 0        # 1 = matar procesos (PELIGROSO)
max_rss_kb      = 8192
whitelist_procs = procd,dropbear,gsmd,netifd,dnsmasq,ubusd
top_n           = 5
```

## Prueba en laboratorio

```bash
ssh root@ROUTER 'dd if=/dev/zero bs=1M count=100 2>/dev/null | while read l; do echo $l; done &'
ssh root@ROUTER 'cat /proc/meminfo | head -3'
```

## Qué ética

- La diferencia entre una métrica **bruta** y una métrica **semánticamente correcta**
- Lectura de `/proc/*/status` para inventario de procesos
- El diseño de la **listas blanca** como mecanismo de seguridad
- Por qué la "auto-optimización" es la feature más peligrosa del catálogo

---
---

<a name="n3"></a>

# LAB 6.3 · CELLULAR SIGNAL MONITOR

**Producto de referencia:** Cellular Signal Monitor · **Categoría:** Notificación · **Versión:** v1.0.0

---

## Qué hace

Monitoreo en tiempo real de la fuerza de señal celular (SINR) y calidad. Envía alertas
automáticas cuando la calidad de señal degrada, para mantener conectividad óptima en
aplicaciones IoT industriales.

## Fuente de datos

`gsmctl -q` (RutOS ≥ 00.07.03 devuelve todas las métricas):
```
RSSI: -60
RSRP: -103
SINR: 2
RSRQ: -16
```

En 2G/fallback puede devolver un solo entero (`-55`) o `N/A`.

## ⚠ Parseo robusto (obligatorio)

```c
/* 1) Buscar la línea "RSSI:"
   p = strstr(out, "RSSI:");  if (p) p += 5;

   /* 2) Saltar espacios/tabs/newlines */
   while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;

   /* 3) "N/A" o vacío → dato no disponible (return -1, no inventar) */
   if (*p=='N' || *p=='n' || *p=='\0') return -1;

   /* 4) strtol con validación de que realmente parseó un número */
   long v = strtol(p, &end, 10);
   if (end == p) return -1;
   *rssi = (int)v;
```

## Algoritmo

```
EN CADA ciclo (default: 30 s):
  1. gsmctl -q → parsear RSSI, RSRP, SINR, RSRQ
  2. Clasificar calidad:
        BUENA     : RSSI >= -70  Y  SINR >= 10
        PROMEDIO  : RSSI >= -90  Y  SINR >= 3
        POBRE     : resto
  3. Aplicar histéresis en la clasificación (no oscilar entre BUENA y PROMEDIO)
  4. SI calidad < calidad_minima:
        ALERTA con las 4 métricas + clasificación
  5. SI calidad mejoró y estaba en alerta:
        ALERTA de recuperación
```

## Configuración

```ini
sample_interval    = 30
min_quality        = promedio    # good | average | poor
use_sinr           = 1
good_rssi          = -70
good_sinr          = 10
poor_rssi          = -90
poor_sinr          = 3
alert_on_recovery  = 1
```

## Prueba en laboratorio

```bash
ssh root@ROUTER 'gsmctl -q'
ssh root@ROUTER 'gsmctl -t'   # tipo de conexión (NO confundir con -c)
# Mover la antena / alejar el router de la ventana y ver la degradación
```

## Qué enseña

- Parseo robusto de salidas de texto que **cambian de formato entre versiones de firmware**
- Clasificación con umbrales múltiples (no un solo número)
- **null vs cero**: "no sé la señal" ≠ "señal cero"
- Uso de `popen` con **comandos constantes** (justificación de seguridad)

---
---

<a name="n4"></a>

# LAB 6.4 · CABLE CONNECTION ALERT

**Producto de referencia:** Cable Connection Alert · **Categoría:** Notificación · **Versión:** v1.0.5

---

## Qué hace

Alertas instantáneas al conectar o desconectar un cable de red. Útil para monitorear la
integridad física de la red.

## Fuente de datos

| Ruta | Qué entrega |
|---|---|
| `/sys/class/net/eth0/carrier` | `1` = hay link, `0` = no hay |
| `/sys/class/net/eth0/operstate` | `up` / `down` / `unknown` |
| `/sys/class/net/eth0/speed` | velocidad negociada |
| `/sys/class/net/eth0/mtu` | (contexto) |

## ⚠ Detección de flanco, no de nivel

El error clásico: publicar "conectado" cada 5 segundos cuando está conectado (ruido).
La app debe **publicar SOLO en el cambio de estado**:

```
estado_anterior = DESCONOCIDO        /* primer ciclo */

EN CADA ciclo (default: 5 s):
  1. leer carrier → actual (0/1)
  2. SI actual != estado_anterior:
        - determinar dirección (subida/bajada)
        - incluir velocidad, duplex, si está disponible
        - PUBLICAR alerta
        - cooldown: no re-publicar por 10 s
  3. estado_anterior = actual
```

**Persistencia:** el estado inicial debe ser `DESCONOCIDO`, no `0`. Si arrancas la app con el
cable puesto y asumes "desconectado", generas una falsa alarma al primer ciclo.

## Configuración

```ini
sample_interval = 5
monitor_ifaces   = eth0,eth1
cooldown         = 10
report_on_start  = 1       # publicar estado inicial
detect_speed     = 1       # incluir velocidad si cambia
```

## Prueba en laboratorio

```bash
# Estado actual
ssh root@ROUTER 'cat /sys/class/net/eth0/carrier'
# PROBAR: desconectar y conectar el cable FÍSICAMENTE
ssh root@ROUTER 'ip link show eth0'
```

## Qué enseña

- **Flancos vs niveles**: la diferencia entre un evento y un estado
- sysfs de red: la interfaz de bajo nivel del kernel
- Detección de **cambio**: la operación más básica y más olvidada
- Reinicio de la app sin generar falsas alarmas (estado inicial correcto)

---
---

<a name="n5"></a>

# LAB 6.5 · WiFi NEW USER ALERT

**Producto de referencia:** WiFi New User Alert · **Categoría:** Notificación · **Versión:** v1.3.1

---

## Qué hace

Detecta automáticamente cuando un nuevo dispositivo se conecta a la red WiFi y envía alertas
con información detallada del dispositivo.

## Fuente de datos

`iwinfo wlan0 assoclist` — una línea MAC por estación asociada (las líneas de detalle van
indentadas).

## ⚠ Dos errores reales que el alumno debe evitar

### Error 1: buffer insuficiente (I-001, incidencia real del proyecto semilla)

Cada estación genera ~4 líneas (~250 bytes). Con un buffer de 1024 bytes caben ~4 clientes y
el conteo **se trunca silenciosamente** con más:

```c
char al[8192];   /* 8 KiB: soporta el máximo de 50 clientes del RUT200 */
```

### Error 2: contar líneas, no MACs

```
Signal: -52 dBm 00:11:22:33:44:55   ← línea de detalle (indentada, NO cuenta)
Inactivity: 0:00:00                ← línea de detalle (NO cuenta)
00:11:22:33:44:55                  ← línea MAC (CUENTA)
```

```c
/* contar solo líneas que empiezan con MAC (no indentadas) */
if (len >= 17 && p[0] != ' ' && p[0] != '\t') {
    int colons = 0;
    for (int i = 0; i < 17; i++) if (p[i]==':') colons++;
    if (colons == 5) count++;
}
```

## Algoritmo

```
EN CADA ciclo (default: 10 s):
  1. Leer assoclist → conjunto de MACs asociadas AHORA
  2. Cargar conjunto de MACs CONOCIDAS (persistido)
  3. nuevas = ahora − conocidas
  4. SI nuevas no está vacío:
        - para cada MAC en nuevas:
              → consultar OUI (vendor) si está disponible
              → clasificar: conocido / desconocido
              → si en blacklist: opcionalmente bloquear (hostapd_cli)
        - publicar alerta
  5. known = known ∪ ahora        /* ÓNDEF, monotonamente creciente */
  6. persistir known
```

## ⚠ Persistencia: por qué `/tmp` NO sirve

`/tmp` es tmpfs: **se limpia en cada reinicio**. Si guardas las MACs conocidas ahí, tras un
reinicio **todas las MACsidf数据包烩ples serán "nuevas"** → avalancha de alertas falsas.

```
Rutas del RUT200:
  /etc      → overlay rw, PERSISTE
  /usr/local→ overlay rw, PERSISTE  ← usar aquí
  /tmp      → tmpfs, VOLÁTIL  ← NO usar
```

## Configuración

```ini
sample_interval = 10
iface           = wlan0
state_file      = /usr/local/lib/wifi-new-user-alert/known
whitelist       = AA:BB:CC:DD:EE:FF,11:22:33:44:55:66
blacklist       = DE:AD:BE:EF:00:01
alert_on_leave  = 1
vendor_lookup   = 1
max_known       = 500
```

## Prueba en laboratorio

```bash
# Conectar un teléfono al SSID del router → alerta inmediata
ssh root@ROUTER 'iwinfo wlan0 assoclist | wc -l'
ssh root@ROUTER 'iwinfo wlan0 assoclist'
```

## Qué enseña

- **Detección de "nuevo"** → requiere memoria de estado (diff contra histórico)
- Persistencia correcta en un sistema con almacenamiento limitado
- Conjuntos y deduplicación en C (sin `hash_set` en la stdlib)
- Whitelist / blacklist como patrón de configuración universal

---
---

<a name="n6"></a>

# LAB 6.6 · LOGIN ATTEMPT MONITOR

**Producto de referencia:** Login Attempt Monitor · **Categoría:** Notificación · **Versión:** v2.0.1

---

## Qué hace

Monitorea todos los intentos de inicio de sesión al router y envía alertas sobre actividad
sospechosa o intentos fallidos repetidos.

## Fuente de datos

| Recurso | Contenido relevante |
|---|---|
| `/var/log/messages` (o `logread`) | `dropbear ... Failed password for root from 192.168.1.77 port 1234` |
| `/var/log/messages` | `Accepted password for root from ...` |
| `/var/log/wtmp` / `utmp` | sesiones abiertas (login actual) |
| `/proc/net/nf_conntrack` | correlación con conexiones |

## Algoritmo

```
EN CADA ciclo (default: 10 s):
  1. Determinar posición actual en el log (byte offset)
     ★ solo leer las líneas NUEVAS desde el último ciclo
       (releer todo el log cada 10 s es desperdicio y puede
        perder eventos si la app se reinicia)

  2. Por cada línea nueva:
        - extraer: timestamp, IP, usuario, resultado (OK / FAIL)
        - contabilizar

  3. Agregar en ventana deslizante por IP:
        fallidos_en_ventana >= threshold_fails  → ALERTA (fuerza bruta)
        (éxitos >= 1) → evento "login exitoso" (auditoría)

  4. Persistir el offset para el próximo ciclo

  5. Patrón de ataque distribuido:
        si N IPs distintas fallan 1 vez cada una en ventana →
        ALERTA de nivel inferior (posible passphrase spraying)
```

## ⚠ El problema del offset (y por qué lo naïve falla)

Si guardás solo un offset y el log **se rota** (`logrotate` o reinicio), el offset apunta a un
punto inválido → **se pierde el historial o se relee basura**.

**Solución robusta:** guardar `offset` + `inode` del archivo de log.
Si el inode cambia → el log rotó → empezar desde 0 y alertar "log rotado".

## Configuración

```ini
log_file         = /var/log/messages
track_patterns   = Failed password,Accepted password,authentication failure
threshold_fails  = 5
window_seconds   = 300
spray_ips        = 5           # # IPs distintas para alertar spraying
spray_threshold  = 3           # fallos por IP para considerar spraying
alert_on_success = 1
state_file       = /usr/local/lib/login-attempt-monitor/state
```

## Prueba en laboratorio

```bash
# Desde el PC, 8 intentos fallidos
for i in $(seq 1 8); do ssh root@incorrecto@192.168.1.1 2>/dev/null; done

# Verificar
ssh root@ROUTER 'logread | grep -c "Failed password"'
mosquitto_sub -h broker -t 'teltonika/alerts/login' -v
```

## Qué enseña

- **Parsing de logs** con formatos no documentados
- Lectura incremental (offset + inode) y rotación de logs
- Detección de ataques **distribuidos** (spraying) además de los centrales
- Persistencia de estado con detección de invalidación

---
---

<a name="n7"></a>

# LAB 6.7 · DISK USAGE ALERT

**Producto de referencia:** Disk Usage Alert · **Categoría:** Notificación · **Versión:** v1.1.3

---

## Qué hace

Monitorea el uso del almacenamiento interno y externo del router, enviando alertas cuando el
espacio disponible es bajo.

## Fuente de datos

| Recurso | Qué entrega |
|---|---|
| `statvfs()` en `/overlay` | espacio total/libre del overlay (flash) |
| `statvfs()` en `/usr/local` | persistencia principal |
| `statvfs()` en `/tmp` | tmpfs (RAM, no flash) |
| Directorio objetivo (configurable) | `du`-style: archivos grandes |

## ⚠ El recurso escaso real: la FLASH de 16 MB

El RUT200 tiene **16 MB de flash SPI NOR**, de los cuales una parte es el firmware. El overlay
disponible es de apenas **~3–8 MB**. Este es el recurso que de verdad se agota.

**Consecuencia práctica:** un `logread` descontrolado o un archivo de estado mal puesto puede
**llenar la flash e inutilizar el router**. Esta app es una de las más importantes del catálogo.

## Algoritmo

```
EN CADA ciclo (default: 5 min):
  1. statvfs(mountpoint) → f_bfree, f_bavail, f_bsize
  2. usado_pct = 100 * (1 - (f_bavail / (f_bfree + f_bavail)))
  3. Motor de umbral:
        > 80%  : WARNING
        > 90%  : CRITICAL  ("BORRAR LOGS ANTES DE QUEDARSE SIN ESPACIO")
  4. Si > warn_high:
        - analizar archivos grandes (top N por tamaño)
        - incluir en la alerta el top 10 con su tamaño y ruta
  5. Si auto_clean = 1 y > release_high:
        - limpiar solo rutas en whitelist (/tmp/*, logs rotados)
        - NUNCA borrar /etc ni /usr/local automáticamente
```

## ⚠ Los inodos no Tell

`statvfs` devuelve dos cosas distintas:
- **Bloques** (`f_blocks`, `f_bfree`): espacio en bytes
- **Inodos** (`f_files`, `f_ffree`): cantidad de archivos

Con muchos archivos pequeños, puedes tener **30% de espacio libre pero 0 inodos** (o al revés).
Una app seria reporta **ambos**.

## Prueba en laboratorio

```bash
ssh root@ROUTER 'df -h'
ssh root@ROUTER 'df -i'
# Llenar la flash de verdad (¡con cuidado!)
ssh root@ROUTER 'dd if=/dev/zero of=/usr/local/test bs=1M count=5'
ssh root@ROUTER 'rm -f /usr/local/test'
```

## Qué enseña

- `statvfs()`: el API real de información de filesystem
- Bloques vs inodos
- Por qué la flash de un router es un recurso crítico
- Limpieza automática **con whitelist** (nunca borrar a ciegas)

---
---

<a name="n8"></a>

# LAB 6.8 · TEMPERATURE MONITOR PRO

**Producto de referencia:** Temperature Monitor Pro · **Categoría:** Notificación · **Versión:** v1.5.0

---

## Qué hace

Monitoreo térmico avanzado con alertas por sobrecalentamiento. Incluye gráficos históricos.

## ⚠ La verdad incómoda: el RUT200 NO tiene sensor de temperatura de CPU

Este lab es, en parte, un ejercicio de **honestidad técnica**.

El SoC **MT7628AN no expone `/sys/class/thermal/thermal_zone*/temp`**. No hay zona térmica de
SoC. Un análisis de referencia (`iwinfo`/`ls /sys/class/thermal`) lo confirma:

```bash
ssh root@ROUTER 'ls /sys/class/thermal/'
# vacío o inexistente
```

La "Temperature" que muestra el WebUI de Teltonika es la del **MÓDEM LTE**, no de la CPU.

## Fuente de datos (la correcta)

`gsmctl -c` → temperatura del módem en **décimas de °C**:
```bash
ssh root@ROUTER 'gsmctl -c'
360          # → 36.0 °C
```

⚠ **No confundir `-c` (temp) con `-t` (tipo de conexión)**. Es el error más fácil de cometer.

## ⚠ La honestidad como requisito de calidad

**El alumno DEBE:**
1. Detectar si `/sys/class/thermal/` existe
2. Si existe → usarla (y validar contra la del módem)
3. **Si no existe → usar `gsmctl -c` y etiquetar el campo como `modem_temp_c`**
4. Documentarlo explícitamente en el datasheet

**Prohibido inventar un valor.** El producto real reporta "temperatura del router"; un
monitor honesto reporta "temperatura del módem (el SoC no expone sensor)". Esta diferencia
es la diferencia entre un dispositivo confiable y uno que miente.

## Algoritmo

```
EN CADA ciclo (default: 60 s):
  1. Intentar /sys/class/thermal/thermal_zone*/temp
     → si OK: source = "cpu", temp = valor / 1000.0
  2. Si no existe:
     → gsmctl -c → source = "modem", temp = valor / 10.0
  3. Publicar SIEMPRE el campo `source` (transparencia)
  4. Motor de umbral:
        < 45°C  : NORMAL
        45–60°C : WARNING
        > 60°C  : CRITICAL
  5. Historial: guardar últimas N muestras (para "gráfico histórico")
```

## Configuración

```ini
sample_interval = 60
source_priority = auto,sysfs,gsmctl
warn_high       = 45
crit_high       = 60
history_samples = 60       # 1 hora a 60 s
unit            = celsius  # o fahrenheit (conversión en el agente)
```

## Prueba en laboratorio

```bash
ssh root@ROUTER 'ls /sys/class/thermal/ 2>&1'   # probablemente vacío
ssh root@ROUTER 'gsmctl -c'                       # 360 = 36.0 °C
# Mover el router al sol / cerca de una fuente de calor y observar
```

## Qué enseña

- **Verificar antes de asumir**: la ruta que "debería" existir puede no existir
- **Transparencia de la fuente de datos**: publicar de dónde viene cada número
- **Ética de la instrumentación**: es mejor reportar "no sé, y esto es lo que sé" que mentir
- Unidad y escala: décimas vs milésimas (error ×10 o ×1000)

---
---

<a name="n9"></a>

# LAB 6.9 · SYSTEM FILE MONITOR

**Producto de referencia:** System File Monitor · **Categoría:** Notificación · **Versión:** v1.0.0

---

## Qué hace

Monitoreo en tiempo real de archivos y directorios críticos del sistema. Rastrea
modificaciones no autorizadas a `passwd`, `shadow`, archivos de configuración, y monitorea el
historial de comandos en busca de actividad sospechosa.

## Fuentes de datos

| Ruta | Qué protege | Prioridad |
|---|---|---|
| `/etc/passwd` | cuentas de usuario | CRÍTICA |
| `/etc/shadow` | hashes de contraseñas | CRÍTICA |
| `/etc/group` | grupos | ALTA |
| `/etc/config/*` | configuración de red/firewall/system | CRÍTICA |
| `/etc/firewall.conf`, `/etc/config/firewall` | reglas de firewall | CRÍTICA |
| `/root/.ash_history` | historial de comandos | MEDIA |
| `/etc/lp104mqttv2.conf` y equivalentes | credenciales | CRÍTICA |
| `/usr/local/bin/*` | binarios instalados | ALTA |

## ⚠ Detección de cambio: dos técnicas

### Técnica A — hash del contenido (preferida)
```
baseline_hash = SHA256(contenido)
EN CADA ciclo:
    actual = SHA256(contenido_actual)
    SI actual != baseline_hash → CAMBIO DETECTADO
    actualizar baseline (y alertar solo en el primer cambio)
```
Detecta cualquier cambio, incluso si el mtime se manipula.

### Técnica B — mtime + tamaño (barata)
```
stat(path) → st_mtime, st_size
SI mtime > baseline_mtime  O  size != baseline_size → CAMBIO
```
Más barata, pero se puede evadir tocando solo mtime.

**Implementación en C sin librerías:**
- Hash: **no implementado desde cero** (demasiado para el curso). Usar `sha256sum` vía
  `read_cmd_line()` como comando externo — es la decisión pragmática correcta y hay que
  **documentarla explícitamente**
- mtime: `stat()` directo, sin comando externo

## ⚠ Persistencia y FAIL-OPEN vs FAIL-CLOSED

La pregunta clave: **si el archivo de baseline no existe o está corrupto, ¿qué hace la app?**

| Opción | Comportamiento | Riesgo |
|---|---|---|
| **FAIL-OPEN** | alerta y regenera baseline | no detecta el cambio que ya ocurrió |
| **FAIL-CLOSED** | alerta y NO regenera | protege, pero alerta en cada ciclo |
| **FAIL-SAFE** (elegido) | primera vez: genera baseline sin alertar; corrupto: alerta y regenera; hash ilegible: alerta y **NO** regenera | correcto |

## Configuración

```ini
scan_interval   = 300     # 5 min (escanear archivos es I/O)
method          = hash,mtime    # usar ambos
watch_list      = /etc/passwd,/etc/shadow,/etc/config,/root/.ash_history
baseline_file   = /usr/local/lib/system-file-monitor/baseline
alert_on_add    = 1       # archivo nuevo en /etc/config
alert_on_remove = 1       # archivo eliminado
hash_command    = sha256sum
max_history     = 100
```

## Prueba en laboratorio

```bash
# Ver la línea base
ssh root@ROUTER 'cat /usr/local/lib/system-file-monitor/baseline'

# MODIFICAR un archivo crítico
ssh root@ROUTER 'echo "# hack" >> /etc/config/system'

# Ver la alerta
mosquitto_sub -h broker -t 'teltonika/alerts/files' -v

# RESTAURAR
ssh root@ROUTER 'uci revert system && uci commit system'
```

## Qué enseña

- **Integridad de archivos**: concepto fundamental de seguridad
- Baseline: el patrón "primera ejecución vs comparación"
- **FAIL-SAFE vs FAIL-OPEN**: decisión de diseño con implicancia de seguridad
- Decisión pragmática: usar `sha256sum` externo en vez de implementar SHA-256 (y decirlo)

---
---

# ANEXO A · MATRIZ DE RESUMEN

## A.1 Fuentes de datos por aplicación

| Lab | `/proc` | `/sys` | Comando ext. | `iptables` | Logs |
|---|---|---|---|---|---|
| 5.1 Port Scan | conntrack | — | — | — | — |
| 5.2 ICMP Flood | conntrack | — | `hping3` (test) | ✓ | ✓ |
| 5.3 Web Attack | conntrack | — | `iw`/`iwinfo` | — | — |
| 5.4 SSH Bruteforce | conntrack | — | — | ✓ | ✓ |
| 5.5 DoS Limiter | conntrack | — | — | ✓ | — |
| 5.6 WiFi Optimizer | — | — | `iw`, `iwinfo`, `uci` | — | — |
| 6.1 CPU | `/proc/stat` | — | — | — | — |
| 6.2 RAM | `/proc/meminfo`, `/proc/*/status` | — | — | — | — |
| 6.3 Celular | — | — | `gsmctl -q` | — | — |
| 6.4 Cable | — | `net/eth*/carrier` | — | — | — |
| 6.5 WiFi User | — | — | `iwinfo assoclist` | — | — |
| 6.6 Login | conntrack | — | `logread` | — | ✓ |
| 6.7 Disco | — | — | `df` (verif.) | — | — |
| 6.8 Temperatura | — | thermal (no existe) | `gsmctl -c` | — | — |
| 6.9 Archivos | — | — | `sha256sum` | — | ✓ |

## A.2 Conceptos técnicos cubiertos (uno por app)

| Lab | Concepto principal |
|---|---|
| 5.1 | Sliding window + conteo de puertos únicos |
| 5.2 | Tasa vs cantidad absoluta |
| 5.3 | Detección de burst por varianza |
| 5.4 | Lectura de logs + bloqueo con expiración |
| 5.5 | Detectar vs bloquear + limpieza de estado |
| 5.6 | Función objetivo y decisión automática |
| 6.1 | Aritmética 64-bit en plataforma 32-bit |
| 6.2 | Métrica semánticamente correcta (`MemAvailable`) |
| 6.3 | Parseo robusto + null vs cero |
| 6.4 | Flancos vs niveles |
| 6.5 | Detección de "nuevo" con memoria de estado |
| 6.6 | Lectura incremental con rotación de logs |
| 6.7 | `statvfs` + bloques vs inodos |
| 6.8 | Verificar antes de asumir + ética de instrumentación |
| 6.9 | Integridad de archivos + FAIL-SAFE |

## A.3 Incidencias reales del proyecto semilla que el alumno debe evitar

Estas **no son teóricas**. Son fallos reales detectados por el agente auditor en `lp104MqttTeltonikaV2`:

| ID | Incidencia | Dónde aparece en el curso |
|---|---|---|
| **I-001** | Buffer de 1024 trunca el conteo de clientes WiFi con >4 estaciones | LAB 6.5 |
| **I-002** | `strtoul` satura en MIPS32 → CPU% clavado en 0.0 tras ~497 días | LAB 6.1 |
| **I-003** | Credenciales reales hardcodeadas en el `Makefile` versionado | Módulo 7 (seguridad) |
| **I-004** | `popen()` con datos externos = inyección de shell | LAB 6.3 (se usa con comandos constantes) |
| **I-005** | Buffer insuficiente en `read_cmd_line` para salidas grandes | LAB 6.5 |

> **Ejercicio de prevención**: antes de entregar cada lab, el alumno debe **buscar activamente**
> estas clases de error en su propio código. Encontrarlas solo vale más que que se las
> reporten.

---

# ANEXO B · PROTOCOLO DE PRUEBA EN LABORATORIO

Todo lab se prueba con el mismo protocolo de 6 pasos. El agente `auditor-teltonika` verifica que
el alumno lo haya seguido.

```
□ PASO 1 — BUILD LIMPIO
     make clean && make
     file ./lab → debe decir MIPS, statically linked

□ PASO 2 — TAMAÑO RAZONABLE
     ls -la ./lab → < 150 KB (el binario debe ser pequeño)

□ PASO 3 — DEPLOY SIN EJECUTAR
     scp del binario + .conf + init.d al router
     chmod +x, chmod 600 en el .conf
     ⚠ NO arrancar todavía

□ PASO 4 — PRUEBA EN FOREGROUND
     Ejecutar en primer plano → ver los logs en pantalla
     Debe arrancar sin warnings ni errores

□ PASO 5 — PRUEBA DEL EVENTO REAL
     Generar el ataque/evento de verdad (nmap, hydra, hping3, cable, etc.)
     Confirmar que la alerta aparece

□ PASO 6 — INSTALAR COMO SERVICIO
     /etc/init.d/labNN enable && start
     Reiniciar el router → confirmar que arranca solo
     logread | grep labNN → confirmar que opera
```

**Evidencia a guardar por lab** (va en el repositorio del alumno, en `evidencia/labNN/`):
- Salida de `make` (sin warnings)
- Salida de `file`
- Salida de `ls -la`
- Logs del ataque real
- Log de la alerta MQTT detectada
- `logread` del servicio funcionando

---

**Siguiente documento:** [`ARQUITECTURA-LABCORE.md`](./ARQUITECTURA-LABCORE.md) — el diseño de
la librería que las 15 apps comparten.