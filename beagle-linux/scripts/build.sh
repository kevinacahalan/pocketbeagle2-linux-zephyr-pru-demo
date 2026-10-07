#!/bin/bash
# Containerized builds: Zephyr in the official Zephyr image, PRU with TI CGT,
# Linux userspace with the aarch64 cross compiler.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CACHE=$ROOT/.cache
PSSP=$ROOT/third_party/pru-software-support-package
PSSP_REV=28107daa1d184059f67676ba49d0fcaebb0f6a96
CGT=$CACHE/ti/ti-cgt-pru_2.3.3
INSTALLER=$CACHE/ti_cgt_pru_2.3.3_linux_installer_x86.bin

install_cgt() {
	if [[ -x $CGT/bin/clpru ]]; then
		return
	fi
	mkdir -p "$CACHE"
	if [[ ! -f $INSTALLER ]]; then
		curl -fL -o "$INSTALLER" \
			https://software-dl.ti.com/codegen/esd/cgt_public_sw/PRU/2.3.3/ti_cgt_pru_2.3.3_linux_installer_x86.bin
	fi
	chmod +x "$INSTALLER"
	# The installer is a 32-bit Bitrock stub that only starts on older glibc.
	podman run --rm --security-opt label=disable \
		-v "$CACHE:/cache" \
		docker.io/library/debian:buster \
		bash -lc 'sed -i "s|deb.debian.org|archive.debian.org|g; s|security.debian.org|archive.debian.org|g" /etc/apt/sources.list && printf "Acquire::Check-Valid-Until false;\n" > /etc/apt/apt.conf.d/99no-check && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y libc6-i386 && cp /cache/ti_cgt_pru_2.3.3_linux_installer_x86.bin /tmp/inst.bin && chmod +x /tmp/inst.bin && /tmp/inst.bin --mode unattended --prefix /cache/ti'
	if [[ ! -x $CGT/bin/clpru ]]; then
		echo "clpru was not installed at $CGT" >&2
		find "$CACHE" -name clpru -type f >&2 || true
		exit 1
	fi
}

ERPC=$ROOT/third_party/erpc
ERPC_REV=1.14.0

fetch_erpc() {
	if [[ -f $ERPC/zephyr/module.yml ]]; then
		return
	fi
	rm -rf "$ERPC"
	git clone --depth 1 --branch "$ERPC_REV" https://github.com/EmbeddedRPC/erpc.git "$ERPC"
}

fetch_pssp() {
	if [[ -f $PSSP/lib/rpmsg_lib.lib ]]; then
		return
	fi
	rm -rf "$PSSP"
	git clone --depth 1 https://git.ti.com/git/pru-software-support-package/pru-software-support-package.git "$PSSP"
	git -C "$PSSP" fetch --depth 1 origin "$PSSP_REV"
	git -C "$PSSP" checkout "$PSSP_REV"
}

build_pru() {
	install_cgt
	fetch_pssp
	podman run --rm --security-opt label=disable \
		-v "$ROOT:/work" \
		-v "$CGT:/opt/ti-cgt-pru:ro" \
		-w /work \
		docker.io/library/debian:13 \
		bash -lc 'apt-get update && apt-get install -y make && make -C pru PRU_CGT=/opt/ti-cgt-pru PSSP=/work/third_party/pru-software-support-package'
}

# The PRU remoteproc patch has to match the board kernel. Headers are copied
# from the board once; the module itself is cross-compiled here.
build_kmod() {
	local krel=${PB2_KREL:-6.18.52-arm64-k3-r56}
	local headers=$CACHE/linux-headers-$krel
	local host=${PB2_SSH:-}
	if [[ -z $host ]]; then
		echo "Set PB2_SSH to user@board (SSH to the board, which must have the matching kernel headers)." >&2
		echo "Set PB2_KREL if uname -r on the board is not $krel." >&2
		exit 1
	fi
	if [[ ! -f $headers/Makefile ]]; then
		echo "fetching kernel headers from $host"
		mkdir -p "$headers"
		rsync -a "$host:/usr/src/linux-headers-$krel/" "$headers/"
	fi
	podman run --rm --security-opt label=disable \
		-v "$ROOT:/work" \
		-v "$headers:/headers:ro" \
		-w /work \
		docker.io/library/debian:13 \
		bash -lc 'set -euo pipefail
			apt-get update
			DEBIAN_FRONTEND=noninteractive apt-get install -y make gcc g++ gcc-aarch64-linux-gnu libc6-dev-arm64-cross flex bison bc
			rm -rf /tmp/kheaders
			cp -a /headers /tmp/kheaders
			# Headers from the board contain aarch64 host tools. Rebuild the two
			# the module build actually runs.
			gcc -O2 -I/tmp/kheaders/scripts/include -o /tmp/kheaders/scripts/basic/fixdep /tmp/kheaders/scripts/basic/fixdep.c
			gcc -O2 -I/tmp/kheaders/scripts/mod -I/tmp/kheaders/scripts/include \
				-o /tmp/kheaders/scripts/mod/modpost \
				/tmp/kheaders/scripts/mod/modpost.c \
				/tmp/kheaders/scripts/mod/file2alias.c \
				/tmp/kheaders/scripts/mod/sumversion.c \
				/tmp/kheaders/scripts/mod/symsearch.c
			make -C /tmp/kheaders M=/work/beagle-linux/kernel/pru_rproc ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
			'
}

build_linux() {
	fetch_erpc
	podman run --rm --security-opt label=disable \
		-v "$ROOT:/work" -w /work/beagle-linux/linux \
		docker.io/library/debian:13 \
		bash -lc 'apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu make device-tree-compiler && make clean && make'
}

build_zephyr() {
	fetch_erpc
	podman run --rm --security-opt label=disable \
		-v "$ROOT:/work" -w /work \
		ghcr.io/zephyrproject-rtos/ci:latest \
		bash -lc 'export ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-1.0.1; git config --global --add safe.directory "*"; if [[ ! -d .west ]]; then west init -l manifest; fi && west update && west build -b pocketbeagle_2/am6254/m4 zephyr-m4/m4 -p always'
}

case ${1:-all} in
	pru) build_pru ;;
	kmod) build_kmod ;;
	linux) build_linux ;;
	zephyr) build_zephyr ;;
	all)
		build_linux
		build_pru
		build_zephyr
		;;
	*)
		echo "usage: $0 [all|linux|pru|kmod|zephyr]" >&2
		exit 2
		;;
esac
