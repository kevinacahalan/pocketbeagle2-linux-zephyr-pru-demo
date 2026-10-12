SUMMARY = "BeagleBoard.org USB gadgets"
DESCRIPTION = "PocketBeagle 2 USB gadget startup from bb-usb-gadgets (Debian trixie). Brings up the NCM usb0 gadget and the ACM serial gadget the way that image does."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://copyright;md5=61a7c7c298652e07e0c0260ef8707942"

SRC_URI = " \
    file://copyright \
    file://bb-start-usb-gadgets \
    file://bb-start-acm-ncm-rndis-old-gadget \
    file://bb-usb-gadgets.service \
    file://bb-boot \
    file://eth0-DHCP.network \
    file://usb0-DHCP.network \
    file://usb0-DHCPServer.network \
    file://usb0-only-DHCPServer.network \
    file://systemd-networkd-wait-online.override.conf \
"

S = "${UNPACKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "bb-usb-gadgets.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${bindir}
    install -d ${D}${sysconfdir}/bbb.io/templates
    install -d ${D}${sysconfdir}/default
    install -d ${D}${sysconfdir}/systemd/network
    install -d ${D}${sysconfdir}/systemd/system/systemd-networkd-wait-online.service.d
    install -d ${D}${systemd_system_unitdir}

    install -m 0755 ${S}/bb-start-usb-gadgets ${D}${bindir}/
    install -m 0755 ${S}/bb-start-acm-ncm-rndis-old-gadget ${D}${bindir}/
    install -m 0644 ${S}/bb-usb-gadgets.service ${D}${systemd_system_unitdir}/

    install -m 0644 ${S}/bb-boot ${D}${sysconfdir}/bbb.io/templates/bb-boot
    install -m 0644 ${S}/bb-boot ${D}${sysconfdir}/default/bb-boot
    install -m 0644 ${S}/eth0-DHCP.network ${D}${sysconfdir}/bbb.io/templates/eth0-DHCP.network
    install -m 0644 ${S}/usb0-DHCP.network ${D}${sysconfdir}/bbb.io/templates/usb0-DHCP.network
    install -m 0644 ${S}/usb0-DHCPServer.network ${D}${sysconfdir}/bbb.io/templates/usb0-DHCPServer.network
    install -m 0644 ${S}/usb0-only-DHCPServer.network ${D}${sysconfdir}/bbb.io/templates/usb0-only-DHCPServer.network
    install -m 0644 ${S}/systemd-networkd-wait-online.override.conf \
        ${D}${sysconfdir}/bbb.io/templates/systemd-networkd-wait-online.override.conf

    # The Debian postinst copies these into place when they are missing.
    install -m 0644 ${S}/usb0-DHCPServer.network ${D}${sysconfdir}/systemd/network/usb0.network
    install -m 0644 ${S}/eth0-DHCP.network ${D}${sysconfdir}/systemd/network/eth0.network
    install -m 0644 ${S}/systemd-networkd-wait-online.override.conf \
        ${D}${sysconfdir}/systemd/system/systemd-networkd-wait-online.service.d/override.conf
}

FILES:${PN} += "${systemd_system_unitdir}/bb-usb-gadgets.service"

RDEPENDS:${PN} += "bash bc systemd systemd-networkd"
