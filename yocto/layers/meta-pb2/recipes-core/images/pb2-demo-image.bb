SUMMARY = "PocketBeagle 2 demo image with Beagle bring-up"
DESCRIPTION = "systemd image for PocketBeagle 2. extlinux selects the demo overlays, the Beagle USB gadgets enumerate, and the uid 1000 home holds the demo the way a Debian build tree does. pb2-demo is on PATH. Does not load or stop the device-manager R5."

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

# Debian IoT card: FAT boot, swap, root. extlinux uses mmcblk1p3.
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

# The PocketBeagle 2 Debian IoT image ships this uid 1000 account.
# A password is not baked in. First boot reads sysconf.txt.
inherit extrausers
EXTRA_USERS_PARAMS = " \
    groupadd -g 1000 debian; \
    useradd -u 1000 -g 1000 -m -s /bin/bash -c 'Beagle User' debian; \
"

pb2_fstab() {
    cat >> ${IMAGE_ROOTFS}${sysconfdir}/fstab <<'EOF'
/dev/mmcblk1p1  /boot/firmware  vfat  user,uid=1000,gid=1000,defaults  0  2
/dev/mmcblk1p2  none            swap  sw  0  0
EOF
    install -d ${IMAGE_ROOTFS}/boot/firmware
}

# Debian build tree paths, under the uid 1000 home. /usr/bin/pb2-demo stays on PATH.
pb2_home_demo() {
    home=${IMAGE_ROOTFS}/home/debian
    fw=${IMAGE_ROOTFS}${nonarch_base_libdir}/firmware
    install -d -o 1000 -g 1000 ${home}/linux-app
    install -d -o 1000 -g 1000 ${home}/beagle-linux/scripts
    install -d -o 1000 -g 1000 ${home}/pru/build
    install -d -o 1000 -g 1000 ${home}/build/zephyr
    install -m 0755 -o 1000 -g 1000 ${IMAGE_ROOTFS}${bindir}/pb2 ${home}/linux-app/pb2
    install -m 0755 -o 1000 -g 1000 ${IMAGE_ROOTFS}${bindir}/pb2-demo ${home}/beagle-linux/scripts/pb2-demo
    install -m 0644 -o 1000 -g 1000 ${fw}/am62x-pru0-fw ${home}/pru/build/am62x-pru0-fw
    install -m 0644 -o 1000 -g 1000 ${fw}/am62x-pru1-fw ${home}/pru/build/am62x-pru1-fw
    install -m 0644 -o 1000 -g 1000 ${fw}/am62-mcu-m4f0_0-fw ${home}/build/zephyr/pb2_m4.elf
}

ROOTFS_POSTPROCESS_COMMAND:append = " pb2_fstab; pb2_home_demo;"

COMPATIBLE_MACHINE = "pocketbeagle2"
