SUMMARY = "PocketBeagle 2 demo image with Beagle bring-up"
DESCRIPTION = "systemd image for PocketBeagle 2. extlinux selects the demo overlays, the Beagle USB gadgets enumerate, and /root holds pb2, pb2-demo, and the firmwares. Login is the default root account. pb2-demo is on PATH."

require recipes-core/images/core-image-minimal.bb

IMAGE_INSTALL:append = " \
    pb2-demo \
    pb2-pru-fw \
    pb2-m4-fw \
    pb2-boot \
    pb2-sysconf \
    bb-usb-gadgets \
    kernel-modules \
    systemd \
    systemd-networkd \
"

IMAGE_FEATURES:append = " ssh-server-dropbear"

# FAT, then the ext4 root U-Boot reads as mmc 1:2, then swap.
WKS_FILE = "pb2-beagle.wks"

IMAGE_BOOT_FILES:append = " \
    Image \
    k3-am62-pocketbeagle2.dtb;ti/k3-am62-pocketbeagle2.dtb \
    extlinux.conf;extlinux/extlinux.conf \
    sysconf.txt;sysconf.txt \
    k3-am62-pocketbeagle2-spi0-hello.dtbo;overlays/k3-am62-pocketbeagle2-spi0-hello.dtbo \
    k3-am62-pocketbeagle2-pru1-xy.dtbo;overlays/k3-am62-pocketbeagle2-pru1-xy.dtbo \
    k3-am62-pocketbeagle2-leftover-gpio.dtbo;overlays/k3-am62-pocketbeagle2-leftover-gpio.dtbo \
"

do_image_wic[depends] += "pb2-demo:do_deploy pb2-boot:do_deploy"

# Root is the account this image already ships. Do not add another
# user, and do not set or clear a password here.

pb2_fstab() {
    cat >> ${IMAGE_ROOTFS}${sysconfdir}/fstab <<'EOF'
/dev/mmcblk1p1  /boot/firmware  vfat  user,uid=0,gid=0,defaults  0  2
/dev/mmcblk1p3  none            swap  sw  0  0
EOF
    install -d ${IMAGE_ROOTFS}/boot/firmware
}

# What root sees on login. /usr/bin/pb2-demo stays on PATH.
# The same binaries and firmwares also stay in /usr/bin and /lib/firmware.
pb2_root_demo() {
    home=${IMAGE_ROOTFS}/root
    fw=${IMAGE_ROOTFS}${nonarch_base_libdir}/firmware
    install -d -m 0700 -o 0 -g 0 ${home}
    install -m 0755 -o 0 -g 0 ${IMAGE_ROOTFS}${bindir}/pb2 ${home}/pb2
    install -m 0755 -o 0 -g 0 ${IMAGE_ROOTFS}${bindir}/pb2-demo ${home}/pb2-demo
    install -m 0644 -o 0 -g 0 ${fw}/am62x-pru0-fw ${home}/am62x-pru0-fw
    install -m 0644 -o 0 -g 0 ${fw}/am62x-pru1-fw ${home}/am62x-pru1-fw
    install -m 0644 -o 0 -g 0 ${fw}/am62-mcu-m4f0_0-fw ${home}/pb2_m4.elf
    cat > ${home}/README.md <<'EOF'
pb2-demo starts the demo.
pb2 is the Linux program it runs.
pb2_m4.elf is the Zephyr M4 firmware.
am62x-pru0-fw and am62x-pru1-fw are the two PRU firmwares.

From this directory:

./pb2-demo
EOF
    chmod 0644 ${home}/README.md
    chown 0:0 ${home}/README.md
}

ROOTFS_POSTPROCESS_COMMAND:append = " pb2_fstab; pb2_root_demo;"

COMPATIBLE_MACHINE = "pocketbeagle2"
