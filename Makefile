# SPDX-License-Identifier: GPL-3.0-only

#
# Makefile for the Temperature Sensor Simulator Driver
#
# Copyright (C) 2024 Fidel E.
#

#
# This Makefile is used to build the Temperature Sensor Simulator Driver
# as a kernel module.
#

KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m := simtemp.o
simtemp-objs := core/simtemp_core.o core/simtemp_dt.o core/simtemp_ringbuf.o

ccflags-y += -I$(src)/include

all:
	dtc -@ -I dts -O dtb -o simtemp.dtbo nxp-simtemp-overlay.dts
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f *.dtbo

load: all
	-sudo rmmod simtemp || true
	sudo cp -f simtemp.dtbo /lib/firmware/
	sudo dtoverlay -d . simtemp || echo "Overlay may already be applied."
	sudo insmod simtemp.ko
	dmesg | tail -n 5

load_nodt: all
	-sudo rmmod simtemp || true
	sudo insmod simtemp.ko
	dmesg | tail -n 5

unload:
	-sudo rmmod simtemp || true
	-sudo dtoverlay -r simtemp || true
	dmesg | tail -n 5

.PHONY: all clean load unload load_nodt
