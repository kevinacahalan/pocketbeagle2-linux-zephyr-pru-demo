# Debian device-tree overlays. The Linux app itself is linux-app/.
#
# Preprocess the way BeagleBoard-DeviceTrees v6.18.x does, so
# #include "ti/k3-pinctrl.h" resolves from that tree. For arm64 their Makefile
# sets src to src/arm64, and -I$(src) is what finds ti/k3-pinctrl.h.
# BBDT is a checkout of that tree. build.sh and the Yocto recipe fetch it.
# This repository does not carry its own copy of the macro.

BBDT ?= $(abspath ../../third_party/BeagleBoard-DeviceTrees)
# Make's built-in CPP is "$(CC) -E", which hides "?=". Force the preprocessor
# BeagleBoard-DeviceTrees names. A command-line CPP= still wins (the Yocto
# recipe passes the build compiler's -E).
CPP = cpp
DTC = dtc

DTSO = \
	k3-am62-pocketbeagle2-spi0-hello.dtso \
	k3-am62-pocketbeagle2-pru1-xy.dtso \
	k3-am62-pocketbeagle2-leftover-gpio.dtso

DTBO = $(DTSO:.dtso=.dtbo)
HEADER := $(BBDT)/src/arm64/ti/k3-pinctrl.h

DTC_CPP_FLAGS = -nostdinc \
	-I$(BBDT)/include \
	-I$(BBDT)/src/arm64 \
	-I$(BBDT)/src \
	-undef -D__DTS__

all: $(DTBO)

$(DTBO): $(HEADER)

$(HEADER):
	$(error Missing $(HEADER). Run ./beagle-linux/scripts/build.sh linux)

%.dtbo: %.dtso
	$(CPP) $(DTC_CPP_FLAGS) -x assembler-with-cpp -o $@.dts.tmp $<
	$(DTC) -@ -O dtb -o $@ -b 0 -i $(BBDT)/src/arm64 $@.dts.tmp
	rm -f $@.dts.tmp

clean:
	rm -f $(DTBO) *.dts.tmp
