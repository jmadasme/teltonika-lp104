# Guía ZeroTier para flota de routers Teltonika (piloto validado)

> **Proyecto**: lp104MqttTeltonikaV2 — extensión de gestión remota por VPN mesh
> **Router**: Teltonika RUT200 (RutOS RUT2M_R_00.07.22.3, OpenWrt 21.02.0, `ramips/mt76x8`, `mipsel_24kc`)
> **Fecha del piloto**: 2026-09-16
> **Estado**: ✅ VALIDADO — el router se auto-recupera por completo tras reinicio

---

## 1. Objetivo

Gestionar centralizadamente una flota de **1200+ buses** con routers Teltonika por SSH remoto, sin necesidad de un servidor central propio ni abrir puertos en cada router. La solución elegida es **ZeroTier** (VPN mesh P2P): cada router se une a una red virtual y es alcanzable por SSH desde el notebook del administrador, sin importar dónde esté cada uno.

### Por qué ZeroTier y no WireGuard/IPsec/OpenVPN en la VPS

| Opción | Veredicto | Motivo |
|---|---|---|
| **ZeroTier mesh** | ✅ **RECOMENDADO** | No requiere servidor central que transporte datos (los root servers solo coordinan el handshake inicial); tráfico P2P directo entre nodos; el RUT200 ya tiene paquete `zerotier` en opkg + UI web (`vuci-app-zerotier-ui`); autorización por web (my.zerotier.com) |
| **WireGuard hub-and-spoke en VPS** | ❌ NO recomendado | La VPS de producción de smartage es Ubuntu 16.04 EOL (kernel 4.4 sin soporte nativo WireGuard); instalar módulos DKMS ahí arriesga romper el broker MQTT del cliente (contrato de 5 años en juego) |
| **IPsec/OpenVPN** | ⚠️ Posible pero complejo | Requieren servidor central y certificados; más frágil para 1200 nodos sin automatización |

**Diferencia clave mesh vs hub-and-spoke**: en hub-and-spoke todo el tráfico pasa por el servidor central (si muere, cae toda la flota). En mesh (ZeroTier) el tráfico es directo entre nodos; el "centro" solo autoriza, no transporta.

### Límites del plan free de ZeroTier
- Plan free: hasta **25 nodos** (suficiente para piloto).
- Para 1200 buses: plan de pago o **controller self-hosted** (ZerotierController en un VPS propio).

---

## 2. Lo que FUNCIONÓ (validado en hardware real)

### 2.1 Instalación del paquete
```sh
# En el router (RUT200), por SSH:
opkg update
opkg install zerotier
```
- Paquete: `zerotier 1.14.0-7`, 384 151 B.
- **IMPORTANTE**: en RutOS los binarios quedan en `/usr/local/usr/bin/`:
  - `/usr/local/usr/bin/zerotier-one`
  - `/usr/local/usr/bin/zerotier-cli`
  - `/usr/local/usr/bin/zerotier-idtool`
- Init: `/etc/init.d/zerotier` (procd, `START=90`). El script busca `PROG=/usr/bin/zerotier-one` con **fallback a `/usr/local$PROG`** — funciona.

### 2.2 Unirse a la red
```sh
# El daemon debe estar corriendo primero (procd lo arranca, o manual):
/etc/init.d/zerotier start

# Unirse a la red (ejemplo: red "srvs" = 233ccaac27bddf60):
/usr/local/usr/bin/zerotier-cli join 233ccaac27bddf60
```
- **Node ID del router**: `0f65f0a4a2`
- Tras el join, el nodo queda `ACCESS_DENIED` hasta autorizarlo en **my.zerotier.com** (Members → Authorize).
- La IP se asigna automáticamente por el controlador (en este caso: **192.168.194.229/24**).

### 2.3 Configuración PERSISTENTE (crítica para sobrevivir reinicios)
El `identity.secret` del daemon vive por defecto en `/var/run` (¡volátil!). Para persistirlo:

```sh
# 1. Rescatar el secreto completo del nodo ya autorizado:
cat /var/run/zerotier/lib/zerotier-one_<nodeid>_cfg*/identity.secret
#   (formato: <node_id>:0:<public>:<secret>)

# 2. Escribirlo en /etc/config/zerotier (persistente en overlay):
# /etc/config/zerotier
config instance 'zt_instance'
	option enabled '1'
	option secret '<secreto_completo_rescatado>'
	option node_id '0f65f0a4a2'

config network_zt_instance
	option enabled '1'
	option network_id '233ccaac27bddf60'
	option allow_global '1'
	option allow_managed '1'
	option allow_default '1'
```
> ⚠️ **El nombre de la sección importa**: el init de Teltonika deriva el tipo de red del nombre de instancia (`zt_instance` + `network_zt_instance`). Usar secciones **nombradas** como arriba.

