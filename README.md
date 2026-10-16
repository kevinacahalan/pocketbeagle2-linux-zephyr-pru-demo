# PocketBeagle 2: Linux, Zephyr, and PRU

Linux on the A53s calls Zephyr functions over eRPC on the existing RPMsg channel, and commands PRU0 over RPMsg. PRU0 is the transfer core: it arms PRU1 in PRU shared RAM, and it also writes a 32-bit X and a 32-bit Y there. PRU1 does not use RPMsg. It runs a cycle-counted loop, samples the arm block once per period, and when the X/Y sequence changes it shifts that pair out on a header pin. Linux hands Zephyr bulk chunks through a separate DDR region (`zephyr-m4/include/pb2_shm.h`). The device-manager R5 is not loaded or stopped.

Host compiles run in containers. `LAYOUT.md` is the directory map. `78000000.r5f` is not loaded or stopped.

## Getting started

From the repo root, this builds userspace, PRU firmware, and the M4 ELF:

```
./beagle-linux/scripts/build.sh
```

[Build](#build) is that script, one target at a time. Do not use a compiler installed on the host.

The Debian image is <https://www.beagleboard.org/distros/pocketbeagle-2-debian-13-7-2026-10-09-iot-v6-18-x-k3>, file `pocketbeagle2-debian-13.7-iot-v6.18-k3-arm64-2026-10-09-10gb.img.xz`.

The Yocto card image is `yocto/build/deploy-ti/images/pocketbeagle2/pb2-demo-image-pocketbeagle2.rootfs.wic.xz`.

### BeagleBoard Debian

The Debian image still needs this demo installed. Put `linux-app/pb2` and `beagle-linux/scripts/pb2-demo` on `PATH`. Put `pru/build/am62x-pru0-fw` and `pru/build/am62x-pru1-fw` at `/lib/firmware/am62x-pru0-fw` and `/lib/firmware/am62x-pru1-fw`. Put `build/zephyr/pb2_m4.elf` at `/lib/firmware/am62-mcu-m4f0_0-fw`. Put `beagle-linux/kernel/pru_rproc/pru_rproc.ko` at `/usr/lib/pru_rproc.ko`; the stock module has no virtio kick. Put the three overlay blobs in `/boot/firmware/overlays/`. On the default label in `/boot/firmware/extlinux/extlinux.conf`, set:

```
fdtoverlays /overlays/k3-am62-pocketbeagle2-spi0-hello.dtbo /overlays/k3-am62-pocketbeagle2-pru1-xy.dtbo /overlays/k3-am62-pocketbeagle2-leftover-gpio.dtbo
```

The pads do not change until the next boot. Install `tmux` and `libcap2-bin` if `pb2-demo --tmux` or `setcap` is missing. The image requests DHCP on `usb0`. Start the demo with the module you installed:

```
sudo PB2_PRU_KO=/usr/lib/pru_rproc.ko pb2-demo
```

`pb2-demo` stops and starts the M4, PRU0, and PRU1. It does not stop or reload `78000000.r5f`.

### Yocto

From the repo root, fetch kas-container and build `pb2-demo-image`. Podman and docker both work. Set `KAS_CONTAINER_ENGINE` to `podman` or `docker`. Unset, kas-container uses docker when that client is installed, and otherwise podman.

```
wget -O kas-container https://raw.githubusercontent.com/siemens/kas/5.5/kas-container
chmod +x kas-container
KAS_BUILD_DIR=yocto/build ./kas-container build yocto/kas/pb2-poky.yml
```

The machine, kernel, and layers come from TI's `kas/poky-wrynose-config.yml`. The distro is poky. The target is `pb2-demo-image` (core-image-minimal plus this demo). The build compiles `pb2`, the Zephyr M4 ELF, and both PRU firmwares. It does not load or stop `78000000.r5f`. Downloads and shared state stay under `yocto/` and are not committed.

`pb2-demo-image` uses systemd. It already contains `/usr/bin/pb2` and `/usr/bin/pb2-demo`. `pb2-demo` is on `PATH`. The serial console is `ttyS2` at 115200. Login is root, home `/root`. The image does not add another account and does not set a password. `/root` holds `README.md`, `pb2`, `pb2-demo`, `pb2_m4.elf`, `am62x-pru0-fw`, and `am62x-pru1-fw`. From that directory, `./pb2-demo` starts the demo. The M4 ELF is `/lib/firmware/am62-mcu-m4f0_0-fw`. The PRU firmwares are `/lib/firmware/am62x-pru0-fw` and `/lib/firmware/am62x-pru1-fw`. U-Boot loads that M4 ELF when it is present. The default extlinux label selects the three overlay blobs, so SPI0 and P1.20 take the demo mux on the next boot. USB comes up as the Beagle NCM gadget on `usb0` and the ACM serial gadget (`serial-getty@ttyGS0.service`). `pb2-demo` does not stop or reload `78000000.r5f`. If the kernel already started the M4, `pb2-demo` leaves it running.

## Build

Have these in hand: this repo, git, podman or docker, and a network path to GitHub, git.ti.com, and the Zephyr project. The script is `beagle-linux/scripts/build.sh`. It builds the Containerfiles under `containers/`, then compiles inside those images. Do not use a compiler installed on the host. Podman and docker both work. Set `PB2_CONTAINER_ENGINE` to `podman` or `docker`. Unset, the script uses podman when that is installed, and otherwise docker.

From the repo root, this builds userspace, PRU firmware, and Zephyr, in that order:

```
./beagle-linux/scripts/build.sh
```

The kernel module is `./beagle-linux/scripts/build.sh kmod` and needs the Debian board. The Poky image is the kas-container build in [Getting started](#getting-started).

1. Linux userspace and the Debian overlays. Image `pb2-linux-userspace` from `containers/linux/Containerfile` (Debian 13, the aarch64 cross compiler, and `dtc`). The script clones EmbeddedRPC 1.14.0 into `third_party/erpc` if it is missing, and BeagleBoard-DeviceTrees `v6.18.x` commit `550f7711a4696d5f5e92505289379b754e871313` into `third_party/BeagleBoard-DeviceTrees`.

   ```
   ./beagle-linux/scripts/build.sh linux
   ```

   Output: `linux-app/pb2`, plus `beagle-linux/overlays/*.dtbo`. `pb2` is the program `pb2-demo` starts. `spi` is its Linux SPI hello, next to xy, arm, chunk, and say. `PINMUX.md` lists every pin the overlays mux.

2. PRU firmware. Image `pb2-pru-cgt` from `containers/pru-cgt/Containerfile`. The first stage installs TI CGT 2.3.3 on Debian 10, because the installer is a 32-bit stub that needs that older glibc. The second stage is Debian 13 with `clpru` at `/opt/ti-cgt-pru`.

   ```
   ./beagle-linux/scripts/build.sh pru
   ```

   PRU0, the transfer core, is `pru/pru0/` (`main.c` and `AM62x_PRU0.cmd`). PRU1, the SLIP frame, is `pru/pru1/`: the sample loop is `main.c`, the pin shifting is `slip_tx.asm`, and the linker script is `AM62x_PRU1.cmd`. Output: `pru/build/am62x-pru0-fw` and `pru/build/am62x-pru1-fw`.

3. Zephyr on the M4. Image `pb2-zephyr-sdk` from `containers/zephyr/Containerfile`, the Zephyr CI image with `ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-1.0.1`. When `.west/` is missing, the container runs `west init -l manifest` from the repo root, then `west update`, `zephyr-m4/scripts/apply-patches.sh`, and `west build -b pocketbeagle_2/am6254/m4 zephyr-m4/m4`. `west update` checks out the commit in `manifest/west.yml` and drops local edits under `zephyr/`. `zephyr-m4/patches/` is empty: that `main` commit already releases the hardware chip select, so an active-low SPIEN idles high without a local patch.

   ```
   ./beagle-linux/scripts/build.sh zephyr
   ```

   Output: `build/zephyr/pb2_m4.elf`.

4. Patched `pru_rproc`, only for the Debian image. The stock module has no virtio kick, so PRU0's RPMsg firmware will not boot until this module is loaded. The script rsyncs `/usr/src/linux-headers-$PB2_KREL` from `$PB2_SSH` into `.cache/` once, then cross-compiles in the Linux image.

   ```
   ./beagle-linux/scripts/build.sh kmod
   ```

   Output: `beagle-linux/kernel/pru_rproc/pru_rproc.ko`. `PB2_SSH` is the Debian login you chose, and `PB2_KREL` must be `uname -r` on that board.

### Common problems

- **No podman or docker.** Install one of them. `build.sh` runs under either: set `PB2_CONTAINER_ENGINE` to `podman` or `docker`. kas-container takes `KAS_CONTAINER_ENGINE` set to `podman` or `docker`.
- **West checkout.** Do not run `west init -l .` from the repo root. That puts `.west/`, `zephyr/`, and `modules/` in the parent directory. The script uses `west init -l manifest`, which keeps the workspace at the repo root. If an old `.west/` points somewhere else, remove it and run the Zephyr build again. `west update` resets `zephyr/` to the pinned commit.
- **Zephyr pin.** `manifest/west.yml` sets `revision` to a commit hash. The comment on that line says the hash is Zephyr `main` on 11 Oct 2026. The revision is not a branch name and not a tag. `west update` has to fetch that commit from `zephyrproject-rtos/zephyr`.
- **Overlay headers.** `ti/k3-pinctrl.h` is not in this repository. `./beagle-linux/scripts/build.sh linux` clones BeagleBoard-DeviceTrees commit `550f7711a4696d5f5e92505289379b754e871313` into `third_party/BeagleBoard-DeviceTrees` and preprocesses the overlays with it. If that clone fails, the `.dtbo` files are not produced.

### Zephyr devicetree overlay

`zephyr-m4/m4/boards/pocketbeagle_2_am6254_m4.overlay` is a Zephyr devicetree overlay, not a Linux overlay for the Debian kernel. `west build` picks it up by board name (`pocketbeagle_2/am6254/m4`) and compiles it into `pb2_m4.elf`. It shrinks the M4 DDR region so two ranges stay outside the Zephyr image: a 64 KiB bulk buffer at `0x9d9ef000` and the 4 KiB mailbox at `0x9d9ff000`. There is no loadable ELF section at either address.

On the Debian image the overlay is not applied with `dtbo`, configfs, or a reboot. `pb2-demo` copies the ELF to `/lib/firmware/am62-mcu-m4f0_0-fw` and starts remoteproc `5000000.m4fss`. That boot is what brings the overlay's memory map up. The script does not stop or reload `78000000.r5f`. It does not run at boot.

The same overlay also muxes main SPI2 and, once that ELF is running, `main()` clocks the five bytes `hello` at 1 MHz, mode 0, 8-bit MSB. D1 is MOSI and D0 is MISO. That pinmux is inside `pb2_m4.elf`. It is not a Debian dtbo. The M4 console UART is unwired, so the result is also written to the DDR mailbox: `spi hello ok` after the transfer returns, or `spi hello not ready` / `spi hello fail <errno>` if it does not.

That main pinmux is a bare `pinctrl@f4000`. The M4 does not unlock the `ti,control-module` syscon at `0xf0000`. The Debian kernel does, and that is why CLK and MOSI can toggle.

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

P2.29A (ball M22) and P2.31A (ball AA18) are the shorted partners and are disabled. Probe P2.25, P2.27, P2.29, and P2.31. Clock is 1 MHz, SPI mode 0. The payload is the five bytes `hello`.

### Zephyr, header P1

| Signal | Header | Ball | Pad offset |
| --- | --- | --- | --- |
| CLK | P1.08 | A20 | `0x01b0` |
| MOSI (SPI2_D1) | P1.12 | A19 | `0x0198` |
| MISO (SPI2_D0) | P1.10 | B19 | `0x0194` |
| CS0 | P1.06 | E19 | `0x01ac` |

P1.06A (ball AD18), P1.10's other ball A18, and P1.12A (ball AE18) are disabled. Probe P1.06, P1.08, P1.10, and P1.12. Same payload, rate, and mode as Linux.

### Linux overlay file, build, and load

Source: `beagle-linux/overlays/k3-am62-pocketbeagle2-spi0-hello.dtso`.

`./beagle-linux/scripts/build.sh linux` is the one command. It fetches BeagleBoard-DeviceTrees, preprocesses this overlay with `ti/k3-pinctrl.h`, and runs `dtc`, along with `pb2`. The blob is `beagle-linux/overlays/k3-am62-pocketbeagle2-spi0-hello.dtbo`.

The Debian image loads overlays from the boot partition. Copy the blob to `/boot/firmware/overlays/`, then on the default label in `/boot/firmware/extlinux/extlinux.conf` set:

```
fdtoverlays /overlays/k3-am62-pocketbeagle2-spi0-hello.dtbo /overlays/k3-am62-pocketbeagle2-pru1-xy.dtbo /overlays/k3-am62-pocketbeagle2-leftover-gpio.dtbo
```

That takes effect on the next boot. After that, the linux window command `spi` writes `hello` to `/dev/spidev0.0` (main SPI0 is the SPI controller the Debian image enables; the child is `spidev@0` with compatible `rohm,dh2228fv`, which this kernel binds to `spidev`). Pass another path if the node name differs:

```
spi /dev/spidev0.0
```

`pb2 spi` does the same transfer and exits. The linux window prints `Linux wrote the SPI hello, 5 bytes on /dev/spidev0.0`. With the M4 image running, Zephyr's own SPI2 write leaves `spi hello ok` in the DDR mailbox, or `spi hello not ready` / `spi hello fail <errno>` if it does not.

## X and Y through PRU0 to PRU1

PRU0 is the only RPMsg core (channel `rpmsg-raw`, port 30). Linux types `pru0 xy <x> <y>` in the linux window. That text is the RPMsg payload. Zephyr does not open its own channel to PRU0. Linux types `zephyr xy <x> <y>`, eRPC `sendXy` runs on the existing Zephyr RPMsg channel, and Zephyr publishes the same `xy <x> <y>` text in the DDR mailbox at `0x9D9FF000`. PRU0 already polls that mailbox. Both numbers are decimal and fit in 32 bits.

PRU0 writes the pair into PRUSS shared RAM, not the DDR mailbox. The AM62x linker map puts 32 KiB of shared RAM at local address `0x00010000` (`PRU_SHAREDMEM`, constant register C28, length `0x8000`). C28's pointer is that address shifted right by 8, so `0x0100` names this base, and a near access covers 256 bytes. The firmware does not program C28. It uses the direct local address, which is how the arm block is already reached. The SoC address of local `0x00010000` is `0x30050000`.

| Offset from local `0x00010000` | SoC address | What |
| --- | --- | --- |
| `0x00` | `0x30050000` | Arm block, 32 bytes. PRU0 writes it. PRU1's timing loop samples it. |
| `0x40` | `0x30050040` | X/Y record. Still inside the first 256 bytes of shared RAM. |

The record is little-endian `uint32` fields, in this order: `magic`, `seq`, `x`, `y`, `consumed`. Magic is `0x50423258` (`PB2X`). PRU0 stores `x`, then `y`, then the magic, then `seq` last. `consumed` is PRU1's. `pb2 xy` prints the record once. The pru1 window prints it on each sample.

PRU1 is the transmission core. When `seq` changes and the magic matches, it copies X and Y, shifts a frame out, then stores that `seq` in `consumed`. A matching `consumed` means PRU1 finished that sequence. It does not mean a scope saw the pin.

The frame is SLIP (RFC 1055) carried as UART 8N1. Idle is high. Each byte is one UART character: start bit 0, eight data bits least-significant first, stop bit 1. On the BeagleBoard Debian image the PRUSS core clock is 250 MHz, and each bit is 2170 cycles, which is 115200 baud within a fraction of a percent. One iteration of PRU1's timing loop grows by the length of the frame when a new sequence is sent.

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

Source: `beagle-linux/overlays/k3-am62-pocketbeagle2-pru1-xy.dtso`. `./beagle-linux/scripts/build.sh linux` builds the blob next to the SPI overlay. Copy it to `/boot/firmware/overlays/`. The Debian `fdtoverlays` line names this blob with the SPI blob and the leftover-gpio blob. The PRUSS node takes that pin group. The pad does not change until the next boot.

Header pins that Linux, Zephyr, and the PRUs do not already use are muxed as input GPIOs by `beagle-linux/overlays/k3-am62-pocketbeagle2-leftover-gpio.dtso`. Each of those pins uses `AM62X_IOPAD()` (the two MCU partners use `AM62X_MCU_IOPAD()`) and is commented as unused and muxed as Linux GPIO. The same `build.sh linux` step writes `k3-am62-pocketbeagle2-leftover-gpio.dtbo`. Copy the blob to `/boot/firmware/overlays/`. The Debian `fdtoverlays` line names this blob with the other two. The Yocto image selects this blob on the default extlinux label with the other two. The full pin list is `PINMUX.md`. None of these pins is an LED.

From the linux window the commands are `pru0 xy <x> <y>` and `zephyr xy <x> <y>`. PRU0's RPMsg reply names who asked and what it wrote for PRU1. `pb2 xy` prints the shared record, including `consumed`.

## Run

`pb2-demo-image` installs the runner on `PATH` as `/usr/bin/pb2-demo`. U-Boot loads the M4 ELF from `/lib/firmware` when it is present. Run the demo yourself:

```
pb2-demo
```

That starts PRU0 and PRU1. If the M4 is already running, it leaves it. Otherwise it starts the M4. Output is merged on this console. Each line is prefixed `[Linux]`, `[M4]`, `[PRU0]`, or `[PRU1]`. It never stops, reloads, or replaces `78000000.r5f`.

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

The Linux side sends a startup script before it reads commands: ping PRU0, `say hello from linux`, `arm 100000 30`, `chunk bulk-hello`, `xy 41 83`, `spi`, then `pru0 arm 250000 20`.

The Debian image does not ship this runner. [BeagleBoard Debian](#beagleboard-debian) is what that image still needs installed. The Yocto image already has `pb2-demo` on `PATH`.

## What a passing run looks like

Shell prompts are omitted. The typed command is the line above each Linux sentence. The windows print:

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
| `third_party/` | PRU software support package, EmbeddedRPC, and BeagleBoard-DeviceTrees |
| `.cache/` | TI CGT install and kernel headers |
| `pru/build/` | PRU firmware images |
| `linux-app/pb2` | the userspace program |
| `*.ko` | kernel modules, including patched `pru_rproc` |

The Debian image requests DHCP on `usb0`. That note is in [BeagleBoard Debian](#beagleboard-debian). The M4 console balls are header P2.05A and P2.07A. This example does not wire them, so `printk` is not the console you watch. The visible Zephyr console is the RPMsg window. `PINMUX.md` is the pin list.
