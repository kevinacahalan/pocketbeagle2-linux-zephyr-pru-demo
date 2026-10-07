# Layout

Four areas. The root keeps this note, `README.md`, `.gitignore`, and `manifest/` (the west manifest). Generated trees stay at the root and are not committed.

## Beagle Linux — `beagle-linux/`

The Debian image already on the board. This is the active test target.

- `linux/` — userspace IPC (`pb2-ipc`)
- `kernel/pru_rproc/` — patched `pru_rproc` with virtio kick
- `scripts/build.sh` — container builds
- `scripts/pb2-tmux.sh` — board demo

## Zephyr — `zephyr-m4/` and `manifest/`

- `zephyr-m4/m4/` — M4 app, board `pocketbeagle_2/am6254/m4`
- `zephyr-m4/include/pb2_shm.h` — mailbox header shared with the PRU
- `manifest/west.yml` — Zephyr v4.4.2, `import: true`

`manifest/` stays at the repo root. `west init -l manifest` from the repo root sets the workspace topdir to the parent of that directory, which is where `.west/`, `zephyr/`, `modules/`, and `build/` already live. Do not run `west init -l .` from the repo root.

## PRU — `pru/`

Firmware for both cores (`-DPRU_CORE=0` or `1`). Images land in `pru/build/`. The TI support package is cloned to `third_party/` when building. Neither is committed.

## Yocto — `yocto/`

`kas/`, `layers/`, and `config/` only. No image is built or deployed from this tree. The board keeps the Debian image.

## Temporary

`README.md` lists what is checked in and what is left untracked.
