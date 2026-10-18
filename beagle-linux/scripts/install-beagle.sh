#!/bin/bash
# SPDX-License-Identifier: MIT
# One-time setup of the demo on the BeagleBoard Debian image. Run it on the board.
#
#   sudo ./pb2-beagle/install-beagle.sh                    from a bundle
#   sudo ./beagle-linux/scripts/install-beagle.sh [dir]    from a repo checkout
#
# Installs the demo and makes the parts that were lost at every reboot stick:
#   - pb2 and pb2-demo on PATH, the PRU and M4 firmware in /lib/firmware
#   - the patched pru_rproc, so the kernel loads it instead of the stock module
#   - the three overlays on the default extlinux label
#   - a udev rule that makes /dev/rpmsg* usable by the dialout group
#   - pb2-pru.service, which starts PRU0 and PRU1 at boot
#   - tmux, for pb2-demo --tmux, from apt when it is missing
# The M4 image is installed as /lib/firmware/pb2_m4.elf, not under the kernel's
# default name am62-mcu-m4f0_0-fw, so the M4 stays off until pb2-demo starts it.
#
# Artifacts come from dir when it is given (flat, by file name), else from
# the directory this script is in when it is a bundle, else from the build
# outputs in this repo. Reboot when it finishes. Pads, the module, and the
# rpmsg permissions only change at boot.

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC=${1:-}
# Run from a bundle (./beagle-linux/scripts/build.sh bundle), the artifacts
# sit next to this script.
if [[ -z $SRC && -f $HERE/pb2 ]]; then
	SRC=$HERE
fi
EXTLINUX=/boot/firmware/extlinux/extlinux.conf
OVERLAY_DIR=/boot/firmware/overlays
BASE_DTB=/boot/firmware/ti/k3-am62-pocketbeagle2.dtb
OVERLAYS=(
	k3-am62-pocketbeagle2-spi0-hello.dtbo
	k3-am62-pocketbeagle2-pru1-xy.dtbo
	k3-am62-pocketbeagle2-leftover-gpio.dtbo
)

if [[ $(id -u) -ne 0 ]]; then
	exec sudo "$0" "$@"
fi

# find NAME REPO-PATH: the file in artifact-dir, else the repo build output.
find_artifact() {
	local name=$1 repo=$2
	if [[ -n $SRC && -f $SRC/$name ]]; then
		echo "$SRC/$name"
	elif [[ -f $ROOT/$repo ]]; then
		echo "$ROOT/$repo"
	else
		echo "missing $name (looked in ${SRC:-<no artifact-dir>} and $ROOT/$repo)" >&2
		return 1
	fi
}

KREL=$(uname -r)
PB2=$(find_artifact pb2 linux-app/pb2)
DEMO=$(find_artifact pb2-demo beagle-linux/scripts/pb2-demo)
PRU0=$(find_artifact am62x-pru0-fw pru/build/am62x-pru0-fw)
PRU1=$(find_artifact am62x-pru1-fw pru/build/am62x-pru1-fw)
M4=$(find_artifact pb2_m4.elf build/zephyr/pb2_m4.elf)
KO=$(find_artifact pru_rproc.ko beagle-linux/kernel/pru_rproc/pru_rproc.ko)
declare -A DTBO
for o in "${OVERLAYS[@]}"; do
	DTBO[$o]=$(find_artifact "$o" "beagle-linux/overlays/$o")
done

# The module has to be built for this kernel.
if ! grep -aq "vermagic=$KREL " "$KO"; then
	echo "$KO was not built for $KREL." >&2
	echo "Rebuild it: PB2_KREL=$KREL ./beagle-linux/scripts/build.sh kmod" >&2
	exit 1
fi

# The overlays have to apply to the board's own DTB.
if command -v fdtoverlay >/dev/null 2>&1 && [[ -f $BASE_DTB ]]; then
	args=()
	for o in "${OVERLAYS[@]}"; do
		args+=("${DTBO[$o]}")
	done
	trial=$(mktemp)
	if ! fdtoverlay -i "$BASE_DTB" -o "$trial" "${args[@]}"; then
		rm -f "$trial"
		echo "The overlays do not apply to $BASE_DTB." >&2
		exit 1
	fi
	rm -f "$trial"
fi

