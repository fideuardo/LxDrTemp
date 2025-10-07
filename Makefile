obj-m := simtemp.o
simtemp-objs := core/simtemp_core.o


all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
