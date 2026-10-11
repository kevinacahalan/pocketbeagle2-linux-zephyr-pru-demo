SUMMARY = "PocketBeagle 2 Linux demo"
DESCRIPTION = "Cross-compiles the pb2 userspace program and the Debian device-tree overlays, and installs the pb2-demo runner. EmbeddedRPC is fetched and compiled."
LICENSE = "BSD-3-Clause & CLOSED"
LIC_FILES_CHKSUM = "file://third_party/erpc/LICENSE;md5=b66f32a90f9577a5a3255c21d79bc619"

DEPENDS = "dtc-native"

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
    file://zephyr-m4/include/pb2_shm.h;subdir=src \
    file://zephyr-m4/erpc/shim;subdir=src \
    file://beagle-linux/scripts/pb2-demo;subdir=src \
    git://github.com/EmbeddedRPC/erpc.git;protocol=https;nobranch=1;destsuffix=src/third_party/erpc;name=erpc \
"
SRCREV_erpc = "e8d5684a920e023a49cec875c04ad05ddc8521e6"

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
    oe_runmake -C ${S}/beagle-linux/overlays
}

do_install () {
    install -d ${D}${bindir}
    install -d ${D}${nonarch_base_libdir}/firmware
    install -m 0755 ${S}/linux-app/pb2 ${D}${bindir}/pb2
    install -m 0755 ${S}/beagle-linux/scripts/pb2-demo ${D}${bindir}/pb2-demo
    # Overlays are built here and installed as files. They are not added
    # to the boot fdtoverlays list.
    install -m 0644 ${S}/beagle-linux/overlays/k3-am62-pocketbeagle2-spi0-hello.dtbo \
        ${D}${nonarch_base_libdir}/firmware/
    install -m 0644 ${S}/beagle-linux/overlays/k3-am62-pocketbeagle2-pru1-xy.dtbo \
        ${D}${nonarch_base_libdir}/firmware/
    install -m 0644 ${S}/beagle-linux/overlays/k3-am62-pocketbeagle2-leftover-gpio.dtbo \
        ${D}${nonarch_base_libdir}/firmware/
}

FILES:${PN} += "${nonarch_base_libdir}/firmware/*.dtbo"

# The runner is a shell script. It is not a boot service.
RDEPENDS:${PN} += "bash tmux libcap"
