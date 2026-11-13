#!/bin/sh
# 99-lp104-banner.sh — banner SSH estático (Teltonika RUT200)
#
# Se instala en /etc/profile.d/99-lp104-banner.sh. OpenWrt 21.02 sourcea
# /etc/profile.d/*.sh desde /etc/profile, de modo que este banner se muestra
# en cada login interactivo.
#
# Es 100% estático (solo un `cat` de texto embebido): sin red, sin comandos
# externos, sin esperas -> NO puede colgar el login.
#
# No revela el nombre ni la ubicación del programa; sí indica la identidad
# del objeto web (client id / tópicos) al que apunta la telemetría.
#
# Fallback (si /etc/profile no sourceara /etc/profile.d/*.sh):
#   añadir al final de /etc/banner la línea:  cat /etc/profile.d/99-lp104-banner.sh

cat <<'LP104_BANNER_EOF'

+=============================================================================+
|                                                                             |
|                             .   *   .   *   .                               |
|                          *    \  |  /    *                                  |
|                           .  --(*)--  .            jmadasme@gmail.com       |
|                          *    /  |  \    *                                  |
|                             '   *   '   *                                   |
|                                                                             |
+=============================================================================+
|  Este equipo publica telemetría MQTT cada 30 s:                             |
|    - señal celular (dBm)      - clientes WiFi conectados                    |
|    - uso de CPU (%)           - temperatura                                 |
|                                                                             |
|  Identidad del objeto web (client id):                                      |
|    6aaa978111a8892fd2927dcc                                                 |
|                                                                             |
|  Tópicos publicados:                                                        |
|    6aaa978111a8892fd2927dcc/report/online   -> "true"                       |
|    6aaa978111a8892fd2927dcc/report/status   -> JSON de métricas             |
+=============================================================================+

LP104_BANNER_EOF
