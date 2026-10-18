SUMMARY = "PocketBeagle 2 Zephyr M4 demo firmware"
DESCRIPTION = "Builds the M4 app in zephyr/ for pocketbeagle_2/am6254/m4 with the Zephyr SDK and a west workspace fetched during the build. Installs the ELF as am62-mcu-m4f0_0-fw."
# The demo is MIT, EmbeddedRPC is BSD-3-Clause, and Zephyr is Apache-2.0.
LICENSE = "MIT & BSD-3-Clause & Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302 \
    file://third_party/erpc/LICENSE;md5=b66f32a90f9577a5a3255c21d79bc619"

DEPENDS = "cmake-native ninja-native gperf-native dtc-native python3-native python3-pip-native git-native"

FILESEXTRAPATHS:prepend := "${@os.path.abspath(os.path.join(os.path.dirname(d.getVar('FILE')), '..', '..', '..', '..', '..'))}/:"

SRC_URI = " \
    file://zephyr/west.yml \
    file://zephyr/m4 \
    file://zephyr/include \
    file://zephyr/erpc \
    file://zephyr/scripts \
    git://github.com/EmbeddedRPC/erpc.git;protocol=https;nobranch=1;destsuffix=src/third_party/erpc;name=erpc \
    https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/zephyr-sdk-1.0.1_linux-x86_64_minimal.tar.xz;name=sdk;subdir=sdk \
    https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/toolchain_gnu_linux-x86_64_arm-zephyr-eabi.tar.xz;name=armtc;subdir=sdk/zephyr-sdk-1.0.1/gnu \
"
SRC_URI[sdk.sha256sum] = "ca9bc0ff66fafca1dac9d592a36d953cf16d096a9d09b1c0357f021cf9f6a7eb"
SRC_URI[armtc.sha256sum] = "21b85981cb5a1818d9bc53d82af80f208946ec038b982ff1907287572ed3a634"
SRCREV_erpc = "e8d5684a920e023a49cec875c04ad05ddc8521e6"

S = "${UNPACKDIR}/src"
ZEPHYR_SDK = "${UNPACKDIR}/sdk/zephyr-sdk-1.0.1"

COMPATIBLE_MACHINE = "pocketbeagle2"
PACKAGE_ARCH = "${MACHINE_ARCH}"
# The Zephyr ELF records the toolchain directory. The M4 does not use that path.
INSANE_SKIP:${PN} += "arch buildpaths"

do_configure () {
    rm -rf ${S}/zephyr
    cp -a ${UNPACKDIR}/zephyr ${S}/zephyr
    test -f ${S}/third_party/erpc/LICENSE
    test -x ${ZEPHYR_SDK}/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc || \
        test -x ${ZEPHYR_SDK}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc
}

do_compile[network] = "1"
do_compile () {
    export PATH="${STAGING_BINDIR_NATIVE}:${PATH}"
    export HOME="${WORKDIR}/home"
    mkdir -p "${HOME}"
    export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK}"
    export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
    export CMAKE_BUILD_PARALLEL_LEVEL="${@oe.utils.parallel_make(d)}"
    export GIT_CONFIG_COUNT=1
    export GIT_CONFIG_KEY_0=safe.directory
    export GIT_CONFIG_VALUE_0='*'
    pip3 install --target ${WORKDIR}/py west
    export PYTHONPATH="${WORKDIR}/py${PYTHONPATH:+:$PYTHONPATH}"
    export PATH="${WORKDIR}/py/bin:${PATH}"
    # The repo root already has a .west workspace. west walks parents, so
    # init this firmware tree from /tmp instead of inside that workspace.
    ws=/tmp/pb2-m4-west
    rm -rf "$ws"
    mkdir -p "$ws"
    cp -a ${S}/. "$ws/"
    cd "$ws"
    west init -l zephyr
    west update
    "$ws/zephyr/scripts/apply-patches.sh"
    if [ -f zephyr/upstream/scripts/requirements-base.txt ]; then
        pip3 install --target ${WORKDIR}/py -r zephyr/upstream/scripts/requirements-base.txt
    fi
    west build -b pocketbeagle_2/am6254/m4 zephyr/m4 -d "$ws/zbuild" -p always
    if [ -f "$ws/zbuild/zephyr/pb2_m4.elf" ]; then
        elf="$ws/zbuild/zephyr/pb2_m4.elf"
    else
        elf="$ws/zbuild/zephyr/zephyr.elf"
    fi
    test -f "$elf"
    install -m 0644 "$elf" ${WORKDIR}/pb2_m4.elf
}

do_install () {
    install -d ${D}${nonarch_base_libdir}/firmware
    install -m 0644 ${WORKDIR}/pb2_m4.elf ${D}${nonarch_base_libdir}/firmware/am62-mcu-m4f0_0-fw
}

FILES:${PN} = "${nonarch_base_libdir}/firmware/am62-mcu-m4f0_0-fw"
