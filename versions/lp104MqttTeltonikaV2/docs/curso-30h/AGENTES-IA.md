# AGENTES DE IA — EL PROTOCOLO DEL CURSO

## Cómo se usan los agentes preentrenados en cada fase del curso

---

## 1. PRINCIPIO RECTOR

> **El agente de IA no reemplaza el criterio del alumno: lo amplifica.**
>
> El agente conoce el SDK, el sysfs, el procd, los bugs de la plataforma y los errores
> comunes. El alumno sabe lo que quiere que la app haga y es el responsable de verificar
> que lo haga.
>
> **La IA es el acelerador. La verificación en el router es la verdad.**

---

## 2. LOS SEIS AGENTES

| Agente | Especialidad | Tipo | Cuándo invocarlo |
|---|---|---|---|
| `orquestador-teltonika` | Conduce el pipeline completo | orquestador | Al inicio de cada lab, para el flujo general |
| `analista-teltonika` | Especificación técnica previa al código | trabajador | **Siempre antes de escribir código** |
| `implementador-teltonika` | Escribe, compila y transfiere | trabajador | Después de la especificación |
| `auditor-teltonika` | Auditoría estática completa | trabajador | **Siempre antes de dar por terminado** |
| `teltonika-info` | Investigación en wiki oficial y comunidad | consultivo | Cuando hay un bloqueo específico |
| `documentador-teltonika` | README, datasheet, docs de dev | trabajador | Al cierre de cada módulo |

---

## 3. EL CICLO DE 6 PASOS (con prompts reales)

### PASO 1 — ESPECIFICAR (`analista-teltonika`)

El agente produce una ficha: fuentes de datos, algoritmo, umbrales, alertas, riesgos.

**Prompt del alumno:**

````
Actúa como analista-teltonika. Especifica la mini-aplicación
"CPU MONITOR ALERT".

Contexto de hardware (obligatorio incluirlo siempre):
  - Router: Teltonika RUT200
  - SoC: MediaTek MT7628AN, MIPS little-endian, 580 MHz
  - Target: ramips/mt76x8, arquitectura mipsel_24kc
  - libc: musl (RutOS RUT2M_R_00.07.22.3, OpenWrt 21.02)
  - RAM: 128 MB, flash: 16 MB SPI NOR

Requisitos funcionales:
  1. Monitorear % de uso de CPU en tiempo real
  2. Alertar cuando supere un umbral configurable
  3. Incluir series históricas para ver tendencia
  4. Reportar cada 60s aunque no haya cambio

Entrega:
  - Fuente de datos exacta (ruta del sistema) y formato de la salida
  - Algoritmo de cálculo, con la aritmética escrita
  - Campos del JSON de la alerta
  - Variables de configuración con nombre, tipo y default
  - Riesgos técnicos específicos de esta plataforma
  - 5 casos de prueba

No escribas código todavía.
````

**Qué aprende el alumno:** a especificar antes de construir. El error #1 de los
programadores sin experiencia es saltar directo al código.

---

### PASO 2 — CONSTRUIR (`implementador-teltonika`)

**Prompt del alumno:**

````
Actúa como implementador-teltonika. Implementa el CPU Monitor Alert
según la especificación que sigue.

[PEGAR LA ESPECIFICACIÓN DEL PASO 1]

Requisitos obligatorios de calidad:
  - C puro, sin librerías externas. Enlazar estático (-static)
  - Toolchain: mipsel-openwrt-linux-musl-gcc 8.4.0 (SDK OpenWrt 21.02 ramips/mt76x8)
  - -Wall -Wextra, cero warnings
  - PROHIBIDO strcpy/strcat/sprintf sobre buffer de tamaño fijo.
    Usar snprintf con el límite del buffer, siempre.
  - PROHIBIDO system(). Si necesitás un comando, usá popen()
    SOLO con cadenas constantes (sin datos externos interpolados).
  - Manejo de SIGTERM/SIGINT para cierre limpio
  - Log a stderr con timestamp y niveles (procd lo captura)
  - Configuración desde archivo /etc/labNN.conf (clave=valor),
    NO compilada en el binario
  - Los contadores de /proc/stat deben ser unsigned long long
    (en MIPS32, strtoul SATURA y el delta se rompe a los ~497 días)

