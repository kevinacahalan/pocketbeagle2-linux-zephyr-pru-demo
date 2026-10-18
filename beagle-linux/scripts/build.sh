#!/bin/bash
# SPDX-License-Identifier: MIT
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
BBDT=$ROOT/third_party/BeagleBoard-DeviceTrees
BBDT_REV=550f7711a4696d5f5e92505289379b754e871313

IMAGE_LINUX=pb2-linux-userspace
IMAGE_PRU=pb2-pru-cgt
IMAGE_ZEPHYR=pb2-zephyr-sdk

# PB2_CONTAINER_ENGINE is podman or docker. Unset, use podman when it
# is installed, otherwise docker. --security-opt label=disable is podman.
select_engine() {
	local engine=${PB2_CONTAINER_ENGINE:-}
	if [[ -z $engine ]]; then
		if command -v podman >/dev/null 2>&1; then
			engine=podman
		elif command -v docker >/dev/null 2>&1; then
			engine=docker
		else
			echo "Install podman or docker." >&2
			exit 1
		fi
	fi
	case $engine in
		podman|docker) ;;
		*)
			echo "PB2_CONTAINER_ENGINE must be podman or docker." >&2
			exit 1
			;;
	esac
	if ! command -v "$engine" >/dev/null 2>&1; then
		echo "PB2_CONTAINER_ENGINE is $engine, and that program is not installed." >&2
		exit 1
	fi
	PB2_CONTAINER_ENGINE=$engine
}

engine_build() {
	if [[ $PB2_CONTAINER_ENGINE == podman ]]; then
		podman build "$@"
	else
		docker build "$@"
	fi
}

# Run as the host user, so everything written into the repo stays owned by
# you. Without this, docker leaves root-owned files, and rootless podman
# leaves files owned by a subordinate uid that you cannot delete.
engine_run() {
	if [[ $PB2_CONTAINER_ENGINE == podman ]]; then
		podman run --rm --security-opt label=disable \
			--userns=keep-id --user "$(id -u):$(id -g)" -e HOME=/tmp "$@"
	else
		docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp "$@"
	fi
}

build_image() {
	local tag=$1 file=$2
	select_engine
	engine_build -t "$tag" -f "$file" "$(dirname "$file")"
}

# at_rev DIR REV: DIR is a git checkout whose HEAD is REV (a commit or tag).
at_rev() {
	local head want
	head=$(git -C "$1" rev-parse HEAD 2>/dev/null) || return 1
	want=$(git -C "$1" rev-parse "$2^{commit}" 2>/dev/null) || return 1
	[[ $head == "$want" ]]
}

fetch_erpc() {
	if [[ -f $ERPC/zephyr/module.yml ]] && at_rev "$ERPC" "$ERPC_REV"; then
		return
	fi
	rm -rf "$ERPC"
	git clone --depth 1 --branch "$ERPC_REV" https://github.com/EmbeddedRPC/erpc.git "$ERPC"
}

# BeagleBoard-DeviceTrees v6.18.x provides ti/k3-pinctrl.h. The overlay
# Makefile includes that header. This repository does not vendor a copy.
fetch_bbdt() {
	if [[ -f $BBDT/src/arm64/ti/k3-pinctrl.h ]] && at_rev "$BBDT" "$BBDT_REV"; then
		return
	fi
	rm -rf "$BBDT"
	git clone --depth 1 https://github.com/beagleboard/BeagleBoard-DeviceTrees.git "$BBDT"
	git -C "$BBDT" fetch --depth 1 origin "$BBDT_REV"
	git -C "$BBDT" checkout "$BBDT_REV"
}

fetch_pssp() {
	if [[ -f $PSSP/lib/rpmsg_lib.lib ]] && at_rev "$PSSP" "$PSSP_REV"; then
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
	fetch_bbdt
	engine_run \
		-v "$ROOT:/work" -w /work \
		"$IMAGE_LINUX" \
		bash -lc 'make -C linux-app clean all && make -C beagle-linux/overlays clean all'
}