### 2.4 Interfaz de red + firewall (los 2 ajustes que faltaron)
El daemon crea la interfaz `ztr4nxo42h` (nombre derivado del network id), pero **por defecto el firewall la rechaza** y netifd la derriba si se registra mal:

```sh
# a) Firewall: agregar la interfaz ZT a la zona LAN (ACCEPT):
uci add_list firewall.@zone[0].network='ztr4nxo42h'   # zona lan (input ACCEPT)
uci commit firewall
/etc/init.d/firewall reload

# b) Interfaz en /etc/config/network (¡con auto=1!):
uci set network.zt=interface
uci set network.zt.proto='none'
uci set network.zt.device='ztr4nxo42h'
uci set network.zt.auto='1'        # ← CRÍTICO: con auto=0 netifd la derriba
uci commit network
/etc/init.d/network reload
```
> ⚠️ **Trampa encontrada**: `option auto "0"` hace que netifd baje la interfaz tras el reload → `No route to host`. Debe ser `auto="1"`.

### 2.5 Verificación final (post-reinicio real)
```sh
# Estado del daemon:
/usr/local/usr/bin/zerotier-cli -D/var/run/zerotier/lib/zerotier-one_<nodeid>_cfg021350 status
# → 200 info 0f65f0a4a2 1.14.0 ONLINE

# Redes:
/usr/local/usr/bin/zerotier-cli -D... listnetworks
# → 233ccaac27bddf60 srvs OK PRIVATE ztr4nxo42h 192.168.194.229/24

# Interfaz con IP:
ip addr show ztr4nxo42h
# → inet 192.168.194.229/24 scope global ztr4nxo42h

# Desde el notebook:
ping 192.168.194.229        # → 0% pérdida
ssh root@192.168.194.229    # → SSH OK
```

---

## 3. Lo que NO funcionó (y cómo se resolvió)

### 3.1 Identidad en /var/run (volátil) — RESUELTO
**Problema**: el daemon manual guardaba `identity.secret` en `/var/run/zerotier/lib/...`, que se borra al reiniciar → el router cambiaba de node id y perdía autorización.
**Solución**: rescatar el secreto y persistirlo en `/etc/config/zerotier` (ver 2.3).

### 3.2 Firewall por defecto REJECT — RESUELTO
**Problema**: la interfaz ZT no estaba en ninguna zona → el tráfico entrante era REJECT → `ping loss` / `Connection refused`.
**Solución**: agregar `ztr4nxo42h` a la zona `lan` (input ACCEPT) + `firewall reload`.

### 3.3 netifd derribaba la interfaz — RESUELTO
**Problema**: `network.zt.auto='0'` → tras `network reload` la interfaz quedaba DOWN → `No route to host`.
**Solución**: `auto='1'` + `network reload` + `zerotier restart` (para que el daemon re-aplique la IP).

### 3.4 Sin IPv4 tras reload (solo link-local) — RESUELTO
**Problema**: tras el `network reload` la IP 192.168.194.229 no se re-asignaba (listnetworks la mostraba, `ip addr` no).
**Solución**: `/etc/init.d/zerotier restart` → a los ~15s la interfaz recuperó `inet 192.168.194.229/24`. (Warning inofensivo: `rm: '/var/run/zerotier/lib/zerotier-one' is a directory`.)

### 3.5 Daemons duplicados — RESUELTO
**Problema**: había 2 zerotier-one (uno manual `-d`, otro procd) → comportamiento errático.
**Solución**: `killall zerotier-one` + dejar solo el de procd.

### 3.6 zerotier-cli sin autotoken — RESUELTO
**Problema**: `zerotier-cli status` fallaba porque no encontraba el autotoken en la ruta por defecto.
**Solución**: pasar `-D<home_path>` explícito: `/usr/local/usr/bin/zerotier-cli -D/var/run/zerotier/lib/zerotier-one_<nodeid>_cfg021350 ...`

### 3.7 Error de resolución de IP (verificado y descartado)
**Problema**: tras el reboot el puerto 22 pareció abierto a los 10s.
**Solución**: era un falso positivo (el router aún no había caído del todo); la verificación real con uptime confirmó el reinicio. La conectividad ZeroTier real tardó ~2 min post-boot.

---

## 4. Guía de reproducción CASI automatizada (script)

El siguiente script resume todo el proceso. Ejecutar como `root` en el router vía SSH:

