# PocketBeagle 2: Linux, Zephyr, and PRU

Linux on the A53s calls Zephyr functions over eRPC on the existing RPMsg channel, and commands PRU0 over RPMsg. Zephyr asks PRU0 through a small DDR mailbox. PRU0 is the transfer core: it arms PRU1 in PRU shared RAM, and it also writes a 32-bit X and a 32-bit Y there. PRU1 does not use RPMsg. It runs a cycle-counted loop, samples the arm block once per period, and when the X/Y sequence changes it shifts that pair out on a header pin. Linux hands Zephyr bulk chunks through a separate DDR region (`zephyr-m4/include/pb2_shm.h`). The device-manager R5 is not loaded or stopped.

The board still runs the BeagleBoard Debian image. That tree is `beagle-linux/`, and it is the active test target. A separate minimum Poky image is built with kas-container from `yocto/kas/pb2-poky.yml`. These instructions do not flash the board. Host builds run in containers.

`LAYOUT.md` is the directory map.

## Build

Every compile runs in a container. The script is `beagle-linux/scripts/build.sh`. It uses podman to build the images described by the Containerfiles, then runs the compile in those images. Do not use a compiler installed on the host. `./beagle-linux/scripts/build.sh` with no argument builds userspace, PRU firmware, and Zephyr, in that order. The kernel module is a separate step because it needs the board's kernel headers.

1. Linux userspace, image `pb2-linux-userspace` from `containers/linux/Containerfile` (Debian 13, the aarch64 cross compiler, and `dtc`). This also clones EmbeddedRPC 1.14.0 into `third_party/erpc` if it is missing, and BeagleBoard-DeviceTrees `v6.18.x` commit `550f7711a4696d5f5e92505289379b754e871313` into `third_party/BeagleBoard-DeviceTrees` so the overlays can include `ti/k3-pinctrl.h`. That header is not copied into this repository.

   ```
   ./beagle-linux/scripts/build.sh linux
   ```

   Output: `linux-app/pb2`. That one program is what `pb2-demo` starts. SPI hello is its `spi` command, next to xy, arm, chunk, and say. The same step preprocesses the Debian overlays in `beagle-linux/overlays/` with `AM62X_IOPAD()` from that Beagle tree, including `k3-am62-pocketbeagle2-leftover-gpio.dtbo`. `PINMUX.md` lists every pin those overlays mux.

2. PRU firmware, image `pb2-pru-cgt` from `containers/pru-cgt/Containerfile`. The first stage installs TI CGT 2.3.3 on Debian 10, because the installer is a 32-bit stub that needs that older glibc. The second stage is Debian 13 with `clpru` at `/opt/ti-cgt-pru`. The firmware is compiled there.

   ```
   ./beagle-linux/scripts/build.sh pru
   ```

   PRU0, the transfer core, is `pru/pru0/` (`main.c` and `AM62x_PRU0.cmd`). PRU1, the SLIP frame, is `pru/pru1/`: the sample loop is `main.c`, the pin shifting is `slip_tx.asm`, and the linker script is `AM62x_PRU1.cmd`. Output: `pru/build/am62x-pru0-fw` and `pru/build/am62x-pru1-fw`.

3. Zephyr on the M4, image `pb2-zephyr-sdk` from `containers/zephyr/Containerfile`. That image is the Zephyr CI image with `ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-1.0.1`. The container runs `west init -l manifest` from the repo root when `.west/` is missing, then `west update`, `zephyr-m4/scripts/apply-patches.sh`, and `west build -b pocketbeagle_2/am6254/m4 zephyr-m4/m4`. `west update` checks out the Zephyr revision pinned in `manifest/west.yml` and drops local edits, so that script reapplies any patches under `zephyr-m4/patches/`. That directory is empty: Zephyr `v4.5.0-rc1` already releases the hardware chip select, so an active-low SPIEN idles high without a local patch. Do not run `west init -l .` from the repo root.

   ```
   ./beagle-linux/scripts/build.sh zephyr
   ```

   Output: `build/zephyr/pb2_m4.elf`.

