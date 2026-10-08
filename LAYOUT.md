# Layout

The root keeps this note, `README.md`, `.gitignore`, `manifest/` (the west manifest), and `containers/` (the three build images). Generated trees stay at the root and are not committed.

## Beagle Linux — `beagle-linux/`

The Debian image already on the board. This is the active test target.

- `beagle-linux/linux/` — userspace IPC (`pb2-ipc`, `pb2-spi`)
- `beagle-linux/kernel/pru_rproc/` — patched `pru_rproc` with virtio kick
- `beagle-linux/scripts/build.sh` — builds the container images, then compiles
- `beagle-linux/scripts/pb2-demo` — on-target runner, installed as `/usr/bin/pb2-demo`

## Zephyr — `zephyr-m4/` and `manifest/`

- `zephyr-m4/m4/` — M4 app, board `pocketbeagle_2/am6254/m4`
- `zephyr-m4/include/pb2_shm.h` — mailbox header shared with the PRU
- `manifest/west.yml` — Zephyr v4.4.2, `import: true`

`manifest/` stays at the repo root. `west init -l manifest` from the repo root sets the workspace topdir to the parent of that directory, which is where `.west/`, `zephyr/`, `modules/`, and `build/` already live. Do not run `west init -l .` from the repo root.

## PRU — `pru/pru0/` and `pru/pru1/`

`pru/pru0/` is the transfer core: its own `main.c` and `AM62x_PRU0.cmd`. `pru/pru1/` sends the SLIP frame: `main.c` is the sample loop, `slip_tx.asm` shifts the frame onto the pin, and `AM62x_PRU1.cmd` is its linker script. Images land in `pru/build/`. The TI support package is cloned to `third_party/` when building. Neither the images nor that package is committed.

## Yocto — `yocto/`

`yocto/kas/pb2-poky.yml` is the kas-container config for `pb2-demo-image`, a minimum Poky image for machine `pocketbeagle2`. `yocto/layers/meta-pb2` compiles the Linux demo, the Zephyr M4 firmware, and both PRU firmwares, and installs `pb2-demo` on `PATH`. The image does not start those cores at boot. `yocto/sources/`, `yocto/downloads/`, and `yocto/build/` are generated and not committed. The build does not flash the board. The Debian image under `beagle-linux/` stays the active test target.

## Containers — `containers/`

- `containers/linux/Containerfile` — Linux userspace (`pb2-linux-userspace`)
- `containers/pru-cgt/Containerfile` — TI CGT, then the PRU firmware image (`pb2-pru-cgt`)
- `containers/zephyr/Containerfile` — Zephyr SDK (`pb2-zephyr-sdk`)

## Temporary

`README.md` lists what is checked in and what is left untracked.
