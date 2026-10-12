SUMMARY = "PocketBeagle 2 extlinux boot files"
DESCRIPTION = "extlinux.conf in the Beagle microSD layout, with the demo overlays selected on the default label, plus the sysconf.txt template. Installed under /boot/firmware and deployed for the FAT boot partition."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://extlinux.conf \
    file://sysconf.txt \
"

S = "${UNPACKDIR}"

inherit deploy

COMPATIBLE_MACHINE = "pocketbeagle2"

do_install() {
    install -d ${D}/boot/firmware/extlinux
    install -m 0644 ${S}/extlinux.conf ${D}/boot/firmware/extlinux/extlinux.conf
    install -m 0644 ${S}/sysconf.txt ${D}/boot/firmware/sysconf.txt
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${S}/extlinux.conf ${DEPLOYDIR}/extlinux.conf
    install -m 0644 ${S}/sysconf.txt ${DEPLOYDIR}/sysconf.txt
}
addtask deploy after do_install before do_build

FILES:${PN} = "/boot/firmware/extlinux/extlinux.conf /boot/firmware/sysconf.txt"