4. Patched `pru_rproc`, in the Linux userspace image. The stock module has no virtio kick, so PRU0's RPMsg firmware will not boot until this module is loaded. The script rsyncs `/usr/src/linux-headers-$PB2_KREL` from `$PB2_SSH` into `.cache/` once, then cross-compiles in the container. `PB2_SSH` is required. `PB2_KREL` defaults to `6.18.52-arm64-k3-r56`, the kernel of the BeagleBoard Debian Trixie image this example was exercised on. Set it to `uname -r` on your board if that differs.

   ```
   ./beagle-linux/scripts/build.sh kmod
   ```

   Output: `beagle-linux/kernel/pru_rproc/pru_rproc.ko`.

5. Poky image for PocketBeagle 2, with kas-container. The machine, kernel, and layers come from TI's `kas/poky-wrynose-config.yml`. The distro is poky, and the target is `pb2-demo-image` (core-image-minimal plus the demo). The build compiles `pb2`, the Zephyr M4 ELF, and both PRU firmwares. It does not flash the board, and it does not load or stop `78000000.r5f`.

   From the repo root:

   ```
   wget -O kas-container https://raw.githubusercontent.com/siemens/kas/5.5/kas-container
   chmod +x kas-container
   KAS_BUILD_DIR=yocto/build KAS_CONTAINER_ENGINE=podman ./kas-container build yocto/kas/pb2-poky.yml
   ```

   Downloads, shared state, and `tmp/` stay under `yocto/`. They are not committed. The device-tree overlays are installed as files under `/lib/firmware` and are not added to the boot overlay list.

### Zephyr devicetree overlay

`zephyr-m4/m4/boards/pocketbeagle_2_am6254_m4.overlay` is a Zephyr devicetree overlay, not a Linux overlay for the Debian kernel. `west build` picks it up by board name (`pocketbeagle_2/am6254/m4`) and compiles it into `pb2_m4.elf`. It shrinks the M4 DDR region so two ranges stay outside the Zephyr image: a 64 KiB bulk buffer at `0x9d9ef000` and the 4 KiB mailbox at `0x9d9ff000`. There is no loadable ELF section at either address.

On the flashed Debian image the overlay is not applied with `dtbo`, configfs, or a reboot. `pb2-demo` copies the ELF to `/lib/firmware/am62-mcu-m4f0_0-fw` and starts remoteproc `5000000.m4fss`. That boot is what brings the overlay's memory map up. The script does not stop or reload `78000000.r5f`. It does not run at boot.

The same overlay also muxes main SPI2 and, once that ELF is running, `main()` clocks the five bytes `hello` at 1 MHz, mode 0, 8-bit MSB. D1 is MOSI and D0 is MISO. That pinmux is inside `pb2_m4.elf`. It is not a Debian dtbo. The M4 console UART is unwired, so the result is also written to the DDR mailbox: `spi hello ok` after the transfer returns, or `spi hello not ready` / `spi hello fail <errno>` if it does not.

That main pinmux is a bare `pinctrl@f4000`. Upstream, main padcfg is a child of the `ti,control-module` syscon at `0xf0000`, and that syscon is what the M4 unlocks at boot. This image does not unlock main padcfg itself. CLK and MOSI already toggle, so Linux has been unlocking that block.

## SPI hello

Linux and Zephyr use different controllers. Linux uses main SPI0 (`0x20100000`). Zephyr uses main SPI2 (`0x20120000`). Mode 0 on the SPI0 balls and mode 1 on the SPI2 balls are the SPI functions. The PRU GPO/GPI modes on those balls are other mux values and are not selected. The M4 console is MCU UART0 on P2.05A (ball B5) and P2.07A (ball A5); those pads are not in either overlay. RPMsg does not use header pins.

MCU SPI0 (`0x04b00000`) is not used. The cape tables only bring `MCU_SPI0_CS2` (P2.09A) and `MCU_SPI0_CS3` (P1.26, ball D6) out to the headers, not a clock and data pair.

Some header pins short two SoC balls. Only one ball is muxed to SPI. The partner ball is held in GPIO mode 7 with its output disabled.

### Linux, header P2

