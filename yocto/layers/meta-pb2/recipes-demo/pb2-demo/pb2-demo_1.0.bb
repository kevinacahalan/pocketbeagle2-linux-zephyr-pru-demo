SUMMARY = "PocketBeagle 2 Linux demo (pb2-ipc and pb2-spi)"
DESCRIPTION = "Cross-compiles the in-tree userspace demo and its device-tree overlays. EmbeddedRPC is fetched and compiled; nothing is copied from a previous laptop build."
LICENSE = "BSD-3-Clause & CLOSED"
LIC_FILES_CHKSUM = "file://third_party/erpc/LICENSE;md5=b66f32a90f9577a5a3255c21d79bc619"

DEPENDS = "dtc-native"

FILESEXTRAPATHS:prepend := "${@os.path.abspath(os.path.join(os.path.dirname(d.getVar('FILE')), '..', '..', '..', '..', '..'))}/:"

SRC_URI = " \
    file://beagle-linux/linux/pb2-ipc.c;subdir=src/beagle-linux/linux \
    file://beagle-linux/linux/pb2-spi.c;subdir=src/beagle-linux/linux \
    file://beagle-linux/linux/pb2_erpc_linux.cpp;subdir=src/beagle-linux/linux \
    file://beagle-linux/linux/pb2_erpc_linux.h;subdir=src/beagle-linux/linux \
    file://beagle-linux/linux/erpc_config.h;subdir=src/beagle-linux/linux \
    file://beagle-linux/linux/Makefile;subdir=src/beagle-linux/linux \
    file://beagle-linux/linux/k3-am62-pocketbeagle2-spi0-hello.dtso;subdir=src/beagle-linux/linux \
    file://beagle-linux/linux/k3-am62-pocketbeagle2-pru1-xy.dtso;subdir=src/beagle-linux/linux \
    file://zephyr-m4/include/pb2_shm.h;subdir=src/zephyr-m4/include \
    file://zephyr-m4/erpc/shim;subdir=src/zephyr-m4/erpc \
    git://github.com/EmbeddedRPC/erpc.git;protocol=https;nobranch=1;destsuffix=src/third_party/erpc;name=erpc \
"
SRCREV_erpc = "e8d5684a920e023a49cec875c04ad05ddc8521e6"

S = "${WORKDIR}/src"

COMPATIBLE_MACHINE = "pocketbeagle2"

do_configure[noexec] = "1"

do_compile () {
    # Drop anything a host build may have left in the fetched tree, then
    # compile with the Yocto compiler. The Makefile links with CXXFLAGS.
    rm -f ${S}/beagle-linux/linux/pb2-ipc \
        ${S}/beagle-linux/linux/pb2-spi \
        ${S}/beagle-linux/linux/*.o \
        ${S}/beagle-linux/linux/*.dtbo
    oe_runmake -C ${S}/beagle-linux/linux \
        CC="${CC}" \
        CXX="${CXX}" \
        CFLAGS="${CFLAGS}" \
        CXXFLAGS="${CXXFLAGS} ${LDFLAGS} -std=c++17 -fno-exceptions -fno-rtti"
}

do_install () {
    install -d ${D}${bindir}
    install -d ${D}${nonarch_base_libdir}/firmware
    install -m 0755 ${S}/beagle-linux/linux/pb2-ipc ${D}${bindir}/pb2-ipc
    install -m 0755 ${S}/beagle-linux/linux/pb2-spi ${D}${bindir}/pb2-spi
    # Overlays are built here and installed as files. They are not added
    # to the boot fdtoverlays list.
    install -m 0644 ${S}/beagle-linux/linux/k3-am62-pocketbeagle2-spi0-hello.dtbo \
        ${D}${nonarch_base_libdir}/firmware/
    install -m 0644 ${S}/beagle-linux/linux/k3-am62-pocketbeagle2-pru1-xy.dtbo \
        ${D}${nonarch_base_libdir}/firmware/
}

FILES:${PN} += "${nonarch_base_libdir}/firmware/*.dtbo"
