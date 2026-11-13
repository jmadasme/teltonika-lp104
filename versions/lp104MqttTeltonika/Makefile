# Makefile — cross-compilación de lp104mqtt para Teltonika RUT200 (ramips/mt76x8)
#
# Target: OpenWrt 21.02.0 / ramips / mt76x8 / mipsel_24kc / musl
# Toolchain: mipsel-openwrt-linux-musl-gcc (gcc 8.4.0)
#
# Uso:
#   make                       # compila lp104mqtt (estático, stripped)
#   make SDK_DIR=/ruta/al/sdk  # override del SDK por CLI
#   SDK_DIR=/ruta make         # override del SDK por entorno
#   make install               # scp del binario al router (requiere ROUTER_PASS)
#   make test                  # ejecuta /usr/local/bin/lp104mqtt en el router
#   make clean
#
# SEGURIDAD: la password del router NUNCA se escribe en este archivo.
#   - Vía 1: exportar ROUTER_PASS en el entorno (se usa con sshpass).
#   - Vía 2: autenticación por clave SSH (dejar ROUTER_PASS vacío).
#
# RUTA PERSISTENTE: el binario se instala en /usr/local/bin/$(TARGET)
# (overlay rw en RUT200), la misma ruta que usa el init.d (PROG).
# /tmp es volátil (tmpfs) y NO es destino de instalación.

# SDK instalado localmente en el proyecto (persistente; ya NO en /tmp que se borra).
SDK_DIR ?= /home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64

TOOLCHAIN_DIR = $(SDK_DIR)/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl
CC            = $(TOOLCHAIN_DIR)/bin/mipsel-openwrt-linux-musl-gcc
STRIP         = $(TOOLCHAIN_DIR)/bin/mipsel-openwrt-linux-musl-strip

CFLAGS  ?= -static -Os -Wall -Wextra
LDFLAGS ?= -static

# La toolchain de OpenWrt requiere STAGING_DIR para localizar su sysroot.
export STAGING_DIR = $(SDK_DIR)/staging_dir

TARGET  = lp104mqtt
SRC     = lp104mqtt.c
HDR     = mqtt_config.h

ROUTER_IP   ?= 192.168.1.2
ROUTER_PORT ?= 22
ROUTER_USER ?= root
ROUTER_PASS ?=
# Ruta persistente: coincide con PROG del init.d (/usr/local/bin/lp104mqtt).
# /usr/local es overlay rw en RUT200; /tmp es volátil (tmpfs).
ROUTER_DEST ?= /usr/local/bin/$(TARGET)

# ssh usa -p para el puerto; scp usa -P (mayúscula).
SSH_OPTS = -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $(ROUTER_PORT)
SCP_OPTS = -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P $(ROUTER_PORT)
SSH      = $(if $(ROUTER_PASS),sshpass -p '$(ROUTER_PASS)' ssh,ssh)
SCP      = $(if $(ROUTER_PASS),sshpass -p '$(ROUTER_PASS)' scp,scp)

.PHONY: all clean check-toolchain install test

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

install: $(TARGET)
	$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) 'mkdir -p $(dir $(ROUTER_DEST))'
	$(SCP) $(SCP_OPTS) $(TARGET) $(ROUTER_USER)@$(ROUTER_IP):$(ROUTER_DEST)
	$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) 'chmod +x $(ROUTER_DEST)'
	@echo "OK: $(TARGET) transferido a $(ROUTER_IP):$(ROUTER_DEST)"

test:
	$(SSH) $(SSH_OPTS) $(ROUTER_USER)@$(ROUTER_IP) '$(ROUTER_DEST); echo "exit=$$?"'