```
AM62X_IOPAD(0x01bc, PIN_OUTPUT|PIN_DRIVE_STRENGTH_NOMINAL, 0) /* (A14) SPI0_CLK */
AM62X_IOPAD(0x01c4, PIN_OUTPUT|PIN_DRIVE_STRENGTH_NOMINAL, 0) /* (B14) SPI0_D1 */
AM62X_IOPAD(0x01c0, PIN_INPUT|PIN_DRIVE_STRENGTH_NOMINAL, 0) /* (B13) SPI0_D0 */
AM62X_IOPAD(0x01b4, PIN_OUTPUT|PIN_DRIVE_STRENGTH_NOMINAL, 0) /* (A13) SPI0_CS0 */
AM62X_IOPAD(0x00a4, PIN_DISABLE|PIN_DRIVE_STRENGTH_NOMINAL, 7) /* (M22) GPMC0_DIR.GPIO0_40 */
AM62X_IOPAD(0x0170, PIN_DISABLE|PIN_DRIVE_STRENGTH_NOMINAL, 7) /* (AA18) RGMII2_TD1.GPIO0_90 */
```

| Signal | Header | Ball | Pad offset |
| --- | --- | --- | --- |
| CLK | P2.29 | A14 | `0x01bc` |
| MOSI (SPI0_D1) | P2.25 | B14 | `0x01c4` |
| MISO (SPI0_D0) | P2.27 | B13 | `0x01c0` |
| CS0 | P2.31 | A13 | `0x01b4` |

P2.29A (ball M22) and P2.31A (ball AA18) are the shorted partners and are disabled. Attach a Saleae to P2.25, P2.27, P2.29, and P2.31. Clock is 1 MHz, SPI mode 0. The payload is the five bytes `hello`.

### Zephyr, header P1

| Signal | Header | Ball | Pad offset |
| --- | --- | --- | --- |
| CLK | P1.08 | A20 | `0x01b0` |
| MOSI (SPI2_D1) | P1.12 | A19 | `0x0198` |
| MISO (SPI2_D0) | P1.10 | B19 | `0x0194` |
| CS0 | P1.06 | E19 | `0x01ac` |

P1.06A (ball AD18), P1.10's other ball A18, and P1.12A (ball AE18) are disabled. Attach a Saleae to P1.06, P1.08, P1.10, and P1.12. Same payload, rate, and mode as Linux.

### Linux overlay file, build, and load

Source: `beagle-linux/overlays/k3-am62-pocketbeagle2-spi0-hello.dtso`.

`./beagle-linux/scripts/build.sh linux` is the one command. It fetches BeagleBoard-DeviceTrees, preprocesses this overlay with `ti/k3-pinctrl.h`, and runs `dtc`, along with `pb2`. The blob is `beagle-linux/overlays/k3-am62-pocketbeagle2-spi0-hello.dtbo`.

The flashed image loads overlays from the boot partition, not from configfs (`/sys/kernel/config` on this image has no device-tree overlays). Copy the blob to `/boot/firmware/overlays/`, then on the default label in `/boot/firmware/extlinux/extlinux.conf` set:

```
fdtoverlays /overlays/k3-am62-pocketbeagle2-spi0-hello.dtbo
```

That takes effect on the next boot. After that, the linux window command `spi` writes `hello` to `/dev/spidev0.0` (main SPI0 is the SPI controller this image enables; the child is `spidev@0` with compatible `rohm,dh2228fv`, which this kernel binds to `spidev`). Pass another path if the node name differs:

```
spi /dev/spidev0.0
```

`pb2 spi` does the same transfer and exits. The software transfers ran. The captured line is in "What a passing run looks like" below. With the M4 image running, the DDR mailbox read `spi hello ok` from Zephyr's own SPI2 write. The Saleae was not connected, so this repository has no logic-analyzer capture of those pins.

## X and Y through PRU0 to PRU1

PRU0 is the only RPMsg core (channel `rpmsg-raw`, port 30). Linux types `pru0 xy <x> <y>` in the linux window. That text is the RPMsg payload. Zephyr does not open its own channel to PRU0. Linux types `zephyr xy <x> <y>`, eRPC `sendXy` runs on the existing Zephyr RPMsg channel, and Zephyr publishes the same `xy <x> <y>` text in the DDR mailbox at `0x9D9FF000`. PRU0 already polls that mailbox. Both numbers are decimal and fit in 32 bits.

