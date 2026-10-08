SUMMARY = "Minimum Poky image that boots PocketBeagle 2 and carries the demo"
DESCRIPTION = "core-image-minimal plus the Linux demo, the Zephyr M4 firmware, and both PRU firmwares. pb2-demo is on PATH. Nothing in this image starts the M4 or the PRUs at boot. Does not load or stop the device-manager R5."

require recipes-core/images/core-image-minimal.bb

IMAGE_INSTALL:append = " pb2-demo pb2-pru-fw pb2-m4-fw kernel-modules"
IMAGE_FEATURES:append = " ssh-server-dropbear"

COMPATIBLE_MACHINE = "pocketbeagle2"
