SUMMARY = "PocketBeagle 2 PRU0 and PRU1 firmware"
DESCRIPTION = "Compiles both PRU images with TI CGT and the PRU software support package. Installs am62x-pru0-fw and am62x-pru1-fw."
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = ""

DEPENDS = "ti-cgt-pru-native"

FILESEXTRAPATHS:prepend := "${@os.path.abspath(os.path.join(os.path.dirname(d.getVar('FILE')), '..', '..', '..', '..', '..'))}/:"

SRC_URI = " \
    file://pru/Makefile;subdir=src/pru \
    file://pru/pru0/main.c;subdir=src/pru/pru0 \
    file://pru/pru0/AM62x_PRU0.cmd;subdir=src/pru/pru0 \
    file://pru/pru1/main.c;subdir=src/pru/pru1 \
    file://pru/pru1/slip_tx.asm;subdir=src/pru/pru1 \
    file://pru/pru1/AM62x_PRU1.cmd;subdir=src/pru/pru1 \
    file://zephyr-m4/include/pb2_shm.h;subdir=src/zephyr-m4/include \
    git://git.ti.com/git/pru-software-support-package/pru-software-support-package.git;protocol=https;nobranch=1;destsuffix=pssp;name=pssp \
"
SRCREV_pssp = "28107daa1d184059f67676ba49d0fcaebb0f6a96"

S = "${UNPACKDIR}/src"
PRU_CGT = "${STAGING_DATADIR_NATIVE}/ti-cgt-pru"

COMPATIBLE_MACHINE = "pocketbeagle2"
PACKAGE_ARCH = "${MACHINE_ARCH}"
INSANE_SKIP:${PN} += "arch"

do_configure[noexec] = "1"

do_compile () {
    rm -rf ${S}/pru/build
    oe_runmake -C ${S}/pru \
        PRU_CGT="${PRU_CGT}" \
        PSSP="${UNPACKDIR}/pssp"
    test -f ${S}/pru/build/am62x-pru0-fw
    test -f ${S}/pru/build/am62x-pru1-fw
}

do_install () {
    install -d ${D}${nonarch_base_libdir}/firmware
    install -m 0644 ${S}/pru/build/am62x-pru0-fw ${D}${nonarch_base_libdir}/firmware/am62x-pru0-fw
    install -m 0644 ${S}/pru/build/am62x-pru1-fw ${D}${nonarch_base_libdir}/firmware/am62x-pru1-fw
}

FILES:${PN} = "${nonarch_base_libdir}/firmware/am62x-pru0-fw ${nonarch_base_libdir}/firmware/am62x-pru1-fw"
