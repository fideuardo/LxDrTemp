# ===== Config básica =====
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# Si necesitas forzar toolchain/arch (normalmente NO hace falta):
# export ARCH ?= x86_64
# export CROSS_COMPILE ?=

# ===== Declaración del módulo =====
obj-m := simtemp.o

# Lista automáticamente todos los .c en core/ como parte de simtemp
simtemp-objs := $(patsubst %.c,%.o,$(wildcard core/*.c))

# ===== Includes del proyecto (se propagan a subdirectorios) =====
# MUY IMPORTANTE: apunta a la raíz del módulo con $(M) para que funcione también dentro de core/
subdir-ccflags-y += -I$(M)/include -I$(M)/include/uapi

# ===== Reglas estándar =====
.PHONY: all clean load unload reload dmesg dtbo

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@rm -f *.dtbo

# ===== Helpers de carga/descarga (opcionales) =====
load: all
	sudo insmod ./simtemp.ko

unload:
	-@sudo rmmod simtemp 2>/dev/null || true

reload: unload load

dmesg:
	dmesg | tail -n 80

# ===== (Opcional) Compilar Device Tree Overlay con dtc =====
# Requiere: sudo apt install device-tree-compiler
DTDIR ?= dts
DTBODIR ?= .

dtbo: $(DTBODIR)/simtemp.dtbo

$(DTBODIR)/simtemp.dtbo: $(DTDIR)/simtemp-overlay.dts
	dtc -@ -I dts -O dtb -o $@ $<
	@echo "Generado: $@"
