# Debian device-tree overlays. The Linux app itself is linux-app/.

DTSO = \
	k3-am62-pocketbeagle2-spi0-hello.dtso \
	k3-am62-pocketbeagle2-pru1-xy.dtso \
	k3-am62-pocketbeagle2-leftover-gpio.dtso

DTBO = $(DTSO:.dtso=.dtbo)

all: $(DTBO)

%.dtbo: %.dtso
	dtc -@ -I dts -O dtb -o $@ $<

clean:
	rm -f $(DTBO)
