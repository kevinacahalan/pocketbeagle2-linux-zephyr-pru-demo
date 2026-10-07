# Cross-compiled on the laptop by ./beagle-linux/scripts/build.sh kmod
obj-m += pru_rproc.o
ccflags-y += -I$(src)

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
