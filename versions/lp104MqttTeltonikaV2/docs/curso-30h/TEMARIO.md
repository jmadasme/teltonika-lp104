# PROGRAMACIÓN DE MINI-APLICACIONES NATIVAS PARA ROUTERS TELTONIKA

## Cursouytemario técnico · 30 horas · Experimental · Asistido por Agentes de IA

**RUTOS / OpenWrt · RutOS 7 · MIPS little-endian · Lenguaje C embebido**

---

### Código de curso: `TELTONIKA-APPDEV-30H`
### Hardware de referencia: Teltonika **RUT200** (MT7628AN · `mipsel_24kc` · musl · RutOS `RUT2M_R_00.07.22.3`)
### Proyecto semilla: `lp104MqttTeltonikaV2` (este repositorio)

---

# 0. DECLARACIÓN DE INTENCIÓN DEL CURSO

Este curso existe para cerrar una brecha que, hasta hoy, parece insurmountable para quien no es
programador: **escribir programas que se ejecutan dentro de un router Teltonika**.

Todo lo que aprenderás aquí se corresponde con productos reales que hoy se venden en el
marketplace [rutosapp.com](https://rutosapp.com). Los **15 laboratorios** de este curso son la
reconstrucción técnica, completa y desde cero, de esas mismas aplicaciones.

La diferencia —y el motivo por el que este curso es posible— no es la talentía del alumno. Es que
**cada alumno trabaja con agentes de IA preentrenados especializados en Teltonika**, que ya conocen
el SDK, el sysfs, el procd, la UCI, los bugs del MT7628AN y los umbrales de compilación.

---

## 0.1 Lo que NO se necesita saber para entrar

Este curso tiene **requisito cero**. No se pide:

| Área | ¿Necesaria antes del curso? | ¿Qué hace la IA? |
|---|---|---|
| Programación (C, cualquier lenguaje) | **No** | Genera el esqueleto, explica cada línea, corrige, audita |
| Linux / línea de comandos | **No** | Redacta el comando exacto y predice su salida |
| OpenWrt /embedded systems | **No** | Explica sysfs, procd, overlay y UCIdesde cero |
| RutOS / Teltonika | **No** | Conoce la matriz de modelos, rutas y versiones |
| Redes (TCP/IP, MQTT, sockets) | **No** | Construye el protocolo byte a byte contigo |
| Toolchains / cross-compilación | **No** | Entrega el `Makefile` y la corrección de errores |
| Electrónica / GPIO | **No** | Mapea pines y niveles lógicos de tu modelo |

> **El compromiso pedagógico del curso:** en las primeras 6 horas el alumno ya tiene un programa
> compilado, cross-compilado, transferido y ejecutándose en su router. No en la hora 30. En la hora 6.

---

## 0.2 Lo que SÍ se entrega al alumno (material tangible)

Al cerrar el curso, cada alumno posee físicamente y en su repositorio:

### A. Código fuente completo de las 15 aplicaciones

```
teltonika-appdev/
├── labcore/                     # Librería núcleo compartida (ENTREGABLE)
│   ├── labcore.h
│   ├── labcore_config.c         # parser clave=valor (heredado de lp104mqttv2)
│   ├── labcore_log.c            # logging estructurado con niveles
│   ├── labcore_mqtt.c           # cliente MQTT 3.1.1 a mano (CONNECT/PUBLISH/SUBSCRIBE/PING)
│   ├── labcore_json.c           # constructor de JSON con guards de buffer
│   ├── labcore_threshold.c      # motor de umbrales con histéresis y anti-rebote (debounce)
│   ├── labcore_state.c          # persistencia de estado (último valor, baselines, whitelist)
│   ├── labcore_netmon.c         # abstracción sobre /proc/net/nf_conntrack
│   └── labcore_cmd.c            # popen seguro con timeout y sin inyección
│
├── apps/                        # 15 aplicaciones independientes (ENTREGABLE)
│   ├── a01-wifi-channel-optimizer/
│   ├── a02-port-scan-detector/
│   ├── a03-icmp-flood-detector/
│   ├── a04-web-attack-detector/
│   ├── a05-ssh-bruteforce-protector/
│   ├── a06-dos-connection-limiter/
│   ├── n01-cpu-monitor-alert/
│   ├── n02-ram-usage-notifier/
│   ├── n03-cellular-signal-monitor/
│   ├── n04-cable-connection-alert/
│   ├── n05-wifi-new-user-alert/
│   ├── n06-login-attempt-monitor/
│   ├── n07-disk-usage-alert/
│   ├── n08-temperature-monitor-pro/
│   └── n09-system-file-monitor/
│
├── deploy/                      # systemd-free: procd + init.d (ENTREGABLE)
│   ├── init.d/lab01-...     (×15 scripts procd)
│   ├── examples/lab01.conf.example ... lab15.conf.example
│   └── make_deploy.sh              # instalador de los 15 servicios
│
├── libs/                        # Librerías de terceros que el curso realmente usa (ENTREGABLE)
│   ├── README.md                    # por qué C puro y por qué cada excepción
│   └── (opcional) libuci/           # cliente UCI documentado, vs. parseo directo
│
├── Makefile                     # toolchain mipsel_24kc + musl + enlazado estático (ENTREGABLE)
├── sdk/                         # instrucciones de obtaining del SDK
└── docs/                        # ESTA documentación (ENTREGABLE)
    ├── TEMARIO.md                 # este documento
    ├── CATALOGO-LABS.md           # ficha técnica de las 15 apps
    ├── ARQUITECTURA-LABCORE.md    # diseño de la librería núcleo
    ├── AGENTES-IA.md              # cómo se usan los agentes en cada fase
    ├── datasheet/                 # manual de usuario de cada app
    └── developer/                 # explicación del código, línea por línea
```

### B. Documentación completa (4 niveles)

| Nivel | Documento | Para qué sirve |
|---|---|---|
| **1 · Usuario** | `datasheet/labNN-alerta.md` | Qué hace la app, cómo se configura, qué significan los umbrales, qué hace cada alerta |
| **2 · Técnico** | `developer/labNN-design.md` | Arquitectura, fuentes de datos en el router, algoritmo, fórmulas, complejidad |
| **3 · Código** | `developer/labNN-code.md` | Explicación función por función, variables, decisiones y alternativas descartadas |
| **4 · Operación** | `datasheet/OPERACION.md` | Instalación, diagnóstico, rollback, firmware factory reset seguro |

### C. Librerías y componentes reutilizables

`labcore` es una **librería real** que el alumno puede usar en sus propios proyectos: no es código
de muestra, es infraestructura de producción, con gestión de errores, guards de buffer,
timeouts, reconexión con backoff y cierre limpio por señales.

---

## 0.3 Lo que NO se entrega: los binarios propietarios de rutosapp

> **Clarificación legal y de múltiples, necesaria para que el curso sea legal y_transferible:**

El marketplace **rutosapp.com vende binarios cerrados propietarios** de estas 15 aplicaciones.

En este curso esas herramientas se usan de **material de estudio y referencia funcional**:

- **SÍ** se utiliza: la **especificación funcional** publicada (qué detecta, qué umbral, qué alerta)
- **NO** se distribuye: los binarios propietarios, ni su código, ni sus licencias
- **SÍ** se entrega: **15 implementaciones propias, en C, código abierto, desde cero**, que replican
  la funcionalidad declarada de cada producto

El resultado es que el alumno no copia código ajeno: **reconstruye la solución desde la
especificación**, que es exactamente el ejercicio profesional que se le pedirá en un campo real.
Y al final del curso tiene **más** que lo que el marketplace ofrece: código, documentación,
librerías y la capacidad de crear apps que el marketplace todavía no vende.

---

## 0.4 Las tres promesas del curso

1. **Velocidad**: de cero al primer programa ejecutándose dentro del router en **6 horas**.
2. **Calidad**: todo el código pasa por un **agente auditor estático** antes de considerarse
   terminado. Auditoría de buffers, desbordamientos, fugas, hilos, manejo de errores y secretos.
3. **Autonomía**: al final el alumno **no necesita el curso**. Tiene el mapa completo del sistema
   y agentes que loorientan en cualquier tarea nueva.

---

# 1. ARQUITECTURA PEDAGÓGICA DEL CURSO

## 1.1 El problema real y por qué la IA lo resuelve

El estudiante que intenta este camino **sin apoyo** pierde sus 30 horas en:

| Trampa | Horas que cuesta sin IA | Cómo la resuelve el agente |
|---|---|---|
| `Exec format error` | 3–6 h | Dice "falta cross-compilar: usá `mipsel-openwrt-linux-musl-gcc`, no el gcc del host" |
| Finding the SDK correcto | 2–4 h | Te da la URL exacta del SDK para tu modelo |
| `undefined reference` por libs de OpenWrt | 2–3 h | Te dice "enlazá estático y quitá esa dependencia" |
| No saber qué rutaSysFS existe en tu modelo | 4–8 h | Te dice "el MT7628AN no expone zona térmica; usá `gsmctl -c`" |
| Assembler propio de MQTT | 3–5 h | Escribe el CONNECT byte a byte y te lo explica |
| Atrapado en un bug de C de 64 bits | 2–3 h | Auditor: "usá `unsigned long long` + `strtoull`; en MIPS32 `strtoul` satura" |
| No saber si tu servico arranca | 1–2 h | Te da el script procd correcto con `respawn` y prioridades |
| Seguridad: secretos en el repo | 1 h | Auditor: "la password está hardcodeada en el Makefile — es una fuga" |

**Total ahorrado: 18–32 horas.** Ese es el valor real del curso.

## 1.2 Los agentes preentrenados que sostienen el curso

Cada agente tiene un **rol**, un **contexto** y un **formato de salida** definidos. Ver
`AGENTES-IA.md` para el protocolo completo.

| Agente | Rol en el curso | Momento en que se usa |
|---|---|---|
| **`orquestador-teltonika`** | Conduce el pipeline completo del alumno | Toda la curso |
| **`analista-teltonika`** | Especifica cada lab antes de escribir código | Módulo 2 y cada lab |
| **`implementador-teltonika`** | Escribe el código, compila, transfiere al router | Módulos 3–7 |
| **`auditor-teltonika`** | Auditoría estática: buffers, memoria, hilos, secretos | Antes de dar por terminado cada lab |
| **`teltonika-info`** | Investiga bloqueos en wiki oficial y comunidad | Cuando aparece duda específica |
| **`documentador-teltonika`** | Escribe README, datasheet y documentación dev | Cierre de cada módulo |

## 1.3 El ciclo de trabajo de cada alumno (idéntico en los 15 labs)

```
┌─────────────────────────────────────────────────────────────────┐
│  1. ESPECIFICAR    →  Agente analista-teltonika produce la ficha │
│                      técnica del lab: fuentes de datos,          │
│                      algoritmo, umbrales, alertas, riesgos       │
├─────────────────────────────────────────────────────────────────┤
│  2. CONSTRUIR      →  Agente implementador-teltonika escribe el  │
│                      código y lo compila con el toolchain        │
├─────────────────────────────────────────────────────────────────┤
│  3. TRANSFERIR     →  make deploy al router vía scp/ssh         │
├─────────────────────────────────────────────────────────────────┤
│  4. PROBAR EN VIVO →  Generar tráfico de ataque real desde el PC │
│                      del alumno y verificar la detección         │
├─────────────────────────────────────────────────────────────────┤
│  5. AUDITAR        →  Agente auditor-teltonika: análisis estático│
│                      completo. Sin incidencias → aprobado        │
├─────────────────────────────────────────────────────────────────┤
│  6. DOCUMENTAR     →  Agente documentador-teltonika genera la    │
│                      documentación del lab                      │
└─────────────────────────────────────────────────────────────────┘
                    ↓  Volver al paso 1 con el siguiente lab
```

> **Regla de oro del curso:** ninguna app se considera terminada hasta que ha sido
> **construida, analizada y probada en laboratorio** por el propio alumno.

## 1.4 Qué significa "experimental" en este curso

El 70% de las 30 horas es tiempo de laboratorio. Teoría: 9 horas. Laboratorio: 21 horas.

Las prácticas son **destructivas por diseño**: el alumno **genera ataques reales** contra su
propio router para ver funcionar su detector. No hay simulaciones: hay un `nmap` disparando
contra la IP del router, hay un `hydra` real, hay un ping flood de verdad, y hay un cable de red
que se desconecta físicamente.

---

# 2. REGLAS DE EVALUACIÓN

| Criterio | Peso | Qué se mide |
|---|---|---|
| **Compila y despliega** | 20% | El binario es MIPS, se transfiere, arranca como servicio |
| **Detecta de verdad** | 25% | La app detecta el ataque/evento real en vivo |
| **No rompe nada** | 20% | No hay falsos positivos Operations ni cuelgue del router |
| **Calidad de código** | 20% | Auditoría sin incidencias: buffers, errores, secretos |
| **Documentación** | 15% | datasheet + dev docs del lab |

**Nota sobre la nota**: el curso evalúa **construir, analizar y probar**, no memorizar.

---

# 3. TEMARIO HORA A HORA — 30 HORAS

```
╔══════════════════════════════════════════════════════════════════════════╗
║  MÓDULO 0 · INMERSIÓN                                    2 horas       ║
║  "¿Qué hay adentro de un router y por qué se le puede programar?"       ║
╠══════════════════════════════════════════════════════════════════════════╣
║ 0.1  Anatomía del RUT200 (0:00–0:30)                                  ║
║      · SoC MT7628AN: CPU MIPS 580 MHz, 128 MB RAM, 16 MB flash        ║
║      · Módem Quectel EC200A (LTE Cat 4)                                ║
║      · WiFi 2.4 GHz · 2× Ethernet · GPIO 2DI/2DO                      ║
║      · Mapa de puertos: I/O, RAM, flash, overlay, tmpfs               ║
║      · LA PREGUNTA CLAVE: ¿un router es solo "caja"? No: es un        ║
║        computador Linux completo con 128 MB de RAM                    ║
║                                                                          ║
║ 0.2  RutOS y OpenWrt (0:30–1:15)                                       ║
║      · OpenWrt: distribución Linux mínima para routers                  ║
║      · RutOS: la distribución de Teltonika sobre OpenWrt                ║
║      · BusyBox: el toolbox de UNIX completo en 1 MB                    ║
║      · El rootfs: qué es /rom, /overlay, /tmp, /usr/local             ║
║      · Ejercicio guiado: tour del router por SSH                       ║
║                                                                          ║
║ 0.3  Primer contacto con un agente (1:15–2:00)                          ║
║      · Prompt real: "analista-teltonika, especifícame el               ║
║        entorno de compilación para RUT200"                             ║
║      · Lectura del output: SDK, toolchain, flags, verificación         ║
║                                                                          ║
║ LAB 0 · ENTORNO          Entregable: entorno de compilación operativo  ║
╚══════════════════════════════════════════════════════════════════════════╝
```

```
╔══════════════════════════════════════════════════════════════════════════╗
║  MÓDULO 1 · EL ECOSISTEMA DE AGENTES DE IA              3 horas       ║
║  "Las 6 herramientas que van a hacer el trabajo pesado por ti"          ║
╠══════════════════════════════════════════════════════════════════════════╣
║ 1.1  El rol de cada agente (0:00–0:50)                                  ║
║      · orquestador → analista → implementador → auditor →                ║
║        documentador                                                     ║
║      · teltonika-info: el que busca en la wiki oficial                  ║
║      · Cuándo usar cada uno y con qué prompt                             ║
║                                                                          ║
║ 1.2  Escribir prompts que funcionan (0:50–1:40)                         ║
║      · El prompt de 5 partes: contexto + objetivo +                    ║
║        restricciones + formato de salida + criterio de éxito            ║
║      · Contexto obligatorio: modelo, versión RutOS, arquitectura         ║
║      · Ejemplo real aplicado a "CPU Monitor Alert"                      ║
║                                                                          ║
║ 1.3  Verificar y corregir al agente (1:40–2:30)                          ║
║      · La IA se equivoca: cómo detectarlo (el agente lo admite)        ║
║      · Verificación obligatoria: `file`, `logread`, prueba en vivo       ║
║      · Protocolo de "dame el comando y la salida esperada"              ║
║                                                                          ║
║ 1.4  Iterar rápido: el bucle de 3 pasos (2:30–3:00)                      ║
║      · Pedir → Verificar → Corregir. Nunca más de 3 ciclos sin          ║
║        consultar al agente `teltonika-info`                             ║
║                                                                          ║
║ LAB 1 · PROMPTING         Entregable: 5 prompts reutilizables del alumno║
╚══════════════════════════════════════════════════════════════════════════╝
```

```
╔══════════════════════════════════════════════════════════════════════════╗
║  MÓDULO 2 · PRIMER BINARIO DENTRO DEL ROUTER           4 horas       ║
║  "El momento de la verdad: tu código corriendo en MIPS"                 ║
╠══════════════════════════════════════════════════════════════════════════╣
║ 2.1  El toolchain de Teltonika (0:00–0:45)                              ║
║      · Descargar el SDK: OpenWrt 21.02.0 ramips/mt76x8                  ║
║      · mipsel-openwrt-linux-musl-gcc 8.4.0                             ║
║      · STAGING_DIR: por qué es obligatorio                              ║
║      · Enlazado estático: por qué (y por qué el error clásico)          ║
║      · la trampa del gcc del host: x86-64 ≠ MIPS                        ║
║                                                                          ║
║ 2.2  Anatomía de un programa embebido (0:45–1:30)                        ║
║      · main() como punto de entrada, no como "lógica"                  ║
║      · La estructura de un demonio: init → loop → cleanup               ║
║      · Includes mínimos: unistd, signal, stdio                          ║
║      · El costo de cada include en un binario de 128 KB                 ║
║                                                                          ║
║ 2.3  Primer programa: "hello teltonika" con logging (1:30–2:30)         ║
║      · Escribir 60 líneas de C: init_signals + log_msg + main          ║
║      · MAKEFILE con la ruta del toolchain                               ║
║      · make, file lp104hello, esperar "MIPS" en la salida                ║
║      · Errores que vas a ver y qué significan (tabla)                   ║
║                                                                          ║
║ 2.4  Transferir y ejecutar (2:30–3:15)                                   ║
║      · scp al router, chmod +x, ejecutar                                ║
║      · "Exec format error" → el error más común, la causa exacta        ║
║      · Verificar en el router que el proceso existe                     ║
║                                                                          ║
║ 2.5  Instalar como servicio: procd (3:15–4:00)                           ║
║      · El problema: al cerrar SSH muere el proceso                        ║
║      · init.d scripts: START, respawn, STOP                             ║
║      · /etc/init.d/miapp start|stop|status|restart                     ║
║      · logread: dónde viven los logs                                    ║
║      · Procd vs systemd: por qué NO usás systemd                        ║
║                                                                          ║
║ LAB 2 · HELLO+PROCD      Entregable: hello_teltonika en producción     ║
║                                                                          ║
║   ► HORA 6: el alumno tiene un programa compilado, transferido,          ║
║     ejecutándose como servicio que sobrevive al cierre de SSH.           ║
╚══════════════════════════════════════════════════════════════════════════╝
```

```
╔══════════════════════════════════════════════════════════════════════════╗
║  MÓDULO 3 · RUTOS POR DENTRO                             4 horas       ║
║  "La plataforma: cómo se habla con el router"                            ║
╠══════════════════════════════════════════════════════════════════════════╣
║ 3.1  sysfs y procfs: el router como sistema de archivos (0:00–0:50)    ║
║      · /proc/stat, /proc/meminfo, /proc/net/*                            ║
║      · /sys/class/net/*, /sys/class/thermal/*                          ║
║      · La trampa del MT7628AN: NO hay zona térmica de CPU               ║
║      · Cómo descubrir qué rutas existen en tu modelo                     ║
║      · Leer un archivo binario desde C con fopen/fgets                  ║
║                                                                          ║
║ 3.2  uci: configuración declarativa (0:50–1:35)                          ║
║      · /etc/config/*: el formato clave-opción-lista                     ║
║      · uci get/set/commit desde shell                                   ║
║      · Por qué nuestra app usa /etc/lab.conf y no UCI                   ║
║      · Comparación de ambos:什么时候 conviene cada uno                  ║
║                                                                          ║
║ 3.3  ubus y ubus call (1:35–2:10)                                        ║
║      · La API interna de OpenWrt en JSON                                 ║
║      · ubus call network.wireless status                                 ║
║      · Parsear JSON en C sin librerías: la técnica de substr            ║
║      · Cuándo usar gsmctl/ubus en vez de sysfs (y al revés)            ║
║                                                                          ║
║ 3.4  Herramientas Teltonika: gsmctl e iwinfo (2:10–3:00)                 ║
║      · gsmctl -q: RSSI/RSRP/SINR/RSRQ (métricas celulares)              ║
║      · gsmctl -c: temperatura del MÓDEM en décimas (360 = 36.0 °C)      ║
║      · gsmctl -t: tipo de conexión (no confundir con -c)                ║
║      · iwinfo wlan0 assoclist / info / scan                             ║
║      · Ejecutar comandos desde C: popen seguro y su justified            ║
║                                                                          ║
║ 3.5  Ejecución segura de comandos (3:00–4:00)                            ║
║      · popen vs system: por qué NO system()                             ║
║      · El riesgo de inyección de shell y cómo evitarlo                 ║
║      · read_cmd_line() con guard de buffer                               ║
║      · Ejercicio: escribir un recolector de 4 métricas como JSON        ║
║                                                                          ║
║ LAB 3 · TELEMETRÍA       Entregable: recolector de métricas en JSON     ║
╚══════════════════════════════════════════════════════════════════════════╝
```

```
╔══════════════════════════════════════════════════════════════════════════╗
║  MÓDULO 4 · LABCORE: LA LIBRERÍA NÚCLEO               4 horas       ║
║  "Construye la infraestructura que las 15 apps van a reutilizar"         ║
╠══════════════════════════════════════════════════════════════════════════╣
║ 4.1  labcore_config: parser de configuración (0:00–0:50)                 ║
║      · Formato clave=valor con comentarios #                             ║
║      · fgets línea a línea, trim, split en primer '='                    ║
║      · Validación de rangos: port, umbrales numéricos                    ║
║      · Fallback a defaults si el archivo no existe                       ║
║      · Regla de seguridad: NUNCA loguear la password                    ║
║      · POR QUÉ esto es una decisions de arquitectura: cambiar           ║
║        credenciales NO requiere recompilar                              ║
║                                                                          ║
║ 4.2  labcore_mqtt: MQTT 3.1.1 a mano (0:50–1:50)                         ║
║      · CONNECT: protocol name, level, flags, keepalive, payload          ║
║      · Remaining Length: la codificación de entero variable              ║
║      · CONNACK y sus 6 códigos de error                                  ║
║      · PUBLISH QoS 0 (con y sin retain)                                  ║
║      · SUBSCRIBE: lo necesario para recibir comandos del servidor        ║
║      · PINGREQ / PINGRESP: keepalive defensive                           ║
║      · write_all() con manejo de EAGAIN y poll(POLLOUT)                  ║
║      · Conexión no bloqueante + tcp_connect() con timeout                ║
║      · Backoff exponencial 5→60 s con sleep interrumpible               ║
║                                                                          ║
║ 4.3  labcore_json: construir JSON sin librerías (1:50–2:25)              ║
║      · snprintf con límite: la única forma segura                       ║
║      · Escapar comillas, barras invertidas y caracteres de control      ║
║      · null vs 0: la diferencia semántica entre "no sé" y "cero"        ║
║      · Numbers con precisión fija (%.1f) vs sin crisis                  ║
║                                                                          ║
║ 4.4  labcore_threshold: el motor de alertas (2:25–3:15)                  ║
║      · Esto es lo que diferencia una app útil de una app ruidosa         ║
║      · UMBRAL DE ENTRADA vs UMBRAL DE SALIDA: histéresis                 ║
║        (ej.: avisa al 80% CPU, deja de avisar al 70%)                    ║
║      · Debounce temporal: no bombardear cada 5 segundos                  ║
║      · Contadores de eventos: "10 puertos en 2 segundos"                ║
║      · Máquina de estados: NORMAL→ALERTA→ALERTA_FUENTE→NORMAL           ║
║                                                                          ║
║ 4.5  labcore_state + labcore_netmon (3:15–4:00)                          ║
║      · Persistir estado: última MAC conocida, baseline de disco          ║
║      · Por qué /tmp NO sirve (se limpia en cada reinicio)                ║
║      · /proc/net/nf_conntrack: la fuente de verdad para detección        ║
║      · Parseo línea a línea: protocolo, src, dst, flags                  ║
║      · Escribir un lector de conntrack en 60 líneas                      ║
║                                                                          ║
║ LAB 4 · LABCORE          Entregable: labcore compilada, linkeada,       ║
║                          con test que publica un JSON de ejemplo        ║
╚══════════════════════════════════════════════════════════════════════════╝
```

```
╔══════════════════════════════════════════════════════════════════════════╗
║  MÓDULO 5 · LABORATORIOS DE ACCIÓN                      5 horas       ║
║  "Las 6 herramientas de seguridad y red del marketplace"                 ║
║  (Cada app: 50 min · 10 min de prueba de ataque real en vivo)           ║
╠══════════════════════════════════════════════════════════════════════════╣
║                                                                          ║
║  ► LAB 5.1 · PORT SCAN DETECTOR                    (50 min + 10)       ║
║    Qué hace: detecta escaneo de puertos sospechoso y alerta              ║
║    Fuente de verdad: /proc/net/nf_conntrack (SYN_SENT)                   ║
║    Algoritmo:Sliding window de 2 s sobre (IP origen → puertos dst)     ║
║    Umbral: 10 puertos distintos en 2 s (configurable)                    ║
║    Prueba en vivo: `nmap -sS -p- --min-rate 5000 192.168.x.x`           ║
║    Lo que aprendes: sliding window, conteo de puertos únicos            ║
║                                                                          ║
║  ► LAB 5.2 · ICMP FLOOD DETECTOR                    (50 min + 10)       ║
║    Qué hace: detecta ping flood y alerta                                  ║
║    Fuente: conntrack con protocolo ICMP + regla iptables LOG            ║
║    Algoritmo:Sliding window de 2 s sobre ICMP echo-request             ║
║    Umbral: 20 pings en 2 s (configurable)                                ║
║    Prueba en vivo: `hping3 -c 200 -d 10 192.168.x.x`                    ║
║    Lo que aprendes:_difference entre "muchos paquetes" y "mucho tráfico"║
║                                                                          ║
║  ► LAB 5.3 · WEB ATTACK DETECTOR                      (50 min + 10)       ║
║    Qué hace: detecta ataques ráfaga contra la interfaz web              ║
║    Fuente: conntrack SYN hacia puerto 80/443                             ║
║    Algoritmo: ráfaga = ≥10 SYNs en 2 s hacia puerto web                 ║
║    Umbral: 10 SYNs en 2 s (configurable)                                 ║
║    Prueba en vivo: `for i in $(seq 1 30); do nc -z 192.168.x.x 80; done`║
║    Lo que aprendes: por qué un escáner legítimo dispara falso positivo  ║
║                     y cómo se mitiga con whitelist                      ║
║                                                                          ║
║  ► LAB 5.4 · SSH BRUTE-FORCE PROTECTOR                (50 min + 10)       ║
║    Qué hace: detecta y previene intentos de fuerza bruta SSH           ║
║    Fuente: logread (autenticación BusyBox) + conntrack                   ║
║    Algoritmo: ≥5 intentos desde misma IP en 10 s                         ║
║    Umbral: 5 intentos / 10 s (configurable)                              ║
║    Prueba en vivo: `hydra -l root -P rockyou.txt -t 10 ssh://IP`       ║
║    Lo que aprendes: leer logs del sistema como fuente de datos           ║
║                                                                          ║
║  ► LAB 5.5 · DoS CONNECTION LIMITER                   (50 min + 10)       ║
║    Qué hace: limita conexiones simultáneas por IP                       ║
║    Fuente: conntrack (contar por IP origen)                              ║
║    Algoritmo: conteo de SYN activos; si >límite → alerta + Optionally  ║
║               instalar regla iptables de límite                         ║
║    Umbral: 20 conexiones simultáneas (configurable)                     ║
║    Prueba en vivo: 50 conexiones paralelas con `ab` o `hping3`         ║
║    Lo que aprendes: diferencia entre DETECTAR y BLOQUEAR                 ║
║                     (una app puede hacer ambas cosas)                    ║
║                                                                          ║
║  ► LAB 5.6 · WiFi CHANNEL OPTIMIZER                   (50 min + 10)       ║
║    Qué hace: analiza espectro y cambia canal automáticamente             ║
║    Fuente: `iw dev wlan0 survey dump`, `iwinfo wlan0 scan`              ║
║    Algoritmo: puntuar canal por (#APs vecinos + RSSI) y elegir el mejor  ║
║    Schedule: ejecutar según ventana horaria configurada                  ║
║    Prueba en vivo: ver cambio de canal en `iwinfo wlan0 info`           ║
║    Lo que aprendes: la app más "inteligente" del catálogo —             ║
║                     decisión automática, no solo alerta                  ║
║                                                                          ║
║  ► CIERRE DEL MÓDULO 5 (10 min)                                          ║
║    Tablero comparativo: las 6 apps, sus fuentes de datos, sus umbrales   ║
║                                                                          ║
║  ENTREGA: 6 apps de ACCIÓN, cada una compilada, desplegada,             ║
║           auditada, y probada contra ataque real en vivo.                ║
╚══════════════════════════════════════════════════════════════════════════╝
```

```
╔══════════════════════════════════════════════════════════════════════════╗
║  MÓDULO 6 · LABORATORIOS DE NOTIFICACIÓN               5 horas       ║
║  "Las 9 herramientas de monitoreo del marketplace"                       ║
║  (Cada app: 30 min · 5 min de prueba en vivo)                            ║
╠══════════════════════════════════════════════════════════════════════════╣
║                                                                          ║
║  ► LAB 6.1 · CPU MONITOR ALERT                     (30 min + 5)        ║
║    Qué hace: monitorea %CPU y alerta cuando supera umbral               ║
║    Fuente: /proc/stat con delta de 2 muestras (¡64-bit! ver I-002)      ║
║    Umbral: configurable, con histéresis (entrada 80%, salida 70%)        ║
║    Prueba: generar carga con `stress`/`dd` y ver la alerta subir          ║
║                                                                          ║
║  ► LAB 6.2 · RAM USAGE NOTIFIER                     (30 min + 5)        ║
║    Qué hace: monitorea RAM, alerta en niveles críticos, sugiere acción  ║
║    Fuente: /proc/meminfo → usar MemAvailable, NO MemFree                ║
║    Umbral: configurable; incluye análisis de "top 5 procesos"           ║
║    Prueba: `dd` para reservar memoria y ver la alerta                    ║
║                                                                          ║
║  ► LAB 6.3 · CELLULAR SIGNAL MONITOR                (30 min + 5)        ║
║    Qué hace: monitorea SINR y calidad de señal celular                   ║
║    Fuente: gsmctl -q → RSSI, RSRP, SINR, RSRQ                          ║
║    Algoritmo: clasificar calidad (Good/Average/Poor) por umbrales       ║
║    Prueba: verificar clasificación correcta en el router real            ║
║    ⚠ Nota: el proyecto semilla NO tiene sensor de CPU; esta app         ║
║      measures señal, que SÍ está disponible.                            ║
║                                                                          ║
║  ► LAB 6.4 · CABLE CONNECTION ALERT                 (30 min + 5)        ║
║    Qué hace: alerta inmediata al conectar/desconectar cable de red      ║
║    Fuente: /sys/class/net/eth*/operstate + carrier                       ║
║    Algoritmo: polling + detección de flanco (subida/bajada)             ║
║    Prueba: desconectar/conectar el cable FÍSICAMENTE                    ║
║    Lo que aprendes: flancos vs niveles en un estado booleano             ║
║                                                                          ║
║  ► LAB 6.5 · WiFi NEW USER ALERT                   (30 min + 5)        ║
║    Qué hace: alerta cuando se conecta un dispositivo nuevo al WiFi      ║
║    Fuente: iwinfo wlan0 assoclist → MACs asociadas                     ║
║    Algoritmo: diff contra whitelist/baseline persistido                  ║
║    Prueba: conectar un teléfono y ver la alerta con su MAC              ║
║    Lo que aprendes: detección de "nuevo" requiere memoria de estado      ║
║                                                                          ║
║  ► LAB 6.6 · LOGIN ATTEMPT MONITOR                  (30 min + 5)        ║
║    Qué hace: monitorea intentos de login, detecta fuerza bruta          ║
║    Fuente: logs del sistema (autenticación SSH + web UI)                 ║
║    Umbral: configurable; clasifica sospechoso vs legitimo                ║
║    Prueba: 10 logins fallidos y ver la alerta                            ║
║                                                                          ║
║  ► LAB 6.7 · DISK USAGE ALERT                       (30 min + 5)        ║
║    Qué hace: monitorea espacio en disco, alerta si bajo                  ║
║    Fuente: statvfs() en /overlay y /usr/local + análisis top archivos  ║
║    Prueba: crear archivos hasta bajar del umbral                         ║
║    Lo que aprendes: el overlay de 16 MB es pequeño — la flash es        ║
║                     el recurso escaso de un router Teltonika              ║
║                                                                          ║
║  ► LAB 6.8 · TEMPERATURE MONITOR PRO                (30 min + 5)        ║
║    Qué hace: monitorea temperatura, alerta por sobrecalentamiento       ║
║    Fuente: gsmctl -c (temp del MÓDEM) — el MT7628AN no expone CPU        ║
║    ⚠ CONCEPTO CLAVE: el SoC no tiene sensor térmico; la temperatura     ║
║      reportada es del módem. Documentarlo honestamente es parte         ║
║      de la calidad técnica.                                              ║
║    Prueba: verificarUmbrales con temperatura ambiente real               ║
║                                                                          ║
║  ► LAB 6.9 · SYSTEM FILE MONITOR                    (30 min + 5)        ║
║    Qué hace: vigila archivos críticos, detecta cambios no autorizados   ║
║    Fuente: hash de /etc/passwd, /etc/shadow, /etc/config/* + mtime     ║
║    Algoritmo: guardar baseline de hashes, comparar, alertar si cambia  ║
║    Prueba: modificar un archivo de config y ver la alerta                ║
║    Lo que aprendes: integridad de archivos (concepto de seguridad)       ║
║                                                                          ║
║  ► CIERRE DEL MÓDULO 6 (10 min)                                          ║
║    Tablero: las 9 apps, sus fuentes, sus umbrales, su periodicidad      ║
║                                                                          ║
║  ENTREGA: 9 apps de NOTIFICACIÓN, cada una compilada, desplegada,        ║
║           auditada y probada en vivo.                                    ║
╚══════════════════════════════════════════════════════════════════════════╝
```

```
╔══════════════════════════════════════════════════════════════════════════╗
║  MÓDULO 7 · CALIDAD, EMPAQUETADO Y OPERACIÓN          3 horas       ║
║  "De 'funciona en mi mesa' a 'funciona en producción'"                   ║
╠══════════════════════════════════════════════════════════════════════════╣
║ 7.1  Robustez de producción (0:00–0:45)                                  ║
║      · Manejo de SIGTERM: cierre limpio, no dejar sockets colgados      ║
║      · Reconexión con backoff ante pérdida de red                        ║
║      · Timeouts: ninguna llamada puede colgar el servicio para siempre  ║
║      · Qué hacer cuando un comando externo falla (degradar, no morir)     ║
║      · Reinicio seguro: procd respawn con límites anti-bucle             ║
║                                                                          ║
║ 7.2  Seguridad del código (0:45–1:20)                                   ║
║      · Credenciales fuera del binario (archivo .conf con chmod 600)     ║
║      · Nunca loguear la password                                          ║
║      · .gitignore: qué NUNCA se versiona (binarios, secretos, SDK)       ║
║      · Auditoría: el agente busca la fuga antes que tú                    ║
║      · Validación de inputs (no confiar en nada externo)                 ║
║                                                                          ║
║ 7.3  Rendimiento en un recurso escaso (1:20–1:50)                        ║
║      · 128 MB RAM: presupuesto de memoria de cada app                    ║
║      · Enlazado estático: por qué el binario es autónomo y portable      ║
║      · Verificar tamaño con `ls -la` (debe ser pequeño)                  ║
║      · Evitar fugas: buffers estáticos, no malloc en loop                 ║
║      · Profiling barato: /proc/self/status VmRSS                         ║
║                                                                          ║
║ 7.4  Empaquetado y despliegue de las 15 apps (1:50–2:30)                 ║
║      · Un Makefile que compila las 15 de una vez                          ║
║      · make → make deploy-all → make status-all                          ║
║      · Instalar los 15 init.d con START escalonado (evitar pico)         ║
║      · Considerar que 15 procesos en un MIPS de 580 MHz: planificamos   ║
║      · Desinstalación limpia: make uninstall-all                         ║
║                                                                          ║
║ 7.5  La auditoría como Ritual de calidad (2:30–2:50)                     ║
║      · Qué revisa el agente auditor-teltonika                            ║
║      · Interpretar un informe APROBADO vs REQUIERE CORRECCIÓN           ║
║      · Incidencia típica: buffer de 1024 que trunca con >4 clientes      ║
║      · El bucle virtuous: implementar → auditar → corregir → reauditar  ║
║                                                                          ║
║ 7.6  Operación en el mundo real (2:50–3:00)                              ║
║      · Gestión remota por ZeroTier (guía real incluida en el repo)       ║
║      · Cómo diagnosticar cuando algo deja de funcionar                  ║
║      · Actualización segura de una app en producción                     ║
║                                                                          ║
║ LAB 7 · PRODUCCIÓN       Entregable: las 15 apps en producción, con       ║
║                          informe de auditoría APROBADO cada una         ║
╚══════════════════════════════════════════════════════════════════════════╝
```

---

# 4. DESGLOSE HORARIO

| Módulo | Tema | Horas | Tipo |
|---|---|---|---|
| 0 | Inmersión: qué hay dentro del router | 2 | Teoría + tour guiado |
| 1 | Ecosistema de agentes de IA | 3 | Práctica guiada |
| 2 | Primer binario dentro del router | 4 | **Laboratorio** |
| 3 | RutOS por dentro: sysfs, uci, ubus, gsmctl | 4 | Teoría + práctica |
| 4 | labcore: la librería núcleo | 4 | **Laboratorio** |
| 5 | 6 labs de ACCIÓN (seguridad y red) | 5 | **Laboratorio** |
| 6 | 9 labs de NOTIFICACIÓN (monitoreo) | 5 | **Laboratorio** |
| 7 | Calidad, empaquetado, producción | 3 | **Laboratorio** |
| | **TOTAL** | **30** | 21 h de laboratorio |

```
Barra de horas:

  M0  ██                 2h  teoría
  M1  ███                3h  práctica guiada
  M2  ████               4h  LABORATORIO
  M3  ████               4h  teoría + práctica
  M4  ████               4h  LABORATORIO
  M5  █████              5h  LABORATORIO
  M6  █████              5h  LABORATORIO
  M7  ███                3h  LABORATORIO
      ─────────────────────
      21 h de laboratorio / 30 h  =  70% experimental
```

---

# 5. MATERIAS DEL CURSO

```
╔══════════════════════════════════════════════════════════════════════════╗
║  MATERIA 1 · PLATAFORMA           Fundamentos de routers y RutOS         ║
║  · Arquitectura de un router (SoC, RAM, flash, overlay)                 ║
║  · OpenWrt y RutOS: el sistema operativo del router                     ║
║  · BusyBox, sysfs, procfs, ubus, uci                                  ║
║  · Herramientas Teltonika: gsmctl, iwinfo                             ║
╠══════════════════════════════════════════════════════════════════════════╣
║  MATERIA 2 · PROGRAMACIÓN EMBEBIDA  C para recursos escasos              ║
║  · Estructura de un demonio: init / loop / cleanup                     ║
║  · Manejo de señales POSIX (SIGTERM, SIGPIPE, SIGINT)                  ║
║  · Logging estructurado a stderr (procd lo captura)                    ║
║  · Guardas de buffer: snprintf SIEMPRE                                 ║
║  · Enteros de 64 bits en MIPS32 (el bug de /proc/stat)                  ║
║  · popen seguro vs system()                                            ║
╠══════════════════════════════════════════════════════════════════════════╣
║  MATERIA 3 · PLATAFORMA DE COMPILACIÓN  Toolchain y cross-compilación  ║
║  · El SDK de OpenWrt 21.02 ramips/mt76x8                               ║
║  · mipsel-openwrt-linux-musl-gcc 8.4.0                                ║
║  · Enlazado estático y por qué es obligatorio                           ║
║  · Verificación con `file` (el paso que salva el curso)                 ║
║  · Transferencia vía scp, gestión con variables de entorno              ║
║  · Despliegue seguro y coexistencia de versiones                       ║
╠══════════════════════════════════════════════════════════════════════════╣
║  MATERIA 4 · SERVICIOS Y EMPAQUETADO                                  ║
║  · El problema del proceso que muere al cerrar SSH                      ║
║  · Scripts init.d y procd (START, respawn, STOP)                       ║
║  · Logs con logread                                                   ║
║  · Configuración externa (archivo .conf con chmod 600)                 ║
║  · El patrón "deploy sin ejecutar" para producción segura              ║
╠══════════════════════════════════════════════════════════════════════════╣
║  MATERIA 5 · PROTOCOLOS DE TELEMETRÍA                                  ║
║  · MQTT 3.1.1 implementado byte a byte                                ║
║  · CONNECT / CONNACK / PUBLISH / SUBSCRIBE / PINGREQ                   ║
║  · Remaining Length: la codificación de entero variable                ║
║  · JSON: construcción y escapado sin librerías                        ║
║  · QoS, keepalive, clean session                                        ║
║  · Backoff exponencial para reconexión                                 ║
╠══════════════════════════════════════════════════════════════════════════╣
║  MATERIA 6 · SISTEMAS DE ALERTA                                       ║
║  · Umbrales de entrada vs umbral de salida (histéresis)                ║
║  · Debounce temporal: no bombardear                                     ║
║  · Sliding window: contar eventos en una ventana de tiempo              ║
║  · Máquinas de estado: NORMAL → ALERTA → RECUPERACIÓN                  ║
║  · Por qué esto es lo que separa una app útil de una ruidosa          ║
╠══════════════════════════════════════════════════════════════════════════╣
║  MATERIA 7 · DETECCIÓN DE EVENTOS Y SEGURIDAD                          ║
║  · /proc/net/nf_conntrack como fuente de verdad de conexiones          ║
║  · Parseo de logs del sistema para detectar intentos de acceso         ║
║  · Integridad de archivos: baseline de hashes                          ║
║  · Detección depectrum WiFi y decisión automática de canal             ║
║  · Detectar vs bloquear: la decisión de diseño clave                   ║
╠══════════════════════════════════════════════════════════════════════════╣
║  MATERIA 8 · CALIDAD DE PRODUCCIÓN                                    ║
║  · Auditoría estática de código (buffers, fugas, hilos, secretos)      ║
║  · Gestión de memoria en 128 MB                                        ║
║  · Gestión de errores: degradar, no morir                              ║
║  · Actualización segura y rollback                                     ║
║  · Gestión remota de flotas (ZeroTier)                                 ║
╚══════════════════════════════════════════════════════════════════════════╝
```

---

# 6. ENTREGABLES FINALES DEL CURSO

Al completar los 30 horas, el alumno entrega y se queda con:

| # | Entregable | Cantidad |
|---|---|---|
| 1 | Aplicaciones funcionales compiladas para MIPS | **15** |
| 2 | Librería núcleo `labcore` reutilizable | 1 (9 módulos) |
| 3 | Scripts de servicio procd | **15** |
| 4 | Archivos de configuración de ejemplo (`.conf.example`) | **15** |
| 5 | Archivos Makefile (build + deploy + uninstall) | 1 maestro + 15 |
| 6 | Documentación de usuario (datasheets) | **15** |
| 7 | Documentación de desarrollador (diseño + código) | **15** |
| 8 | Informes de auditoría estática APROBADO | **15** |
| 9 | Evidencia de prueba en laboratorio (logs de detección real) | **15** |
| 10 | README del proyecto con guía de operación | 1 |
| 11 | Prompts reutilizables del alumno | 5 |

---

# 7. CORRELACIÓN DIRECTA CON rutosapp.com

Cada aplicación del marketplace tiene su laboratorio equivalente. La **especificación funcional**
del producto es el enunciado del ejercicio; el alumno debe construir la solución.

## 7.1 Sección ACCIONES (6)

| Producto rutosapp | Versión | Laboratorio | Coincedence |
|---|---|---|---|
| WiFi Channel Optimizer | v2.2.0 | LAB 5.6 | 100% |
| Port Scan Detector | v1.0.0 | LAB 5.1 | 100% |
| ICMP Flood Detector | v1.0.0 | LAB 5.2 | 100% |
| Web Attack Detector | v1.0.0 | LAB 5.3 | 100% |
| SSH Brute Force Protector | v1.0.0 | LAB 5.4 | 100% |
| DoS Connection Limiter | v1.0.0 | LAB 5.5 | 100% |

## 7.2 Sección NOTIFICACIONES (9)

| Producto rutosapp | Versión | Laboratorio | Coincidencia |
|---|---|---|---|
| CPU Monitor Alert | v1.2.0 | LAB 6.1 | 100% |
| RAM Usage Notifier | v1.4.2 | LAB 6.2 | 100% |
| Cellular Signal Monitor | v1.0.0 | LAB 6.3 | 100% |
| Cable Connection Alert | v1.0.5 | LAB 6.4 | 100% |
| WiFi New User Alert | v1.3.1 | LAB 6.5 | 100% |
| Login Attempt Monitor | v2.0.1 | LAB 6.6 | 100% |
| Disk Usage Alert | v1.1.3 | LAB 6.7 | 100% |
| Temperature Monitor Pro | v1.5.0 | LAB 6.8 | 100% |
| System File Monitor | v1.0.0 | LAB 6.9 | 100% |

**Total: 15/15 productos del marketplace = 15 laboratorios.**

Cada ficha técnica (fuente de datos, algoritmo, umbrales, alertas, prueba en vivo) está en
[`CATALOGO-LABS.md`](./CATALOGO-LABS.md).

---

# 8. REQUISITOS DEL ENTORNO

## 8.1 Hardware

| Elemento | Especificación |
|---|---|
| Router | Teltonika RUT200 (o equivalente RUT2xx) |
| PC de desarrollo | Linux (Ubuntu/Debian) x86-64 |
| Red | PC y router en el mismo segmento (cable o WiFi) |
| Opcional | Segundo equipo para generar ataques desde otra IP |

## 8.2 Software

| Herramienta | Para qué |
|---|---|
| SDK OpenWrt 21.02 ramips/mt76x8 | Cross-compilación |
| `make`, `gcc` (host) | Build |
| `ssh`, `scp`, `sshpass` | Transferencia y operación |
| `mosquitto_sub` (host) | Verificar publicaciones MQTT |
| `nmap`, `hping3`, `hydra` (host) | Generar ataques reales en los labs |
| Agentes de IA Teltonika |Assistencia técnica |

---

# 9. DOCUMENTACIÓN DEL CURSO

| Documento | Contenido |
|---|---|
| [`TEMARIO.md`](./TEMARIO.md) | Este documento: temario, horas, entregables |
| [`CATALOGO-LABS.md`](./CATALOGO-LABS.md) | Ficha técnica de las 15 aplicaciones |
| [`ARQUITECTURA-LABCORE.md`](./ARQUITECTURA-LABCORE.md) | Diseño de la librería núcleo |
| [`AGENTES-IA.md`](./AGENTES-IA.md) | Protocolo de uso de los agentes |

Y en la raíz del proyecto:

| Documento | Contenido |
|---|---|
| [`README.md`](../README.md) | Proyecto semilla lp104mqttv2 (el "hello world" del curso) |
| [`../docs/datasheet.md`](../docs/datasheet.md) | Manual de usuario del proyecto semilla |
| [`../docs/developer.md`](../docs/developer.md) | Documentación de desarrollo del proyecto semilla |
| [`../docs/zerotier-guide.md`](../docs/zerotier-guide.md) | Guía de gestión remota de flotas |

---

# 10. CIERRE

Al salir de este curso, el alumno no solo sabe programar aplicaciones para routers Teltonika.

**Sabe:**

- ✅ Que un router es un computador Linux completo, y por eso se puede programar
- ✅ Que existe una ruta desde "una idea" hasta "un servicio corriendo en 128 MB de RAM"
- ✅ Que un toolchain se descarga, se configura y se usa
- ✅ Que MQTT se puede hablar byte a byte
- ✅ Que un umbral bien puesto evita el ruido de alertas
- ✅ Que un detector de ataques se prueba **generando el ataque**
- ✅ Que la IA no sustituye el criterio: **verifica siempre en el router**

**Y lo más importante:**

- ✅ Tiene **15 aplicaciones funcionando**, cada una probada contra ataque real en su laboratorio
- ✅ Tiene **el código fuente, la librería, la documentación y las pruebas** de todo eso
- ✅ Tiene los **agentes** para seguir construyendo sin volver a este curso

Este temario no es una lista de temas. Es una ruta de 30 horas que termina con un portfolio
técnico real, en un nicho de mercado que las grandes frameworks aún no están cubriendo.

---

**Código de curso:** `TELTONIKA-APPDEV-30H`
**Referencia técnica:** RUT200 · MT7628AN · mipsel_24kc · musl · OpenWrt 21.02 · RutOS
**Contacto:** jmadasme@gmail.com
**Licencia:** Material de uso formativo. Las aplicaciones son implementaciones propias en C de
especificaciones funcionales publicadas; los binarios propietarios de rutosapp.com no se
distribuyen.

---

<div align="center">

**30 horas · 8 módulos · 15 laboratorios · 100% verificado en hardware real**

</div>