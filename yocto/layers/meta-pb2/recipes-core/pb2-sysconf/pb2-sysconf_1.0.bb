SUMMARY = "First-boot Beagle account settings"
DESCRIPTION = "Reads account keys from the boot-partition sysconf.txt. The shipped file leaves them commented, so first boot does not rename an account or set a password."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://pb2-sysconf \
    file://pb2-sysconf.service \
"

S = "${UNPACKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "pb2-sysconf.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${sbindir}
    install -d ${D}${systemd_system_unitdir}
    install -m 0755 ${S}/pb2-sysconf ${D}${sbindir}/pb2-sysconf
    install -m 0644 ${S}/pb2-sysconf.service ${D}${systemd_system_unitdir}/
}

RDEPENDS:${PN} += "shadow"
