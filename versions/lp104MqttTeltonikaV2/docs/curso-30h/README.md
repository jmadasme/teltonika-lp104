# CURSO: PROGRAMACIÓN DE MINI-APLICACIONES PARA ROUTERS TELTONIKA

## 30 horas · 8 módulos · 15 laboratorios · RUT200 / MIPS · Asistido por agentes de IA

---

## Inicio rápido

| Documento | Qué contiene |
|---|---|
| **[`TEMARIO.md`](./TEMARIO.md)** | Temario completo hora a hora, entregables, requisitos, correlación con rutosapp |
| **[`CATALOGO-LABS.md`](./CATALOGO-LABS.md)** | Ficha técnica de las 15 apps: fuentes de datos, algoritmos, umbrales, pruebas en vivo |
| **[`ARQUITECTURA-LABCORE.md`](./ARQUITECTURA-LABCORE.md)** | Diseño de la librería núcleo compartida por las 15 apps |
| **[`AGENTES-IA.md`](./AGENTES-IA.md)** | Protocolo de uso de los 6 agentes de IA, con prompts reales |

**Proyecto semilla del curso:** [`../../README.md`](../../README.md) — `lp104mqttv2`, cliente MQTT
en C puro para el RUT200, con el cliente MQTT hecho a mano y 4 métricas de telemetría. Es el
"hello world" del curso y la base de `labcore_mqtt`.

---

## Estructura del curso

```
M0 · Inmersión: qué hay dentro del router         2 h   ██
M1 · Ecosistema de agentes de IA                 3 h   ███
M2 · Primer binario dentro del router            4 h   ████  ◄── hora 6: primer programa corriendo
M3 · RutOS por dentro: sysfs, uci, ubus, gsmctl  4 h   ████
M4 · labcore: la librería núcleo                 4 h   ████
M5 · 6 labs de ACCIÓN (seguridad y red)          5 h   █████
M6 · 9 labs de NOTIFICACIÓN (monitoreo)          5 h   █████
M7 · Calidad, empaquetado, producción            3 h   ███
                                                  ─────────
                                                  30 h   (21 h de laboratorio = 70%)
```

---

## Los 15 laboratorios

**ACCIÓN — seguridad y red** (Módulo 5)

| # | Laboratorio | Producto rutosapp | Concepto que enseña |
|---|---|---|---|
| 1 | Port Scan Detector | v1.0.0 | Sliding window, conteo de puertos únicos |
| 2 | ICMP Flood Detector | v1.0.0 | Tasa vs cantidad absoluta |
| 3 | Web Attack Detector | v1.0.0 | Detección de burst por varianza |
| 4 | SSH Brute-Force Protector | v1.0.0 | Logs del sistema, bloqueo con expiración |
| 5 | DoS Connection Limiter | v1.0.0 | Detectar vs bloquear, limpieza de estado |
| 6 | WiFi Channel Optimizer | v2.2.0 | Función objetivo y decisión automática |

**NOTIFICACIÓN — monitoreo** (Módulo 6)

| # | Laboratorio | Producto rutosapp | Concepto que enseña |
|---|---|---|---|
| 7 | CPU Monitor Alert | v1.2.0 | Aritmética 64-bit en plataforma 32-bit |
| 8 | RAM Usage Notifier | v1.4.2 | Métrica semánticamente correcta (`MemAvailable`) |
| 9 | Cellular Signal Monitor | v1.0.0 | Parseo robusto, null vs cero |
| 10 | Cable Connection Alert | v1.0.5 | Flancos vs niveles |
| 11 | WiFi New User Alert | v1.3.1 | Detección de "nuevo" con memoria de estado |
| 12 | Login Attempt Monitor | v2.0.1 | Lectura incremental con rotación de logs |
| 13 | Disk Usage Alert | v1.1.3 | `statvfs`, bloques vs inodos |
| 14 | Temperature Monitor Pro | v1.5.0 | Verificar antes de asumir, ética de instrumentación |
| 15 | System File Monitor | v1.0.0 | Integridad de archivos, FAIL-SAFE |

**15/15 productos del marketplace rutosapp.com = 15 laboratorios.**

---

## Qué se entrega

| # | Entregable | Cantidad |
|---|---|---|
| 1 | Aplicaciones funcionales compiladas para MIPS | **15** |
| 2 | Librería núcleo `labcore` reutilizable | 1 (9 módulos) |
| 3 | Scripts de servicio procd | **15** |
| 4 | Archivos de configuración de ejemplo | **15** |
| 5 | Makefiles (build + deploy + uninstall) | 1 maestro + 15 |
| 6 | Documentación de usuario (datasheets) | **15** |
| 7 | Documentación de desarrollador (diseño + código) | **15** |
| 8 | Informes de auditoría estática APROBADO | **15** |
| 9 | Evidencia de prueba en laboratorio | **15** |
| 10 | Prompts reutilizables del alumno | 5 |

---

## Reglas de oro

1. **Sin requisitos previos.** No hace falta saber programar, ni Linux, ni OpenWrt, ni Teltonika.
2. **El agente de IA no reemplaza la verificación.** `file`, `make`, `logread` y la prueba en
   vivo son la verdad final.
3. **Ninguna app está terminada hasta que fue construida, analizada y probada en laboratorio
   por el alumno.**
4. **Los ataques se generan de verdad.** `nmap`, `hydra`, `hping3` contra tu propio router.
   No hay simulaciones.
5. **Los binarios propietarios de rutosapp.com no se distribuyen.** Se usa su especificación
   funcional como enunciado y se entrega una implementación propia en C.

---

## Requisitos técnicos

| Elemento | Requisito |
|---|---|
| Router | Teltonika RUT200 (o RUT2xx equivalente) |
| PC | Linux x86-64 en la misma red que el router |
| SDK | OpenWrt 21.02.0 ramips/mt76x8 (GCC 8.4.0, musl) |
| Herramientas | `make`, `ssh`, `scp`, `sshpass`, `mosquitto_sub` |
| Ataque (host) | `nmap`, `hping3`, `hydra`, `ab` |
| IA | Los 6 agentes de Teltonika |

---

**Código de curso:** `TELTONIKA-APPDEV-30H`
**Contacto:** jmadasme@gmail.com