echo "userspace and firmware"
install -m 0755 "$PB2" /usr/bin/pb2
install -m 0755 "$DEMO" /usr/bin/pb2-demo
install -m 0644 "$PRU0" /lib/firmware/am62x-pru0-fw
install -m 0644 "$PRU1" /lib/firmware/am62x-pru1-fw
install -m 0644 "$M4" /lib/firmware/pb2_m4.elf
# The kernel boots whatever has the default M4 name. The Debian image ships
# none, so a file there is from an older install of this demo: move it aside.
if [[ -e /lib/firmware/am62-mcu-m4f0_0-fw ]]; then
	mv /lib/firmware/am62-mcu-m4f0_0-fw /lib/firmware/am62-mcu-m4f0_0-fw.pb2-old
	echo "moved /lib/firmware/am62-mcu-m4f0_0-fw to am62-mcu-m4f0_0-fw.pb2-old so the M4 does not start at boot"
fi
if command -v setcap >/dev/null 2>&1 || [[ -x /usr/sbin/setcap ]]; then
	"$(command -v setcap || echo /usr/sbin/setcap)" cap_sys_rawio+ep /usr/bin/pb2
fi

if ! command -v tmux >/dev/null 2>&1; then
	echo "tmux (for pb2-demo --tmux)"
	if ! { apt-get update -qq && apt-get install -y -qq tmux; } >/dev/null 2>&1; then
		echo "Could not install tmux (does the board have internet?). Plain pb2-demo still works." >&2
	fi
fi

echo "pru_rproc for $KREL"
install -m 0644 "$KO" /usr/lib/pru_rproc.ko
install -D -m 0644 "$KO" "/lib/modules/$KREL/updates/pru_rproc.ko"
depmod "$KREL"

echo "overlays"
for o in "${OVERLAYS[@]}"; do
	install -m 0755 "${DTBO[$o]}" "$OVERLAY_DIR/$o"
done
[[ -f $EXTLINUX.pb2-bak ]] || cp "$EXTLINUX" "$EXTLINUX.pb2-bak"
OVERLAYS="${OVERLAYS[*]}" python3 - "$EXTLINUX" <<'EOF'
import os, sys

path = sys.argv[1]
want = ["/overlays/" + o for o in os.environ["OVERLAYS"].split()]
lines = open(path).read().split("\n")

default = next(l.split(None, 1)[1].strip() for l in lines if l.startswith("default "))
start = next(i for i, l in enumerate(lines) if l.strip() == "label " + default)
end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("label ")), len(lines))

for i in range(start, end):
    if lines[i].strip().startswith("fdtoverlays"):
        have = lines[i].split()[1:]
        lines[i] = "    fdtoverlays " + " ".join(have + [w for w in want if w not in have])
        break
else:
    at = next(i for i in range(start, end) if lines[i].strip().startswith("fdtdir"))
    lines.insert(at + 1, "    fdtoverlays " + " ".join(want))

open(path, "w").write("\n".join(lines))
EOF

echo "rpmsg permissions and PRU start"
cat > /etc/udev/rules.d/99-pb2-rpmsg.rules <<'EOF'
KERNEL=="rpmsg[0-9]*", GROUP="dialout", MODE="0660"
EOF

cat > /usr/libexec/pb2-pru-start <<'EOF'
#!/bin/bash
# Start PRU0 and PRU1 from /lib/firmware. Leaves a running core alone.
set -eu
checked=0
for name in 30074000.pru:am62x-pru0-fw 30078000.pru:am62x-pru1-fw; do
	want=${name%%:*}
	fw=${name##*:}
	for _ in $(seq 1 50); do
		for d in /sys/class/remoteproc/remoteproc*; do
			[[ -e $d/name && $(cat "$d/name") == "$want" ]] || continue
			# The PRU remoteproc exists, so pru_rproc is loaded by now.
			if [[ $checked -eq 0 ]] && ! grep -q pru_rproc_kick /proc/kallsyms; then
				echo "pru_rproc has no virtio kick. Was the kernel upgraded? Rebuild the" >&2
				echo "module for $(uname -r) and run install-beagle.sh again. PRU0 RPMsg will fail." >&2
			fi
			checked=1
			if [[ $(cat "$d/state") == offline ]]; then
				printf "%s\n" "$fw" > "$d/firmware"
				printf "start\n" > "$d/state"
			fi
			break 2
		done
		sleep 0.2
	done
done
EOF
chmod 0755 /usr/libexec/pb2-pru-start

cat > /etc/systemd/system/pb2-pru.service <<'EOF'
[Unit]
Description=Start the PocketBeagle 2 demo PRU cores
After=systemd-modules-load.service local-fs.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/libexec/pb2-pru-start

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable pb2-pru.service
udevadm control --reload

echo
echo "Done. Reboot to apply the overlays and the module."
if command -v tmux >/dev/null 2>&1; then
	echo "After the reboot: pb2-demo --tmux"
else
	echo "After the reboot: pb2-demo"
fi
echo "Original extlinux.conf: $EXTLINUX.pb2-bak"
echo "The module only fits kernel $KREL. After a kernel upgrade, rebuild it and run this again."
