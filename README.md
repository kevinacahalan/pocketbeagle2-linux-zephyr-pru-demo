# PocketBeagle 2: Linux, Zephyr, and PRU

Linux on the A53s calls Zephyr functions over eRPC on the existing RPMsg channel, and commands PRU0 over RPMsg. Zephyr asks PRU0 through a small DDR mailbox. PRU0 is the transfer core: it arms PRU1 in PRU shared RAM, and it also writes a 32-bit X and a 32-bit Y there. PRU1 does not use RPMsg. It runs a cycle-counted loop, samples the arm block once per period, and when the X/Y sequence changes it shifts that pair out on a header pin. Linux hands Zephyr bulk chunks through a separate DDR region (`zephyr-m4/include/pb2_shm.h`). The device-manager R5 is not loaded or stopped.

The board still runs the BeagleBoard Debian image. That tree is `beagle-linux/`, and it is the active test target. Yocto is only a slot for kas, layers, and config. Laptop builds run in containers.

`LAYOUT.md` is the directory map.

## Build

Every compile runs in a container. The script is `beagle-linux/scripts/build.sh`. It uses podman. Do not use a compiler installed on the laptop. `./beagle-linux/scripts/build.sh` with no argument builds userspace, PRU firmware, and Zephyr, in that order. The kernel module is a separate step because it needs the board's kernel headers.

1. Linux userspace, in `debian:13`. This also clones EmbeddedRPC 1.14.0 into `third_party/erpc` if it is missing.

   ```
   ./beagle-linux/scripts/build.sh linux
   ```

   Output: `beagle-linux/linux/pb2-ipc`.

2. PRU firmware. If `clpru` is not already under `.cache/ti/`, the TI CGT 2.3.3 installer runs in `debian:buster` (the installer is a 32-bit stub that needs that older glibc). The firmware itself is compiled in `debian:13`.

   ```
   ./beagle-linux/scripts/build.sh pru
   ```

   PRU0, the transfer core, is `pru/pru0/` (`main.c` and `AM62x_PRU0.cmd`). PRU1, the SLIP frame, is `pru/pru1/` (`main.c` and `AM62x_PRU1.cmd`). Output: `pru/build/am62x-pru0-fw` and `pru/build/am62x-pru1-fw`.

3. Zephyr on the M4, in `ghcr.io/zephyrproject-rtos/ci:latest` with the SDK at `/opt/toolchains/zephyr-sdk-1.0.1`. The container runs `west init -l manifest` from the repo root when `.west/` is missing, then `west update` and `west build -b pocketbeagle_2/am6254/m4 zephyr-m4/m4`. Do not run `west init -l .` from the repo root.

   ```
   ./beagle-linux/scripts/build.sh zephyr
   ```

   Output: `build/zephyr/pb2_m4.elf`.

4. Patched `pru_rproc`, in `debian:13`. The stock module has no virtio kick, so PRU0's RPMsg firmware will not boot until this module is loaded. The script rsyncs `/usr/src/linux-headers-$PB2_KREL` from `$PB2_SSH` into `.cache/` once, then cross-compiles in the container. `PB2_SSH` is required. `PB2_KREL` defaults to `6.18.52-arm64-k3-r56`, the kernel of the BeagleBoard Debian Trixie image this example was exercised on. Set it to `uname -r` on your board if that differs.

   ```
   ./beagle-linux/scripts/build.sh kmod
   ```

   Output: `beagle-linux/kernel/pru_rproc/pru_rproc.ko`.

### Zephyr devicetree overlay

`zephyr-m4/m4/boards/pocketbeagle_2_am6254_m4.overlay` is a Zephyr devicetree overlay, not a Linux overlay for the Debian kernel. `west build` picks it up by board name (`pocketbeagle_2/am6254/m4`) and compiles it into `pb2_m4.elf`. It shrinks the M4 DDR region so two ranges stay outside the Zephyr image: a 64 KiB bulk buffer at `0x9d9ef000` and the 4 KiB mailbox at `0x9d9ff000`. There is no loadable ELF section at either address.

On the flashed Debian image the overlay is not applied with `dtbo`, configfs, or a reboot. `pb2-tmux.sh` copies the ELF to `/lib/firmware/am62-mcu-m4f0_0-fw` and starts remoteproc `5000000.m4fss`. That boot is what brings the overlay's memory map up. The script does not stop or reload `78000000.r5f`.

The same overlay also muxes main SPI2 and, once that ELF is running, `main()` clocks the five bytes `hello` at 1 MHz, mode 0, 8-bit MSB. D1 is MOSI and D0 is MISO. That pinmux is inside `pb2_m4.elf`. It is not a Debian dtbo. The M4 console UART is unwired, so the result is also written to the DDR mailbox: `spi hello ok` after the transfer returns, or `spi hello not ready` / `spi hello fail <errno>` if it does not.