build_pru() {
	build_image "$IMAGE_PRU" "$ROOT/containers/pru-cgt/Containerfile"
	fetch_pssp
	engine_run \
		-v "$ROOT:/work" -w /work \
		-e PRU_CGT=/opt/ti-cgt-pru \
		"$IMAGE_PRU" \
		make -C pru PRU_CGT=/opt/ti-cgt-pru PSSP=/work/third_party/pru-software-support-package
}

# The PRU remoteproc patch has to match the board kernel. Headers are copied
# from the board once; the module itself is cross-compiled in the Linux image.
build_kmod() {
	local host=${PB2_SSH:-}
	local krel headers
	if [[ -z $host ]]; then
		echo "Set PB2_SSH to the board's ssh login, for example PB2_SSH=debian@192.168.7.2." >&2
		echo "The board must have linux-headers for its running kernel installed." >&2
		exit 1
	fi
	krel=${PB2_KREL:-$(ssh "$host" uname -r)}
	headers=$CACHE/linux-headers-$krel
	echo "building pru_rproc for $krel"
	build_image "$IMAGE_LINUX" "$ROOT/containers/linux/Containerfile"
	if [[ ! -f $headers/Makefile ]]; then
		echo "fetching kernel headers from $host"
		mkdir -p "$headers"
		rsync -a "$host:/usr/src/linux-headers-$krel/" "$headers/"
	fi
	engine_run \
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
			# Clean first. The headers sit at /tmp/kheaders for every kernel
			# version, so kbuild would otherwise keep a module built for the
			# previous kernel.
			make -C /tmp/kheaders M=/work/beagle-linux/kernel/pru_rproc ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- clean
			make -C /tmp/kheaders M=/work/beagle-linux/kernel/pru_rproc ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
			'
}

build_zephyr() {
	build_image "$IMAGE_ZEPHYR" "$ROOT/containers/zephyr/Containerfile"
	fetch_erpc
	engine_run \
		-v "$ROOT:/work" -w /work \
		"$IMAGE_ZEPHYR" \
		bash -c 'set -euo pipefail
			git config --global --add safe.directory "*"
			if [[ ! -d .west ]]; then
				west init -l zephyr
			fi
			# west update checks out the manifest revision and drops local edits.
			# apply-patches.sh reapplies zephyr/patches/ when that directory
			# has any.
			west update
			zephyr/scripts/apply-patches.sh
			west build -b pocketbeagle_2/am6254/m4 zephyr/m4 -p always'
}

# Gather what install-beagle.sh needs into out/pb2-beagle, to copy to the board.
bundle() {
	local out=$ROOT/out/pb2-beagle f
	local files=(
		linux-app/pb2
		beagle-linux/scripts/pb2-demo
		beagle-linux/scripts/install-beagle.sh
		pru/build/am62x-pru0-fw
		pru/build/am62x-pru1-fw
		build/zephyr/pb2_m4.elf
		beagle-linux/kernel/pru_rproc/pru_rproc.ko
		beagle-linux/overlays/k3-am62-pocketbeagle2-spi0-hello.dtbo
		beagle-linux/overlays/k3-am62-pocketbeagle2-pru1-xy.dtbo
		beagle-linux/overlays/k3-am62-pocketbeagle2-leftover-gpio.dtbo
	)
	for f in "${files[@]}"; do
		if [[ ! -f $ROOT/$f ]]; then
			echo "missing $f. Run ./beagle-linux/scripts/build.sh first, and kmod for the .ko." >&2
			exit 1
		fi
	done
	rm -rf "$out"
	mkdir -p "$out"
	for f in "${files[@]}"; do
		cp -p "$ROOT/$f" "$out/"
	done
	echo "bundle in $out"
	echo "  scp -r out/pb2-beagle USER@BOARD:"
	echo "  ssh -t USER@BOARD 'sudo ./pb2-beagle/install-beagle.sh && sudo reboot'"
}

case ${1:-all} in
	bundle) bundle ;;
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
		echo "usage: $0 [all|linux|pru|zephyr|kmod|bundle]" >&2
		exit 2
		;;
esac
