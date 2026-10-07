# Yocto

Kas files, layers, and local config only. Nothing in this directory is an image, and nothing here is deployed to the board.

The board keeps running the Debian image under `beagle-linux/` until a kas-container image exists. Do not bitbake from this tree, and do not flash the board from it.

- `kas/` — kas-container configuration, when there is one
- `layers/` — board layers, when there are any
- `config/` — local config fragments, when there are any
