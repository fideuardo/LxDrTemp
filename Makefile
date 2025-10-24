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

obj-m := simtemp.o simtemp_pdev_stub.o
simtemp-objs := core/simtemp_core.o core/simtemp_dt.o core/simtemp_ringbuf.o
simtemp_pdev_stub-objs := core/simtemp_pdev_stub.o

ccflags-y += -I$(src)/include

CFLAGS_USER ?= -O2 -g -std=c11 -Wall -Wextra

.PHONY: all clean load unload load_nodt load_acpi unload_acpi modules dtbo apitest

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

all: dtbo modules apitest

apitest: apitest/apitest

apitest/apitest: apitest/apitest.c include/uapi/simtemp_uapi.h
	$(CC) $(CFLAGS_USER) -Iinclude -o $@ $<

dtbo:
	dtc -@ -I dts -O dtb -o simtemp.dtbo nxp-simtemp-overlay.dts

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f *.dtbo apitest/apitest
load: all
	-sudo rmmod simtemp || true
	sudo cp -f simtemp.dtbo /lib/firmware/
	sudo dtoverlay -d . simtemp || echo "Overlay may already be applied."
	sudo insmod simtemp.ko
	dmesg | tail -n 5

# ---
# ACPI / non-Device-Tree helpers:
# Load the stub platform_device first so nxp_simtemp can probe on x86.
load_acpi: modules
	-sudo rmmod simtemp || true
	-sudo rmmod simtemp_pdev_stub || true
	sudo insmod simtemp_pdev_stub.ko
	sudo insmod simtemp.ko
	dmesg | tail -n 5

unload_acpi:
	-sudo rmmod simtemp || true
	-sudo rmmod simtemp_pdev_stub || true
	dmesg | tail -n 5

load_nodt: all
	-sudo rmmod simtemp || true
	sudo insmod simtemp.ko
	dmesg | tail -n 5

unload:
	-sudo rmmod simtemp || true
	-sudo dtoverlay -r simtemp || true
	dmesg | tail -n 5
