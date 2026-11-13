# Makefile — cross-compilación de lp104mqttv2 para Teltonika RUT200 (ramips/mt76x8)
#
# Target: OpenWrt 21.02.0 / ramips / mt76x8 / mipsel_24kc / musl
# Toolchain: mipsel-openwrt-linux-musl-gcc (gcc 8.4.0)
#
# Uso:
#   make                       # compila lp104mqttv2 (estático, stripped)
#   make SDK_DIR=/ruta/al/sdk  # override del SDK por CLI
#   make install               # despliega binario + init.d + banner y desinstala V1
#   make uninstall-v1          # solo elimina el V1 del router
#   make test                  # ejecuta el binario en el router (foreground)
#   make log                   # logread | grep lp104mqttv2 en el router
#   make clean
#
# SEGURIDAD: la password del router NUNCA se escribe en este archivo.
#   - Vía 1: exportar ROUTER_PASS en el entorno (se usa con sshpass).
#   - Vía 2: autenticación por clave SSH (dejar ROUTER_PASS vacío).
#
# RUTA PERSISTENTE: el binario se instala en /usr/local/bin/$(TARGET)
# (overlay rw en RUT200), la misma ruta que usa el init.d (PROG).
# /tmp es volátil (tmpfs) y NO es destino de instalación.

# SDK instalado localmente en el proyecto V1 (persistente; ya NO en /tmp que se borra).
SDK_DIR ?= /home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64

TOOLCHAIN_DIR = $(SDK_DIR)/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl
CC            = $(TOOLCHAIN_DIR)/bin/mipsel-openwrt-linux-musl-gcc
STRIP         = $(TOOLCHAIN_DIR)/bin/mipsel-openwrt-linux-musl-strip

# gc-sections: elimina código libc muerto -> binario estático ~95 KB
CFLAGS  ?= -static -Os -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS ?= -static -Wl,--gc-sections

# La toolchain de OpenWrt requiere STAGING_DIR para localizar su sysroot.
export STAGING_DIR = $(SDK_DIR)/staging_dir

TARGET  = lp104mqttv2
SRC     = lp104mqttv2.c
HDR     = mqtt_config.h

ROUTER_IP   ?= 192.168.1.2
ROUTER_PORT ?= 22
ROUTER_USER ?= root
ROUTER_PASS ?=
# Rutas de despliegue (persistentes: overlay rw en RUT200).
ROUTER_BIN     ?= /usr/local/bin/$(TARGET)
ROUTER_INIT    ?= /etc/init.d/$(TARGET)
ROUTER_BANNER  ?= /etc/profile.d/99-lp104-banner.sh

# V1 (a desinstalar)
V1_BIN  = /usr/local/bin/lp104mqtt
V1_INIT = /etc/init.d/lp104mqtt

# V2.1: ruta TEMPORAL del nuevo binario en el router (NO pisa el servicio
# activo /usr/local/bin/lp104mqttv2). Home real de root en RUT200.
ROUTER_V2_PATH ?= /usr/local/home/root/$(TARGET)_v2

# ssh usa -p para el puerto; scp usa -P (mayúscula).
SSH_OPTS = -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $(ROUTER_PORT)
SCP_OPTS = -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P $(ROUTER_PORT)
SSH      = $(if $(ROUTER_PASS),sshpass -p '$(ROUTER_PASS)' ssh,ssh)
SCP      = $(if $(ROUTER_PASS),sshpass -p '$(ROUTER_PASS)' scp,scp)

.PHONY: all clean check-toolchain install uninstall-v1 deploy-v2 test log

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	@if [ ! -x "$(CC)" ]; then \
		echo "ERROR: toolchain no encontrada: $(CC)"; \
		echo "       Verifica SDK_DIR (actual: $(SDK_DIR))"; \
		exit 1; \
	fi
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	$(STRIP) $@
	@echo "OK: binario $(TARGET) generado (stripped)"