## SPI hello

Linux and Zephyr use different controllers. Linux uses main SPI0 (`0x20100000`). Zephyr uses main SPI2 (`0x20120000`). Mode 0 on the SPI0 balls and mode 1 on the SPI2 balls are the SPI functions. The PRU GPO/GPI modes on those balls are other mux values and are not selected. The M4 console is MCU UART0 on P2.05A (ball B5) and P2.07A (ball A5); those pads are not in either overlay. RPMsg does not use header pins.

MCU SPI0 (`0x04b00000`) is not used. The cape tables only bring `MCU_SPI0_CS2` (P2.09A) and `MCU_SPI0_CS3` (P1.26, ball D6) out to the headers, not a clock and data pair.

Some header pins short two SoC balls. Only one ball is muxed to SPI. The partner ball is held in GPIO mode 7 with its output disabled.

### Linux, header P2

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

Source: `beagle-linux/linux/k3-am62-pocketbeagle2-spi0-hello.dtso`.

`./beagle-linux/scripts/build.sh linux` builds it in `debian:13` with `dtc`, along with `pb2-ipc` and `pb2-spi`. The blob is `beagle-linux/linux/k3-am62-pocketbeagle2-spi0-hello.dtbo`.

The flashed image loads overlays from the boot partition, not from configfs (`/sys/kernel/config` on this image has no device-tree overlays). Copy the blob to `/boot/firmware/overlays/`, then on the default label in `/boot/firmware/extlinux/extlinux.conf` set:

```
fdtoverlays /overlays/k3-am62-pocketbeagle2-spi0-hello.dtbo
```

That takes effect on the next boot. After that, `pb2-spi` writes `hello` to `/dev/spidev0.0` (main SPI0 is the SPI controller this image enables; the child is `spidev@0` with compatible `rohm,dh2228fv`, which this kernel binds to `spidev`). Pass another path if the node name differs:

```
./pb2-spi /dev/spidev0.0
```

The software transfers ran. `pb2-spi` wrote 5 bytes on `/dev/spidev0.0`. With the M4 image running, the DDR mailbox read `spi hello ok`. The Saleae was not connected, so this repository has no logic-analyzer capture of those pins.

## X and Y through PRU0 to PRU1

PRU0 is the only RPMsg core (channel `rpmsg-raw`, port 30). Linux types `pru0 xy <x> <y>` in the linux window. That text is the RPMsg payload. Zephyr does not open its own channel to PRU0. Linux types `zephyr xy <x> <y>`, eRPC `sendXy` runs on the existing Zephyr RPMsg channel, and Zephyr publishes the same `xy <x> <y>` text in the DDR mailbox at `0x9D9FF000`. PRU0 already polls that mailbox. Both numbers are decimal and fit in 32 bits.

PRU0 writes the pair into PRUSS shared RAM, not the DDR mailbox. The AM62x linker map puts 32 KiB of shared RAM at local address `0x00010000` (`PRU_SHAREDMEM`, constant register C28, length `0x8000`). C28's pointer is that address shifted right by 8, so `0x0100` names this base, and a near access covers 256 bytes. The firmware does not program C28. It uses the direct local address, which is how the arm block is already reached. The SoC address of local `0x00010000` is `0x30050000`.

| Offset from local `0x00010000` | SoC address | What |
| --- | --- | --- |
| `0x00` | `0x30050000` | Arm block, 32 bytes. PRU0 writes it. PRU1's timing loop samples it. |
| `0x40` | `0x30050040` | X/Y record. Still inside the first 256 bytes of shared RAM. |

The record is little-endian `uint32` fields, in this order: `magic`, `seq`, `x`, `y`, `consumed`. Magic is `0x50423258` (`PB2X`). PRU0 stores `x`, then `y`, then the magic, then `seq` last. `consumed` is PRU1's. `pb2-ipc xy` prints the record once. The pru1 window prints it on each sample.

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

The pin is header P1.20, ball Y24, pad offset `0x00cc`, mux mode 2 (`PR0_PRU1_GPO5`), which is PRU1 R30 bit 5. It is not a Linux SPI0 pin (P2.29, P2.25, P2.27, P2.31) and not a Zephyr SPI2 pin (P1.08, P1.12, P1.10, P1.06). The expansion table shows one ball on P1.20.

Source: `beagle-linux/linux/k3-am62-pocketbeagle2-pru1-xy.dtso`. `./beagle-linux/scripts/build.sh linux` builds the blob with `dtc` next to the SPI overlay. Copy it to `/boot/firmware/overlays/` and add it to `fdtoverlays` on the default extlinux label, the same way as the SPI overlay. The PRUSS node takes that pin group. The pad does not change until the next boot. This repository does not include a capture of P1.20.

From the linux window the commands are `pru0 xy <x> <y>` and `zephyr xy <x> <y>`. PRU0's RPMsg reply has the shape `pru0: xy seq N x X y Y`. `pb2-ipc xy` prints the shared record, including `consumed`.

