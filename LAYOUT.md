# Layout

```
.
├── README.md
├── PINMUX.md
├── linux-app/
├── beagle-linux/
│   ├── overlays/
│   ├── kernel/pru_rproc/
│   └── scripts/
├── zephyr/
│   ├── m4/
│   ├── include/
│   ├── erpc/
│   ├── west.yml
│   └── upstream/          generated
├── pru/
│   ├── pru0/
│   ├── pru1/
│   └── build/             generated
├── yocto/
│   ├── kas/
│   ├── layers/meta-pb2/
│   ├── sources/           generated
│   ├── downloads/         generated
│   ├── build/             generated
│   └── sstate-cache/      generated
├── containers/
│   ├── linux/
│   ├── pru-cgt/
│   └── zephyr/
├── .west/                 generated
├── modules/               generated
├── bootloader/            generated
├── tools/                 generated
├── build/                 generated
├── third_party/           generated
└── .cache/                generated
```

`README.md` — what the demo does and how to build it.

`PINMUX.md` — header pins and which core owns them.

`linux-app/` — the `pb2` userspace program. `linux-app/pb2` is a build product.

`beagle-linux/` — Debian overlays, the patched `pru_rproc` module, and the on-target runner.

`beagle-linux/overlays/` — Linux device-tree overlays. The `.dtbo` blobs are build products.

`beagle-linux/kernel/pru_rproc/` — `pru_rproc` with virtio kick.

`beagle-linux/scripts/` — `build.sh` compiles in the containers; `pb2-demo` starts the demo on the board; `install-beagle.sh` sets up the Debian image once, so a reboot needs no manual steps.

`zephyr/` — M4 app, shared mailbox header, eRPC shim, and the west manifest.

`zephyr/m4/` — Zephyr app, board `pocketbeagle_2/am6254/m4`.

`zephyr/west.yml` — pins Zephyr at `zephyr/upstream`. From the repo root, `west init -l zephyr`.

`zephyr/upstream/` — generated upstream Zephyr checkout.

`.west/`, `modules/`, `bootloader/`, `tools/`, `build/` — generated west workspace. `build/zephyr/pb2_m4.elf` is the M4 image.

`pru/pru0/` — transfer core.

`pru/pru1/` — SLIP frame.

`pru/build/` — generated PRU images, `am62x-pru0-fw` and `am62x-pru1-fw`.

`yocto/` — kas config and the `meta-pb2` layer. Recipes there build `pb2`, the M4 ELF, and both PRU images.

`containers/` — Containerfiles for the Linux, PRU, and Zephyr build images.

`third_party/` — generated checkouts used by the build. Not vendored.