check-toolchain:
	@ls -1 $(TOOLCHAIN_DIR)/bin/ 2>/dev/null || \
		{ echo "ERROR: no existe $(TOOLCHAIN_DIR)/bin/"; exit 1; }

clean:
	rm -f $(TARGET) *.o

# Elimina el servicio y binario del V1 (mismo client id -> no puede coexistir).
uninstall-v1:
	-$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) \
		'killall lp104mqtt 2>/dev/null; \
		 /etc/init.d/lp104mqtt stop 2>/dev/null; \
		 /etc/init.d/lp104mqtt disable 2>/dev/null; \
		 rm -f $(V1_INIT) $(V1_BIN); \
		 true'
	@echo "OK: V1 (lp104mqtt) eliminado del router"

install: $(TARGET) uninstall-v1
	$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) 'mkdir -p $(dir $(ROUTER_BIN)) /etc/profile.d'
	$(SCP) $(SCP_OPTS) $(TARGET) $(ROUTER_USER)@$(ROUTER_IP):$(ROUTER_BIN)
	$(SCP) $(SCP_OPTS) init.d/$(TARGET) $(ROUTER_USER)@$(ROUTER_IP):$(ROUTER_INIT)
	$(SCP) $(SCP_OPTS) banner/99-lp104-banner.sh $(ROUTER_USER)@$(ROUTER_IP):$(ROUTER_BANNER)
	$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) \
		'chmod +x $(ROUTER_BIN) $(ROUTER_INIT) $(ROUTER_BANNER); \
		 /etc/init.d/$(TARGET) enable; \
		 /etc/init.d/$(TARGET) restart'
	@echo "OK: V2 desplegado en $(ROUTER_IP) (binario + init.d + banner)"

# V2.1: despliega el NUEVO binario en una ruta temporal SIN ejecutarlo y
# crea /etc/lp104mqttv2.conf con las credenciales. NO reinicia ningún
# servicio ni toca el binario activo /usr/local/bin/lp104mqttv2.
#
# SEGURIDAD: las credenciales reales NUNCA viven en este Makefile (versionado).
#   - Si existe el archivo LOCAL gitignored lp104mqttv2.conf (creado por el
#     implementador con las credenciales reales), se transfiere tal cual.
#   - Si NO existe, se copia lp104mqttv2.conf.example (placeholders) y se
#     imprime un aviso claro de que el usuario debe editar
#     /etc/lp104mqttv2.conf en el router con las credenciales reales.
deploy-v2: $(TARGET)
	$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) 'mkdir -p $(dir $(ROUTER_V2_PATH))'
	$(SCP) $(SCP_OPTS) $(TARGET) $(ROUTER_USER)@$(ROUTER_IP):$(ROUTER_V2_PATH)
	@if [ -f lp104mqttv2.conf ]; then \
		echo "deploy-v2: usando lp104mqttv2.conf local (gitignored) con credenciales reales"; \
		$(SCP) $(SCP_OPTS) lp104mqttv2.conf $(ROUTER_USER)@$(ROUTER_IP):/etc/lp104mqttv2.conf; \
	else \
		echo "deploy-v2: AVISO — no existe lp104mqttv2.conf local; se copia lp104mqttv2.conf.example (placeholders)"; \
		$(SCP) $(SCP_OPTS) lp104mqttv2.conf.example $(ROUTER_USER)@$(ROUTER_IP):/etc/lp104mqttv2.conf; \
		echo "deploy-v2: EDITE /etc/lp104mqttv2.conf en el router con las credenciales reales"; \
	fi
	$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) \
		'chmod +x $(ROUTER_V2_PATH); \
		 chmod 600 /etc/lp104mqttv2.conf; \
		 sync'
	@echo "OK: binario v2 en $(ROUTER_V2_PATH) (sin ejecutar) y /etc/lp104mqttv2.conf desplegado"

test:
	$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) \
		'$(ROUTER_BIN) & sleep 5; kill %1 2>/dev/null; echo "--- test OK ---"'

log:
	$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) \
		'logread | grep lp104mqttv2 | tail -20'
