SUMMARY = "PocketBeagle 2 Linux demo"
DESCRIPTION = "Cross-compiles the pb2 userspace program and the Debian device-tree overlays, and installs the pb2-demo runner. EmbeddedRPC is fetched and compiled. BeagleBoard-DeviceTrees is fetched so the overlays can include ti/k3-pinctrl.h."
# The demo is MIT, EmbeddedRPC is BSD-3-Clause, and k3-pinctrl.h is GPL-2.0-only OR MIT.
LICENSE = "MIT & BSD-3-Clause & (GPL-2.0-only | MIT)"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302 \
    file://third_party/erpc/LICENSE;md5=b66f32a90f9577a5a3255c21d79bc619 \
    file://third_party/BeagleBoard-DeviceTrees/src/arm64/ti/k3-pinctrl.h;beginline=1;endline=1;md5=d3f11af791a00b9505045ea598ecc0c7 \
    file://third_party/BeagleBoard-DeviceTrees/LICENSES/MIT.txt;md5=3912d958d00bac4a6b550f75d7c806bb \
    file://third_party/BeagleBoard-DeviceTrees/LICENSES/GPL-2.0-only.txt;md5=3d26203303a722dedc6bf909d95ba815 \
"

DEPENDS = "dtc-native"

inherit deploy

FILESEXTRAPATHS:prepend := "${@os.path.abspath(os.path.join(os.path.dirname(d.getVar('FILE')), '..', '..', '..', '..', '..'))}/:"

SRC_URI = " \
    file://linux-app/pb2.c;subdir=src \
    file://linux-app/pb2_erpc_linux.cpp;subdir=src \
    file://linux-app/pb2_erpc_linux.h;subdir=src \
    file://linux-app/erpc_config.h;subdir=src \
    file://linux-app/Makefile;subdir=src \
    file://beagle-linux/overlays/Makefile;subdir=src \
    file://beagle-linux/overlays/k3-am62-pocketbeagle2-spi0-hello.dtso;subdir=src \
    file://beagle-linux/overlays/k3-am62-pocketbeagle2-pru1-xy.dtso;subdir=src \
    file://beagle-linux/overlays/k3-am62-pocketbeagle2-leftover-gpio.dtso;subdir=src \
    file://zephyr/include/pb2_shm.h;subdir=src \
    file://zephyr/erpc/shim;subdir=src \
    file://beagle-linux/scripts/pb2-demo;subdir=src \
    git://github.com/EmbeddedRPC/erpc.git;protocol=https;nobranch=1;destsuffix=src/third_party/erpc;name=erpc \
    git://github.com/beagleboard/BeagleBoard-DeviceTrees.git;protocol=https;nobranch=1;destsuffix=src/third_party/BeagleBoard-DeviceTrees;name=bbdt \
"
SRCREV_erpc = "e8d5684a920e023a49cec875c04ad05ddc8521e6"
SRCREV_bbdt = "550f7711a4696d5f5e92505289379b754e871313"
SRCREV_FORMAT = "erpc_bbdt"

S = "${UNPACKDIR}/src"

COMPATIBLE_MACHINE = "pocketbeagle2"

do_configure[noexec] = "1"

do_compile () {
    # Drop anything a host build may have left in the fetched tree, then
    # compile with the Yocto compiler. The Makefile links with CXXFLAGS.
    rm -f ${S}/linux-app/pb2 \
        ${S}/linux-app/*.o \
        ${S}/beagle-linux/overlays/*.dtbo
    oe_runmake -C ${S}/linux-app \
        CC="${CC}" \
        CXX="${CXX}" \
        CFLAGS="${CFLAGS}" \
        LDFLAGS="${LDFLAGS}" \
        CXXFLAGS="${CXXFLAGS} -std=c++17 -fno-exceptions -fno-rtti"
    oe_runmake -C ${S}/beagle-linux/overlays \
        BBDT=${S}/third_party/BeagleBoard-DeviceTrees \
        CPP="${BUILD_CC} -E"
}

do_install () {
    install -d ${D}${bindir}
    install -d ${D}${nonarch_base_libdir}/firmware
    install -d ${D}/boot/firmware/overlays
    install -m 0755 ${S}/linux-app/pb2 ${D}${bindir}/pb2
    install -m 0755 ${S}/beagle-linux/scripts/pb2-demo ${D}${bindir}/pb2-demo
    # Selected from extlinux.conf. Copies also live under /lib/firmware.
    for o in \
        k3-am62-pocketbeagle2-spi0-hello.dtbo \
        k3-am62-pocketbeagle2-pru1-xy.dtbo \
        k3-am62-pocketbeagle2-leftover-gpio.dtbo
    do
        install -m 0644 ${S}/beagle-linux/overlays/$o ${D}${nonarch_base_libdir}/firmware/
        install -m 0644 ${S}/beagle-linux/overlays/$o ${D}/boot/firmware/overlays/
    done
}

do_deploy() {
    install -d ${DEPLOYDIR}
    for o in \
        k3-am62-pocketbeagle2-spi0-hello.dtbo \
        k3-am62-pocketbeagle2-pru1-xy.dtbo \
        k3-am62-pocketbeagle2-leftover-gpio.dtbo
    do
        install -m 0644 ${S}/beagle-linux/overlays/$o ${DEPLOYDIR}/
    done
}
addtask deploy after do_compile before do_build

FILES:${PN} += " \
    ${nonarch_base_libdir}/firmware/*.dtbo \
    /boot/firmware/overlays/*.dtbo \
"

# The runner is a shell script. It is not a boot service.
RDEPENDS:${PN} += "bash tmux libcap"
