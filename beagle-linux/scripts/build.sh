#!/bin/bash
# Build the demo in the container images under containers/.
#   containers/linux/Containerfile    Linux userspace and the PRU kernel module
#   containers/pru-cgt/Containerfile  TI CGT 2.3.3, then the PRU firmware
#   containers/zephyr/Containerfile   Zephyr SDK for the M4
# The Poky image stays at yocto/kas/pb2-poky.yml. This script does not flash.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CACHE=$ROOT/.cache
PSSP=$ROOT/third_party/pru-software-support-package
PSSP_REV=28107daa1d184059f67676ba49d0fcaebb0f6a96
ERPC=$ROOT/third_party/erpc
ERPC_REV=1.14.0

IMAGE_LINUX=pb2-linux-userspace
IMAGE_PRU=pb2-pru-cgt
IMAGE_ZEPHYR=pb2-zephyr-sdk

build_image() {
	local tag=$1 file=$2
	podman build -t "$tag" -f "$file" "$(dirname "$file")"
}

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

build_linux() {
	build_image "$IMAGE_LINUX" "$ROOT/containers/linux/Containerfile"
	fetch_erpc
	podman run --rm --security-opt label=disable \
		-v "$ROOT:/work" -w /work/beagle-linux/linux \
		"$IMAGE_LINUX" \
		make clean all
}

build_pru() {
	build_image "$IMAGE_PRU" "$ROOT/containers/pru-cgt/Containerfile"
	fetch_pssp
	podman run --rm --security-opt label=disable \
		-v "$ROOT:/work" -w /work \
		-e PRU_CGT=/opt/ti-cgt-pru \
		"$IMAGE_PRU" \
		make -C pru PRU_CGT=/opt/ti-cgt-pru PSSP=/work/third_party/pru-software-support-package
}

# The PRU remoteproc patch has to match the board kernel. Headers are copied
# from the board once; the module itself is cross-compiled in the Linux image.
build_kmod() {
	local krel=${PB2_KREL:-6.18.52-arm64-k3-r56}
	local headers=$CACHE/linux-headers-$krel
	local host=${PB2_SSH:-}
	if [[ -z $host ]]; then
		echo "Set PB2_SSH to user@board (the board must have the matching kernel headers)." >&2
		echo "Set PB2_KREL if uname -r on the board is not $krel." >&2
		exit 1
	fi
	build_image "$IMAGE_LINUX" "$ROOT/containers/linux/Containerfile"
	if [[ ! -f $headers/Makefile ]]; then
		echo "fetching kernel headers from $host"
		mkdir -p "$headers"
		rsync -a "$host:/usr/src/linux-headers-$krel/" "$headers/"
	fi
	podman run --rm --security-opt label=disable \
		-v "$ROOT:/work" \
		-v "$headers:/headers:ro" \
		-w /work \
		"$IMAGE_LINUX" \
		bash -lc 'set -euo pipefail
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

build_zephyr() {
	build_image "$IMAGE_ZEPHYR" "$ROOT/containers/zephyr/Containerfile"
	fetch_erpc
	podman run --rm --security-opt label=disable \
		-v "$ROOT:/work" -w /work \
		"$IMAGE_ZEPHYR" \
		bash -lc 'set -euo pipefail
			export ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-1.0.1
			git config --global --add safe.directory "*"
			if [[ ! -d .west ]]; then
				west init -l manifest
			fi
			west update
			west build -b pocketbeagle_2/am6254/m4 zephyr-m4/m4 -p always'
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