## Run

Copy the build products to the board over SSH. `PB2_SSH` is `user@address` for your board. `pb2-tmux.sh` expects this layout under `/home/user/pb2-demo/` (use the account's home, not this placeholder):

```
export PB2_SSH=user@192.0.2.2
ssh "$PB2_SSH" 'mkdir -p /home/user/pb2-demo/fw'
scp beagle-linux/linux/pb2-ipc beagle-linux/scripts/pb2-tmux.sh \
  beagle-linux/kernel/pru_rproc/pru_rproc.ko \
  "$PB2_SSH":/home/user/pb2-demo/
scp pru/build/am62x-pru0-fw pru/build/am62x-pru1-fw \
  "$PB2_SSH":/home/user/pb2-demo/fw/
scp build/zephyr/pb2_m4.elf \
  "$PB2_SSH":/home/user/pb2-demo/fw/am62-mcu-m4f0_0-fw
```

`192.0.2.2` and `user` are placeholders. Replace them with the address and account you set up below.

On the board, from that directory:

```
./pb2-tmux.sh
```

The script needs sudo to stop and start the M4 and the two PRUs, insert `pru_rproc.ko` when the running kernel is missing `pru_rproc_kick`, and grant `pb2-ipc` `CAP_SYS_RAWIO` so it can open `/dev/mem`. It leaves `78000000.r5f` alone. It then opens tmux session `pb2` with windows `linux`, `zephyr`, `pru0`, and `pru1`. Attach with `tmux attach -t pb2`. From the linux window: `zephyr say <text>`, `zephyr arm <period> <count>`, `zephyr chunk <text>`, `zephyr xy <x> <y>`, `pru0 xy <x> <y>`, or `pru0 <text>`. `zephyr arm` asks PRU0, through the mailbox, to arm PRU1. `pru0 arm <period> <count>` does that directly. `chunk` writes the bulk region and then calls Zephyr to check it. `xy` hands PRU0 the pair described above.

The linux window still sends a startup script before it reads commands: ping PRU0, `say hello from linux`, `arm 100000 30`, `chunk bulk-hello`, then `pru0 arm 250000 20`. That script is not the X/Y measurement below.

## What a passing run looks like

These are the program lines from the board. They are not a scope capture.

Linux sent `pru0 xy 17 29` on PRU0's RPMsg channel. PRU0 replied:

```
pru0: xy seq 1 x 17 y 29
```

`pb2-ipc xy` printed the shared record:

```
xy cpu 0x30050040 magic=50423258 seq=1 x=17 y=29 consumed=1
```

Zephyr `sendXy(41, 83)` ran over eRPC. The client printed:

```
zephyr -> xy 41 83
zephyr xy 41 83 -> ok
```

The same record then printed:

```
xy cpu 0x30050040 magic=50423258 seq=2 x=41 y=83 consumed=2
```

Magic `0x50423258` is the record at `0x30050040`. `consumed` matched `seq` both times, so PRU1 had finished shifting that sequence. The frame is SLIP on header P1.20. The P1.20 overlay was not installed, and the wire was not scoped.

On SPI, `pb2-spi` wrote 5 bytes on `/dev/spidev0.0`. The Zephyr mailbox read `spi hello ok`. The Saleae was not connected.

## Checked in and temporary

Checked in: `beagle-linux/`, `zephyr-m4/`, `pru/` sources, `yocto/`, `manifest/west.yml`, this README, and `LAYOUT.md`.

Left on disk and not committed:

| Path | What it is |
| --- | --- |
| `.west/`, `zephyr/`, `modules/`, `build/`, `bootloader/`, `tools/` | West workspace |
| `third_party/` | PRU software support package and EmbeddedRPC clones |
| `.cache/` | TI CGT install and kernel headers |
| `pru/build/` | PRU firmware images |
| `beagle-linux/linux/pb2-ipc`, `beagle-linux/linux/pb2-spi` | userspace binaries |
| `*.ko` | kernel modules, including patched `pru_rproc` |

## Reach the board

Nothing here assumes a USB network share is already configured, a particular USB hub, or a particular serial adapter.

Plug in the board's USB gadget. On the host, find the new network interface (CDC NCM). Give that link an address yourself. The Debian image requests DHCP on `usb0`, so one way is to turn on connection sharing for the new interface and note the address the board receives. A static address on both ends works too. Then:

```
export PB2_SSH=user@192.0.2.2
ping 192.0.2.2
ssh "$PB2_SSH"
```

`192.0.2.2` is a documentation address. Use the address your board actually got, and `user` is that board's account.

A serial console is optional. Any USB-serial adapter on the debug header at 115200 8N1 is enough. The M4's own UART is not wired to that header; the visible Zephyr console is the RPMsg window.