Entrega:
  1. El archivo .c completo
  2. El script init.d procd correspondiente
  3. El .conf.example
  4. Las órdenes exactas de compilación
  5. Qué verificar después de compilar (incluyendo `file`)
````

---

### PASO 3 — TRANSFERIR (`implementador-teltonika`)

```
1. make clean && make
2. file ./cpu-monitor-alert
   → DEBE decir: ELF 32-bit LSB executable, MIPS, MIPS32 rel2,
     statically linked, stripped
   → Si dice x86-64: el toolchain no se está usando. PARÁ.
3. ls -la → el binario debe pesar menos de 150 KB
4. scp al router (destino: /usr/local/bin/)
5. chmod +x
6. Copiar el .conf.example → .conf, editar con credenciales reales, chmod 600
7. NO arrancar todavía: probar en foreground primero
```

**Prevención de incidentes (lección I-003):** las credenciales **nunca** van en el `Makefile`
versionado. Van solo en el `.conf` local, gitignored.

---

### PASO 4 — PROBAR EN VIVO (el alumno, con apoyo del agente)

```bash
# En el router, foreground primero
/cpu-monitor-alert

# En otro terminal: generar carga
while true; do dd if=/dev/zero bs=1M count=500 2>/dev/null | md5sum; done

# Ver la alerta en el broker (desde el PC)
mosquitto_sub -h broker -t 'teltonika/alerts/cpu' -v
```

**Si algo no funciona**, el alumno describe el síntoma al agente en vez de adivinar:

````
La app arranca pero no publica nada. Logs:
[2026-10-02 14:23:01] [INICIO] cpu-monitor-alert — broker www.smartage.cl:1886, intervalo 60s
[2026-10-02 14:23:01] [INICIO] usuario MQTT: luisHernandez
[2026-10-02 14:23:31] [PING] PINGREQ enviado (keepalive)
(nada más en 10 minutos)

Datos: el ping al broker responde, el mismo script MQTT del proyecto
semilla lp104mqttv2 SÍ publica. Config: user/pass cargados desde
/etc/cpu-monitor-alert.conf (chmod 600).

Analizá las causas probables en orden de likelihood y decime qué
verificar primero en el router.
````

---

### PASO 5 — AUDITAR (`auditor-teltonika`)

**Siempre antes de considerar el lab terminado.**

````
Actúa como auditor-teltonika. Revisá estáticamente el código de
CPU Monitor Alert.

Contexto: C embebido, MIPS 32-bit (mipsel_24kc), musl, binario
enlazado estático, corre como servicio procd en un RUT200.

Dimensiones a cubrir:
  1. INTEGRIDAD DE BUFFERS: todos los snprintf con límite correcto?
     ¿buffers del tamaño suficiente para el peor caso real?
  2. ARITMÉTICA: contadores de /proc/stat como unsigned long long?
     ¿otros lugares donde un overflow de 32 bits rompe el cálculo?
  3. GESTIÓN DE ERRORES: ¿cada llamada a popen/fopen/socket verifica
     el retorno? ¿hay degradación o la app muere?
  4. SEGURIDAD: ¿hay secretos en el código, en el Makefile o en el
     log? ¿el popen usa datos externos? ¿se loguea la password?
  5. RECURSOS: ¿el proceso se cierra limpiamente con SIGTERM?
     ¿los file descriptors se liberan?
  6. PLATAFORMA: ¿el binario será MIPS? ¿el código depende de
     algo que no existe en un router de 128 MB de RAM?

Entrega: informe con INCIDENCIAS numeradas (I-001, I-002, ...), cada
una con severidad (ALTA/MEDIA/BAJA), ubicación exacta (línea),
causa raíz y fix concreto.

Si no hay incidencias: APROBADO — SIN INCIDENCIAS.
````

---

### PASO 6 — DOCUMENTAR (`documentador-teltonika`)

````
Actúa como documentador-teltonika. Generá la documentación del lab
"CPU Monitor Alert":

1. DATASHEET (usuario): qué hace, cómo configurar cada variable,
   qué significa cada alerta, cómo diagnosticar problemas.
2. DOC DE DESARROLLO: arquitectura, fuente de datos, algoritmo
   con las fórmulas escritas, complejidad, y el código explicado
   función por función con las decisiones de diseño.
3. README del proyecto: descripción, instalación, operación,
   diagrama, advertencias.

Todo debe estar en español técnico, con tablas y ejemplos reales
de logs de esta app específica.
````