PRU0 writes the pair into PRUSS shared RAM, not the DDR mailbox. The AM62x linker map puts 32 KiB of shared RAM at local address `0x00010000` (`PRU_SHAREDMEM`, constant register C28, length `0x8000`). C28's pointer is that address shifted right by 8, so `0x0100` names this base, and a near access covers 256 bytes. The firmware does not program C28. It uses the direct local address, which is how the arm block is already reached. The SoC address of local `0x00010000` is `0x30050000`.

| Offset from local `0x00010000` | SoC address | What |
| --- | --- | --- |
| `0x00` | `0x30050000` | Arm block, 32 bytes. PRU0 writes it. PRU1's timing loop samples it. |
| `0x40` | `0x30050040` | X/Y record. Still inside the first 256 bytes of shared RAM. |

The record is little-endian `uint32` fields, in this order: `magic`, `seq`, `x`, `y`, `consumed`. Magic is `0x50423258` (`PB2X`). PRU0 stores `x`, then `y`, then the magic, then `seq` last. `consumed` is PRU1's. `pb2 xy` prints the record once. The pru1 window prints it on each sample.

PRU1 is the transmission core. When `seq` changes and the magic matches, it copies X and Y, shifts a frame out, then stores that `seq` in `consumed`. A matching `consumed` means PRU1 finished that sequence. It does not mean a scope saw the pin.

The frame is SLIP (RFC 1055) carried as UART 8N1. Idle is high. Each byte is one UART character: start bit 0, eight data bits least-significant first, stop bit 1. The PRUSS core-clock mux on the image this example was exercised on is 250 MHz, and each bit is 2170 cycles, which is 115200 baud within a fraction of a percent. One iteration of PRU1's timing loop grows by the length of the frame when a new sequence is sent.

Bytes, in order, when none of the payload bytes is `0xC0` or `0xDB`:

| Byte | Value |
| --- | --- |
| 0 | `0xC0` SLIP END |
| 1 | X bits 7:0 |
| 2 | X bits 15:8 |
| 3 | X bits 23:16 |
| 4 | X bits 31:24 |
| 5 | Y bits 7:0 |
| 6 | Y bits 15:8 |
| 7 | Y bits 23:16 |
| 8 | Y bits 31:24 |
| 9 | XOR of bytes 1 through 8 |
| 10 | `0xC0` SLIP END |

If a payload byte (X, Y, or the XOR) is `0xC0`, those two bytes on the wire are `0xDB 0xDC`. If it is `0xDB`, they are `0xDB 0xDD`. The END bytes are not escaped.

The pin is header P1.20, ball Y24, pad offset `0x00cc`, mux mode 2 (`PR0_PRU1_GPO5`), which is PRU1 R30 bit 5:

```
AM62X_IOPAD(0x00cc, PIN_OUTPUT|PIN_DRIVE_STRENGTH_NOMINAL, 2) /* (Y24) VOUT0_DATA5.PR0_PRU1_GPO5 */
```

It is not a Linux SPI0 pin (P2.29, P2.25, P2.27, P2.31) and not a Zephyr SPI2 pin (P1.08, P1.12, P1.10, P1.06). The expansion table shows one ball on P1.20.

Source: `beagle-linux/overlays/k3-am62-pocketbeagle2-pru1-xy.dtso`. `./beagle-linux/scripts/build.sh linux` builds the blob next to the SPI overlay. Copy it to `/boot/firmware/overlays/` and add it to `fdtoverlays` on the default extlinux label, the same way as the SPI overlay. The PRUSS node takes that pin group. The pad does not change until the next boot. This repository does not include a capture of P1.20.

Header pins that Linux, Zephyr, and the PRUs do not already use are muxed as input GPIOs by `beagle-linux/overlays/k3-am62-pocketbeagle2-leftover-gpio.dtso`. Each of those pins uses `AM62X_IOPAD()` (the two MCU partners use `AM62X_MCU_IOPAD()`) and is commented as unused and muxed as Linux GPIO. The same `build.sh linux` step writes `k3-am62-pocketbeagle2-leftover-gpio.dtbo`. Load it the same way as the SPI overlay: copy the blob to `/boot/firmware/overlays/` and append it to `fdtoverlays` on the default extlinux label. It is not on the boot list until you add it, and the image recipe does not add it. The full pin list is `PINMUX.md`. None of these pins is an LED.

