# Layout

Four areas. The root keeps this note, `README.md`, `.gitignore`, and `manifest/` (the west manifest). Generated trees stay at the root and are not committed.

## Beagle Linux — `beagle-linux/`

The Debian image already on the board. This is the active test target.

- `beagle-linux/linux/` — userspace IPC (`pb2-ipc`, `pb2-spi`)
- `beagle-linux/kernel/pru_rproc/` — patched `pru_rproc` with virtio kick
- `beagle-linux/scripts/build.sh` — container builds
- `beagle-linux/scripts/pb2-tmux.sh` — board demo

## Zephyr — `zephyr-m4/` and `manifest/`

- `zephyr-m4/m4/` — M4 app, board `pocketbeagle_2/am6254/m4`
- `zephyr-m4/include/pb2_shm.h` — mailbox header shared with the PRU
- `manifest/west.yml` — Zephyr v4.4.2, `import: true`

`manifest/` stays at the repo root. `west init -l manifest` from the repo root sets the workspace topdir to the parent of that directory, which is where `.west/`, `zephyr/`, `modules/`, and `build/` already live. Do not run `west init -l .` from the repo root.

## PRU — `pru/pru0/` and `pru/pru1/`

`pru/pru0/` is the transfer core: its own `main.c` and `AM62x_PRU0.cmd`. `pru/pru1/` sends the SLIP frame: its own `main.c` and `AM62x_PRU1.cmd`. Images land in `pru/build/`. The TI support package is cloned to `third_party/` when building. Neither the images nor that package is committed.

## Yocto — `yocto/`

`kas/`, `layers/`, and `config/` only. No image is built or deployed from this tree. The board keeps the Debian image.

## Temporary

`README.md` lists what is checked in and what is left untracked.