---

## 4. CASOS DE USO DE `teltonika-info` (investigación)

Cuando hay un **bloqueo concreto** que el `implementador` no resuelve, se consulta
`teltonika-info`, que investiga en fuentes oficiales:

````
Investigación necesaria: en el RUT200 (RutOS RUT2M_R_00.07.22.3,
kernel 5.15.197), el comando `gsmctl -q` devuelve:

RSSI: -60
RSRP: -103
SINR: 2
RSRQ: -16

Pregunta: ¿es el formato estable entre versiones de RutOS? ¿Qué
pasa si el router está en 2G y devuelve solo un entero? ¿Cuál es el
comando correcto para la temperatura del módem y cuál es el de tipo
de conexión?

Fuentes: wiki.teltonika-networks.com, GPL Code Center, comunidad.
Devolvé: causa raíz, respuesta accionable, referencias, nivel de
confianza.
````

---

## 5. ESTRUCTURA DE UN BUEN PROMPT (las 5 partes)

| Parte | Para qué | Ejemplo |
|---|---|---|
| **1. Rol** | Activar el conocimiento especializado del agente | "Actúa como implementador-teltonika" |
| **2. Contexto** | Modelo, versión, arquitectura, libc | "RUT200, MT7628AN, mipsel_24kc, musl, OpenWrt 21.02" |
| **3. Objetivo** | Qué tiene que hacer, con precisión | "Alertar cuando CPU pase 80% durante 10 s" |
| **4. Restricciones** | Qué NO debe hacer, y cómo | "C puro, sin librerías; -Wall -Wextra; sin secrets" |
| **5. Formato de salida** | Cómo entrega el resultado | "El .c completo + init.d + .conf.example + comandos" |

**Sin el punto 2, el agente da respuestas genéricas de Linux x86-64 y el resultado no
compila en el router.** El contexto de plataforma no es opcional: es lo que hace la
diferencia entre un prompt que funciona y uno que no.

---

## 6. CUANDO EL AGENTE SE EQUIVOCA

La IA specialized **se equivoca**. Ocurre. Conocer sus modos de fallo es parte del curso.

| Modo de fallo | Frecuencia | Cómo detectarlo | Qué hacer |
|---|---|---|---|
| Arquitectura equivocada (da ARM para RUT200) | media | `file` no dice MIPS | Reafirmar el contexto explícitamente |
| Ruta que no existe en tu modelo | media | `ls` da error en el router | Verificar con `ls` antes de usar |
| Olvida una restricción | baja | Revisar la salida contra la lista | Repetir la restricción explícitamente |
| Inventa una API inexistente | baja | El compilador falla | Verificar contra la wiki |
| No conoce un comando de RutOS | baja | El comando falla en el router | Consultar `teltonika-info` |

**Regla del curso:** el agente **siempre** se verifica. `file`, `make`, `logread` y la prueba
en vivo son la verdad final.

---

## 7. MÉTRICAS DEL USO DE AGENTES

El curso mide cómo el alumno usa la herramienta:

| Métrica | Qué mide |
|---|---|
| Ciclos de iteración por lab | Eficiencia (ideal: 2–4) |
| Hallazgos del auditor encontrados por el alumno | Autonomía |
| Invocaciones de `teltonika-info` | Cuándo pedir ayuda externa |
| % de prompts con contexto de plataforma | Disciplina |
| Errores de compilación sin verificar | Madurez |

---

## 8. RESPONSABILIDAD DEL ALUMNO

> **El agente hace el trabajo. El alumno responde por el resultado.**
>
> Si un lab falla en el cliente del alumno, el problema es del alumno. La IA es una
> herramienta: amplifica lo que el alumno sabe y lo que el alumno verifica. Un alumno que
> copia la salida del agente sin entenderla no aprendió nada: solo aprendió a copiar.

El curso evalúa **entender**, no **copiar**. Por eso cada lab termina con:

1. **Prueba en vivo** (el agente no puede hacerlo por el alumno)
2. **Auditoría** (el agente detecta lo que el alumno no vio)
3. **Documentación** (el alumno debe poder explicarlo)

---

**Volver al:** [`TEMARIO.md`](./TEMARIO.md) · [`CATALOGO-LABS.md`](./CATALOGO-LABS.md) · [`ARQUITECTURA-LABCORE.md`](./ARQUITECTURA-LABCORE.md)