From the linux window the commands are `pru0 xy <x> <y>` and `zephyr xy <x> <y>`. PRU0's RPMsg reply names who asked and what it wrote for PRU1. `pb2 xy` prints the shared record, including `consumed`.

## Run

`pb2-demo-image` installs the runner on `PATH` as `/usr/bin/pb2-demo`. Nothing starts the cores at boot. Run it yourself:

```
pb2-demo
```

That stops the M4, PRU0, and PRU1, starts them again, and merges their output on this console. Each line is prefixed `[Linux]`, `[M4]`, `[PRU0]`, or `[PRU1]`. It never stops, reloads, or replaces `78000000.r5f`.

```
pb2-demo --tmux
pb2-demo --attach
pb2-demo --attach --tmux
```

`--tmux` is the four-window session `pb2` (`linux`, `zephyr`, `pru0`, `pru1`). `--attach` does not stop or start any core. If session `pb2` is already up, `--attach` attaches to it.

### tmux

The prefix is Ctrl+b. These are windows, not panes. `pb2-demo` creates them in this order:

| After Ctrl+b | Window |
| --- | --- |
| 0 | `linux` |
| 1 | `zephyr` |
| 2 | `pru0` |
| 3 | `pru1` |

Detach, which leaves the session running and returns you to the shell: Ctrl+b, then d. Attach again with `tmux attach -t pb2`.

The script needs root or sudo to stop and start the M4 and the two PRUs, insert `pru_rproc.ko` when the running kernel is missing `pru_rproc_kick`, and grant `pb2` `CAP_SYS_RAWIO` so it can open `/dev/mem`. From the Linux console: `zephyr say <text>`, `zephyr arm <period> <count>`, `zephyr chunk <text>`, `zephyr xy <x> <y>`, `zephyr spi [text]`, `pru0 xy <x> <y>`, `pru0 <text>`, or `spi`. `zephyr spi` asks the M4 to write SPI2. With no text it sends the five-byte hello. With text it sends those bytes. `spi` is the Linux `/dev/spidev0.0` hello. `zephyr arm` asks PRU0, through the mailbox, to arm PRU1. `pru0 arm <period> <count>` does that directly. `chunk` writes the bulk region and then calls Zephyr to check it. `xy` hands PRU0 the pair described above.

The Linux side still sends a startup script before it reads commands: ping PRU0, `say hello from linux`, `arm 100000 30`, `chunk bulk-hello`, then `pru0 arm 250000 20`. That script is not the X/Y measurement below.

The Debian image does not ship this runner. Copy the binaries and firmware into the system locations, and copy `pru_rproc.ko` when you need `PB2_PRU_KO`:

```
export PB2_SSH=user@board
scp linux-app/pb2 beagle-linux/scripts/pb2-demo \
  pru/build/am62x-pru0-fw pru/build/am62x-pru1-fw \
  build/zephyr/pb2_m4.elf \
  "$PB2_SSH":/tmp/
ssh "$PB2_SSH" 'sudo install -m 0755 /tmp/pb2 /tmp/pb2-demo /usr/bin/ \
  && sudo install -m 0644 /tmp/am62x-pru0-fw /tmp/am62x-pru1-fw /lib/firmware/ \
  && sudo install -m 0644 /tmp/pb2_m4.elf /lib/firmware/am62-mcu-m4f0_0-fw'
```

`user@board` is a placeholder. Use the account and address you assigned below.

## What a passing run looks like

These are `tmux capture-pane -J` lines. Prompts are omitted. The typed command is the line above each Linux sentence. The P1.20 overlay was not installed, and the wire was not scoped. The Saleae was not connected. The `spi` lines are from the linux window of the run that used this one program:

```
spi
Linux wrote the SPI hello, 5 bytes on /dev/spidev0.0
```

Linux:

