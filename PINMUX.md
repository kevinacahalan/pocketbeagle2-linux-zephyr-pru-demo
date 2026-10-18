# Pinmux

This example muxes pins from three places: the Linux SPI0 overlay, the Zephyr M4 devicetree overlay, and the PRU1 transmit overlay. A fourth Linux overlay muxes the header GPIOs those cores do not already use. RPMsg does not use a header pin. PRU0 does not mux a header pin.

No pin this example muxes is an LED, and the example does not drive an LED. The four green user LEDs are on the board, not on header P1 or P2. The board tree muxes them, and the kernel LED triggers own them. See [Board LEDs](#board-leds).

Linux overlays include `ti/k3-pinctrl.h` and write each pad with `AM62X_IOPAD()`. MCU-domain pads use `AM62X_MCU_IOPAD()`. The header is `src/arm64/ti/k3-pinctrl.h` from BeagleBoard-DeviceTrees `v6.18.x` commit `550f7711a4696d5f5e92505289379b754e871313`. This repository does not contain a copy of that header. `./beagle-linux/scripts/build.sh linux` clones the tree and preprocesses the overlays the way that tree's Makefile does (`cpp -nostdinc -I src/arm64`, then `dtc`).

A source line looks like:

```
AM62X_IOPAD(0x00cc, PIN_OUTPUT|PIN_DRIVE_STRENGTH_NOMINAL, 2) /* (Y24) VOUT0_DATA5.PR0_PRU1_GPO5 */
```

`PIN_OUTPUT` compiles to `0x10000`, `PIN_INPUT` to `0x54000`, and `PIN_DISABLE` to `0x200000`. `PIN_DRIVE_STRENGTH_NOMINAL` is 0. The mux mode is added to that cell. An input GPIO is mode 7, which compiles to `0x54007` (the same value the board tree uses). A shorted partner held off is mode 7 with the driver disabled, which compiles to `0x200007`. A mode 2 output compiles to `0x10002`. The Zephyr SPI2 pins stay in the M4 overlay and still use `K3_PINMUX()`.

## Linux SPI0

Core: Linux, main SPI0 at `0x20100000`. Source: `beagle-linux/overlays/k3-am62-pocketbeagle2-spi0-hello.dtso`. Mode 0 is the SPI function.

```
AM62X_IOPAD(0x01bc, PIN_OUTPUT|PIN_DRIVE_STRENGTH_NOMINAL, 0) /* (A14) SPI0_CLK */
AM62X_IOPAD(0x01c4, PIN_OUTPUT|PIN_DRIVE_STRENGTH_NOMINAL, 0) /* (B14) SPI0_D1 */
AM62X_IOPAD(0x01c0, PIN_INPUT|PIN_DRIVE_STRENGTH_NOMINAL, 0) /* (B13) SPI0_D0 */
AM62X_IOPAD(0x01b4, PIN_OUTPUT|PIN_DRIVE_STRENGTH_NOMINAL, 0) /* (A13) SPI0_CS0 */
AM62X_IOPAD(0x00a4, PIN_DISABLE|PIN_DRIVE_STRENGTH_NOMINAL, 7) /* (M22) GPMC0_DIR.GPIO0_40 */
AM62X_IOPAD(0x0170, PIN_DISABLE|PIN_DRIVE_STRENGTH_NOMINAL, 7) /* (AA18) RGMII2_TD1.GPIO0_90 */
```

| Signal | Header | Ball | Pad offset | Compiled cell |
| --- | --- | --- | --- | --- |
| CLK | P2.29 | A14 | `0x01bc` | `0x10000` output |
| MOSI (SPI0_D1) | P2.25 | B14 | `0x01c4` | `0x10000` output |
| MISO (SPI0_D0) | P2.27 | B13 | `0x01c0` | `0x54000` input |
| CS0 | P2.31 | A13 | `0x01b4` | `0x10000` output |
| held off | P2.29A | M22 | `0x00a4` | `0x200007` |
| held off | P2.31A | AA18 | `0x0170` | `0x200007` |

P2.29A and P2.31A are shorted to the clock and chip-select pins. They are not driven. No LED.

## Zephyr SPI2

Core: Zephyr on the M4. The pinmux is inside `pb2_m4.elf`, from `zephyr/m4/boards/pocketbeagle_2_am6254_m4.overlay`. It is not a Debian dtbo. Mode 1 is the SPI2 function. Controller: main SPI2 at `0x20120000`.

| Signal | Header | Ball | Pad offset | Mux |
| --- | --- | --- | --- | --- |
| CLK | P1.08 | A20 | `0x01b0` | mode 1 output |
| MOSI (SPI2_D1) | P1.12 | A19 | `0x0198` | mode 1 output |
| MISO (SPI2_D0) | P1.10 | B19 | `0x0194` | mode 1 input |
| CS0 | P1.06 | E19 | `0x01ac` | mode 1 output |
| held off | P1.06A | AD18 | `0x0140` | mode 7, output off |
| held off | P1.10 partner | A18 | `0x01f0` | mode 7, output off |
| held off | P1.12A | AE18 | `0x013c` | mode 7, output off |

The board tree names ball B19 `P1.10(B19)` (GPIO1 line 7) and ball A18 `P1.10(A18)` (GPIO1 line 30). The expansion table prints those two balls in the other order. This file follows the board tree and the Zephyr overlay: B19 is the MISO pin, A18 is the shorted partner and is held off. No LED.

## PRU1 transmit

Core: PRU1. Linux applies the pad; PRU1 drives R30 bit 5. Source: `beagle-linux/overlays/k3-am62-pocketbeagle2-pru1-xy.dtso`.

```
AM62X_IOPAD(0x00cc, PIN_OUTPUT|PIN_DRIVE_STRENGTH_NOMINAL, 2) /* (Y24) VOUT0_DATA5.PR0_PRU1_GPO5 */
```

| Signal | Header | Ball | Pad offset | Compiled cell |
| --- | --- | --- | --- | --- |
| TX (`PR0_PRU1_GPO5`) | P1.20 | Y24 | `0x00cc` | `0x10002` mode 2 output |

One ball. Not an SPI0 or SPI2 pin. No LED.

## Leftover GPIOs

Source: `beagle-linux/overlays/k3-am62-pocketbeagle2-leftover-gpio.dtso`. Every pin in that file is a leftover: unused, muxed as a Linux GPIO, and not used by the demo. The active ball is `AM62X_IOPAD(..., PIN_INPUT|PIN_DRIVE_STRENGTH_NOMINAL, 7)`, which compiles to `0x54007`. A shorted partner is `PIN_DISABLE` mode 7, which compiles to `0x200007`, so it does not fight the header pin. The two MCU partners use `AM62X_MCU_IOPAD()`. The demo does not read or drive these lines. No LED.

```
AM62X_IOPAD(0x01a0, PIN_INPUT|PIN_DRIVE_STRENGTH_NOMINAL, 7) /* leftover P1.02 (E18) MCASP0_AXR0.GPIO1_10, unused and muxed as Linux GPIO */
AM62X_IOPAD(0x0164, PIN_DISABLE|PIN_DRIVE_STRENGTH_NOMINAL, 7) /* leftover P1.02A (AA19) RGMII2_TX_CTL.GPIO0_87, shorted to P1.02 and held off, unused and muxed as Linux GPIO */
AM62X_MCU_IOPAD(0x0040, PIN_DISABLE|PIN_DRIVE_STRENGTH_NOMINAL, 7) /* leftover P2.09A (D4) MCU_MCAN1_RX.MCU_GPIO0_16, shorted to P2.09 and held off, unused and muxed as Linux GPIO */
```

`main_gpio0` takes the main-domain group. The base tree leaves that pinctrl empty, so this overlay does not drop a mux the board already applied. `mcu_gpio0` takes the two MCU partners.

| Header | Ball | Signal | Pad offset | Role |
| --- | --- | --- | --- | --- |
| P1.02 | E18 | `MCASP0_AXR0.GPIO1_10` | `0x01a0` | input |
| P1.02A | AA19 | `RGMII2_TX_CTL.GPIO0_87` | `0x0164` | held off |
| P1.04 | D20 | `MCASP0_AFSX.GPIO1_12` | `0x01a8` | input |
| P1.04A | Y18 | `RGMII2_TD0.GPIO0_89` | `0x016c` | held off |
| P1.13 | N20 | `GPMC0_BE1n.GPIO0_36` | `0x0094` | input |
| P1.19 | AD22 | `RGMII2_RX_CTL.GPIO1_1` | `0x017c` | input |
| P1.21 | AE22 | `RGMII2_RD3.GPIO1_6` | `0x0190` | input |
| P1.23 | AC21 | `RGMII2_RD2.GPIO1_5` | `0x018c` | input |
| P1.25 | AB20 | `RGMII2_RD1.GPIO1_4` | `0x0188` | input |
| P1.27 | AE23 | `RGMII2_RD0.GPIO1_3` | `0x0184` | input |
| P1.29 | Y20 | `VOUT0_DE.GPIO0_62` | `0x00fc` | input |
| P1.31 | Y22 | `VOUT0_DATA14.GPIO0_59` | `0x00f0` | input |
| P1.33 | A17 | `I2C1_SDA.GPIO1_29` | `0x01ec` | input |
| P1.33A | AA23 | `VOUT0_DATA11.GPIO0_56` | `0x00e4` | held off |
| P1.34 | AD23 | `RGMII2_RXC.GPIO1_2` | `0x0180` | input |
| P1.35 | AE21 | `RGMII2_TXC.GPIO0_88` | `0x0168` | input |
| P2.02 | U22 | `VOUT0_DATA0.GPIO0_45` | `0x00b8` | input |
| P2.03 | AB22 | `MDIO0_MDIO.GPIO0_85` | `0x015c` | input |
| P2.03A | B18 | `MCASP0_AXR1.GPIO1_9` | `0x019c` | held off |
| P2.04 | V24 | `VOUT0_DATA1.GPIO0_46` | `0x00bc` | input |
| P2.06 | W25 | `VOUT0_DATA2.GPIO0_47` | `0x00c0` | input |
| P2.08 | W24 | `VOUT0_DATA3.GPIO0_48` | `0x00c4` | input |
| P2.09 | A15 | `UART0_CTSn.GPIO1_22` | `0x01d0` | input |
| P2.09A | D4 | `MCU_MCAN1_RX.MCU_GPIO0_16` | `0x0040` (MCU) | held off |
| P2.10 | AD21 | `RGMII2_TD2.GPIO0_91` | `0x0174` | input |
| P2.11 | B15 | `UART0_RTSn.GPIO1_23` | `0x01d4` | input |
| P2.11A | E5 | `MCU_MCAN1_TX.MCU_GPIO0_15` | `0x003c` (MCU) | held off |
| P2.17 | AC24 | `VOUT0_PCLK.GPIO0_64` | `0x0104` | input |
| P2.18 | V21 | `VOUT0_DATA8.GPIO0_53` | `0x00d8` | input |
| P2.19 | AC20 | `RGMII2_TD3.GPIO1_0` | `0x0178` | input |
| P2.20 | Y25 | `VOUT0_DATA4.GPIO0_49` | `0x00c8` | input |
| P2.22 | AC25 | `VOUT0_VSYNC.GPIO0_63` | `0x0100` | input |
| P2.24 | Y23 | `VOUT0_DATA6.GPIO0_51` | `0x00d0` | input |
| P2.28 | AB24 | `VOUT0_HSYNC.GPIO0_61` | `0x00f8` | input |
| P2.30 | AA24 | `VOUT0_DATA13.GPIO0_58` | `0x00ec` | input |
| P2.32 | AB25 | `VOUT0_DATA12.GPIO0_57` | `0x00e8` | input |
| P2.33 | AA25 | `VOUT0_DATA7.GPIO0_52` | `0x00d4` | input |
| P2.34 | AA21 | `VOUT0_DATA15.GPIO0_60` | `0x00f4` | input |
| P2.35 | W21 | `VOUT0_DATA9.GPIO0_54` | `0x00dc` | input |
| P2.36 | C13 | `SPI0_CS1.GPIO1_16` | `0x01b8` | input |

P1.19, P1.21, P1.23, P1.25, P1.27, P2.02, P2.35, and P2.36 are also analog inputs on the expansion table. This overlay muxes the digital ball only.

`./beagle-linux/scripts/build.sh linux` builds this blob next to the SPI and PRU1 blobs. On Debian, `install-beagle.sh` copies all three to `/boot/firmware/overlays/` and adds them to the default extlinux label. The Yocto image selects all three on its default label. The pad change waits until the next boot.

## Left on the board

These header pins are not leftovers. The overlay does not take them.

| Header | Ball | Why it stays |
| --- | --- | --- |
| P1.06, P1.06A, P1.08, P1.10, P1.10 partner, P1.12, P1.12A | E19, AD18, A20, B19, A18, A19, AE18 | Zephyr SPI2 |
| P1.20 | Y24 | PRU1 transmit |
| P1.26, P1.26A | K24, D6 | Linux I2C2 data (mode 1 on K24). The shorted MCU ball is left alone. |
| P1.28, P1.28A | K22, B3 | Linux I2C2 clock (mode 1 on K22). The shorted MCU ball is left alone. |
| P1.30 | E14 | Linux UART0 TX, board console, pad `0x01cc` |
| P1.32 | D14 | Linux UART0 RX, board console, pad `0x01c8` |
| P1.36, P1.36 partner | B17, V20 | Linux EHRPWM2 on B17, pad `0x01e8` mode 8. The shorted ball is not enabled here. |
| P2.01, P2.01A | B20, AD24 | Linux ECAP2 on B20. AD24 is already held off by the board tree. |
| P2.05, P2.05A | C15, B5 | M4 console, MCU UART0 RX on B5, MCU pad `0x0014`. The shorted main ball is not taken. |
| P2.07, P2.07A | E15, A5 | M4 console, MCU UART0 TX on A5, MCU pad `0x0018`. The shorted main ball is not taken. |
| P2.25, P2.27, P2.29, P2.29A, P2.31, P2.31A | B14, B13, A14, M22, A13, AA18 | Linux SPI0 |
| P1.03 | F18 | Board USB1_DRVVBUS, pad `0x0258` mode 0. Not a demo GPIO. |

Power, ground, USB data, and the analog reference pins are not GPIO pads and are not in the overlay. The M4 console balls are on the header. This example does not mux them, so M4 `printk` is not the console to watch. The visible Zephyr console is RPMsg.

## Board LEDs

This example does not mux or drive an LED.

The board tree's `led-default-pins` group muxes four on-board green LEDs as GPIO outputs (`0x10007`, mode 7). They are not header pins. The kernel `gpio-leds` node owns them:

| Label | Ball | Pad offset | GPIO | What controls it |
| --- | --- | --- | --- | --- |
| `beaglebone:green:usr1` | F24 | `0x0018` | GPIO0_6 | `heartbeat` |
| `beaglebone:green:usr2` | F25 | `0x0014` | GPIO0_5 | `mmc1` |
| `beaglebone:green:usr3` | G24 | `0x0010` | GPIO0_4 | `activity` |
| `beaglebone:green:usr4` | E25 | `0x000c` | GPIO0_3 | default off, no trigger |
