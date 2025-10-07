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

KDIR ?= /home/fideu/WSL2-Linux-Kernel

obj-m := simtemp.o
simtemp-objs := core/simtemp_core.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