```
opening /tmp/pb2-demo/zephyr.in
opening /tmp/pb2-demo/pru0.in
Linux sent "ping" to PRU0
Linux asked Zephyr to say hello from linux
Linux asked Zephyr to arm PRU1 100000 30
Linux asked Zephyr to check chunk bulk-hello
Linux asked PRU0 to arm PRU1 250000 20
linux window ready. commands: zephyr say|arm|chunk|xy ... | pru0 <text>
pru0 xy 17 29
Linux asked PRU0 to publish X/Y 17 29
zephyr xy 41 83
Linux asked Zephyr to publish X/Y 41 83
pru0 arm 250000 20
Linux asked PRU0 to arm PRU1 250000 20
zephyr arm 100000 30
Linux asked Zephyr to arm PRU1 100000 30
zephyr chunk bulk-hello
Linux asked Zephyr to check chunk bulk-hello
zephyr say hello from linux
Linux asked Zephyr to say hello from linux
Linux asked Zephyr to say "hello from linux", and Zephyr replied "zephyr: hello from linux"
zephyr say "test"
Linux asked Zephyr to say test
Linux asked Zephyr to say "test", and Zephyr replied "zephyr: test"
```

Zephyr:

```
zephyr window ready on /dev/rpmsg0 (eRPC)
Linux asked Zephyr to say "hello from linux", and Zephyr replied "zephyr: hello from linux"
Linux asked Zephyr to arm PRU1, and Zephyr published period 100000 count 30 for PRU0
Linux asked Zephyr to check the chunk, and Zephyr matched seq 1 len 10 crc c959d039
Linux asked Zephyr to publish X/Y 41 83, and Zephyr sent that pair to PRU0
Linux asked Zephyr to arm PRU1, and Zephyr published period 100000 count 30 for PRU0
Linux asked Zephyr to check the chunk, and Zephyr matched seq 2 len 10 crc c959d039
Linux asked Zephyr to say "hello from linux", and Zephyr replied "zephyr: hello from linux"
```

PRU0:

```
pru0 window ready on /dev/rpmsg2
sent to pru0: ping
Linux sent "ping" to PRU0, and PRU0 echoed it
Zephyr asked PRU0 to arm PRU1, and PRU0 wrote period 100000 count 30 for PRU1
sent to pru0: arm 250000 20
Linux asked PRU0 to arm PRU1, and PRU0 wrote period 250000 count 20 for PRU1
sent to pru0: xy 17 29
Linux asked PRU0 to publish X/Y, and PRU0 wrote x 17 y 29 seq 1 for PRU1
Zephyr asked PRU0 to publish X/Y, and PRU0 wrote x 41 y 83 seq 2 for PRU1
sent to pru0: arm 250000 20
Linux asked PRU0 to arm PRU1, and PRU0 wrote period 250000 count 20 for PRU1
Zephyr asked PRU0 to arm PRU1, and PRU0 wrote period 100000 count 30 for PRU1
```

PRU1, from the same run. The window prints a line when the X/Y seq changes. An unchanged seq is not printed again. `consumed` matches `seq` for the pair Zephyr published:

```
pru1 xy seq=2 x=41 y=83 consumed=2
```

## Checked in and temporary

Checked in: `linux-app/`, `beagle-linux/`, `containers/`, `zephyr-m4/`, `pru/` sources, `yocto/`, `manifest/west.yml`, `PINMUX.md`, this README, and `LAYOUT.md`.

Left on disk and not committed:

| Path | What it is |
| --- | --- |
| `.west/`, `zephyr/`, `modules/`, `build/`, `bootloader/`, `tools/` | West workspace |
| `third_party/` | PRU software support package and EmbeddedRPC clones |
| `.cache/` | TI CGT install and kernel headers |
| `pru/build/` | PRU firmware images |
| `linux-app/pb2` | the userspace program |
| `*.ko` | kernel modules, including patched `pru_rproc` |

## Reach the board

Plug in the board's USB device port. On the host, find the new network interface and give that link an address. The Debian image requests DHCP on `usb0`. Then:

```
export PB2_SSH=user@board
ssh "$PB2_SSH"
```

`user@board` is a placeholder. Use the account and address you assigned.

A serial console is optional, 115200 8N1. The M4 console balls are header P2.05A and P2.07A, and no adapter is attached, so `printk` is invisible. The visible Zephyr console is the RPMsg window. `PINMUX.md` is the pin list.
