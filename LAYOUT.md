# Layout

```
.
├── README.md
├── PINMUX.md
├── LICENSE
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
├── out/                   generated
└── .cache/                generated
```

`README.md` — what the demo does and how to build it.

`PINMUX.md` — header pins and which core owns them.

`LICENSE` — MIT, plus the files that keep their own license.

`linux-app/` — the `pb2` userspace program. `linux-app/pb2` is a build product.

`beagle-linux/` — Debian overlays, the patched `pru_rproc` module, the build script, and the on-board scripts.

`beagle-linux/overlays/` — Linux device-tree overlays. The `.dtbo` blobs are build products.

`beagle-linux/kernel/pru_rproc/` — `pru_rproc` with virtio kick.

`beagle-linux/scripts/` — `build.sh` compiles in the containers; `pb2-demo` starts the demo on the board; `install-beagle.sh` sets up the Debian image once, so a reboot needs no manual steps.

`zephyr/` — M4 app, the shared-memory header every core includes (`include/pb2_shm.h`), the eRPC interface (`erpc/pb2.erpc`) and its generated code (`erpc/shim/`), and the west manifest.

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

`out/pb2-beagle/` — `build.sh bundle` output, copied to a Debian board and installed with its `install-beagle.sh`.