```sh
#!/bin/sh
# reproduce_zerotier.sh — configura ZeroTier en RUT200 de forma idempotente
# Uso: sh reproduce_zerotier.sh <NETWORK_ID>
# Requisitos: acceso root al router, red ya creada en my.zerotier.com

NET="$1"
[ -z "$NET" ] && { echo "Uso: $0 <NETWORK_ID>"; exit 1; }

set -e
echo "==> [1/6] Instalando paquete zerotier (si falta)"
opkg update >/dev/null 2>&1 || true
opkg list-installed | grep -q '^zerotier' || opkg install zerotier

ZT_ONE=/usr/local/usr/bin/zerotier-one
ZT_CLI=/usr/local/usr/bin/zerotier-cli

echo "==> [2/6] Arrancando daemon"
/etc/init.d/zerotier start || true

echo "==> [3/6] Uniéndose a red $NET"
$ZT_CLI join "$NET" || true

echo "==> [4/6] Persistiendo identidad en /etc/config/zerotier"
NODE_ID=$($ZT_CLI info | awk '{print $3}')
CFG=$(ls -d /var/run/zerotier/lib/zerotier-one_${NODE_ID}_cfg* 2>/dev/null | head -1)
if [ -n "$CFG" ] && [ -f "$CFG/identity.secret" ]; then
  SECRET=$(cat "$CFG/identity.secret")
  cat > /etc/config/zerotier <<EOF
config instance 'zt_instance'
	option enabled '1'
	option secret '$SECRET'
	option node_id '$NODE_ID'

config network_zt_instance
	option enabled '1'
	option network_id '$NET'
	option allow_global '1'
	option allow_managed '1'
	option allow_default '1'
EOF
  echo "   Node ID: $NODE_ID — autorízalo en my.zerotier.com"
fi

echo "==> [5/6] Firewall: interfaz ZT en zona LAN"
IFACE=$(ls /sys/class/net/ | grep '^zt' | head -1)
uci add_list firewall.@zone[0].network="$IFACE" 2>/dev/null || true
uci commit firewall
/etc/init.d/firewall reload || true

echo "==> [6/6] Interfaz de red con auto=1"
uci set network.zt=interface
uci set network.zt.proto='none'
uci set network.zt.device="$IFACE"
uci set network.zt.auto='1'
uci commit network
/etc/init.d/network reload || true
/etc/init.d/zerotier restart || true

echo "==> LISTO. Espera ~2 min y verifica:"
echo "    $ZT_CLI listnetworks"
echo "    ip addr show $IFACE"
```

**Flujo operativo tras el script** (por cada router nuevo):
1. Ejecutar el script (necesita solo el network id de la flota).
2. Autorizar el nodo en my.zerotier.com (web, 5 segundos).
3. Anotar la IP asignada (o reservarla fija por MAC en el controlador).
4. Verificar `ssh root@<ip_zerotier>`.

> Para automatizar 1200 buses: el paso 2 se puede hacer con la **API de my.zerotier.com** (POST /network/<id>/member) desde un script, y el paso 3 con reservas DHCP por MAC en el controlador.

---

## 5. Estado del piloto (lo que quedó instalado)

| Componente | Valor |
|---|---|
| Paquete | zerotier 1.14.0-7 (opkg, overlay) |
| Red | `srvs` = `233ccaac27bddf60` |
| IP del router | **192.168.194.229/24** |
| Node ID | `0f65f0a4a2` |
| Interfaz | `ztr4nxo42h` (UP, mtu 2800) |
| Firewall | zona LAN (ACCEPT) |
| Servicio | procd `START=90` (auto-start) |
| Gestión remota | `ssh root@192.168.194.229` |
| Coexistencia | lp104mqttv2 sigue reportando MQTT cada 30s (no interferido) |

### Validación post-reinicio (uptime 180s)
- ZeroTier: `ONLINE`, red `srvs OK`, IP `192.168.194.229/24` ✅
- lp104mqttv2: `[CONECTADO]` + publicando `report/online` + `report/status` ✅
- SSH por ZeroTier desde notebook: **OK** ✅
- Ping por ZeroTier: 0% pérdida ✅

---

## 6. Pendientes para la flota completa (1200 buses)

1. **Plan de pago o controller self-hosted** (límite 25 nodos del plan free).
2. **Reserva de IP fija por bus** (MAC → IP estática en el controlador) para gestión predecible.
3. **Script de aprovisionamiento masivo** (API de ZeroTier para autorizar nodos automáticamente).
4. **Prueba fuera de casa** (NAT traversal / relay de ZeroTier desde red externa al notebook).
5. **Nombrado de nodos** en my.zerotier.com (ej: bus-001, bus-002) para identificación rápida.
6. **Seguridad**: idealmente restringir SSH por llave (no password) una vez en producción.

---

## 7. Referencias

- ZeroTier CLI: `zerotier-cli status|listnetworks|peers|join|leave`
- Home path del daemon: `/var/run/zerotier/lib/zerotier-one_<nodeid>_cfg*`
- UI web RutOS: paquete `vuci-app-zerotier-ui` (opcional)
- Wiki Teltonika: ZeroTier en RUTOS (config instance + network_<name>)