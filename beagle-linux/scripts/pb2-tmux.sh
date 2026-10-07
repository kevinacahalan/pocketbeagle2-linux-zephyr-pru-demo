#!/bin/bash
# Start the four-window PocketBeagle 2 hello world.
# Run this on the board.
# Linux calls Zephyr over eRPC on RPMsg, and commands PRU0 over RPMsg.
# Zephyr asks PRU0 through the small DDR mailbox. PRU0 arms PRU1.
# PRU1 has no RPMsg endpoint. Its window only prints the shared-RAM status.
#
# Windows: linux, zephyr, pru0, pru1
# The device-manager R5 (78000000.r5f) is left alone.

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$HERE/pb2-ipc
FW=$HERE/fw
RUN=/tmp/pb2-demo
SESSION=pb2

if ! command -v tmux >/dev/null; then
	echo "tmux is not installed" >&2
	exit 1
fi
if [[ ! -x $BIN ]]; then
	echo "missing $BIN" >&2
	exit 1
fi

# Stock pru_rproc on this kernel cannot boot RPMsg firmware.
# The replacement module is cross-compiled on the laptop (./beagle-linux/scripts/build.sh kmod).
ensure_pru_kick() {
	if grep -q 'pru_rproc_kick' /proc/kallsyms; then
		return 0
	fi
	local ko="" d name state
	if [[ -f $HERE/pru_rproc.ko ]]; then
		ko=$HERE/pru_rproc.ko
	elif [[ -f $HERE/../kernel/pru_rproc/pru_rproc.ko ]]; then
		ko=$HERE/../kernel/pru_rproc/pru_rproc.ko
	else
		echo "missing pru_rproc.ko; build it with ./beagle-linux/scripts/build.sh kmod" >&2
		exit 1
	fi
	for d in /sys/class/remoteproc/remoteproc*; do
		name=$(cat "$d/name")
		if [[ $name == 30074000.pru || $name == 30078000.pru ]]; then
			state=$(cat "$d/state")
			if [[ $state != offline ]]; then
				echo stop | sudo tee "$d/state" >/dev/null || true
			fi
		fi
	done
	sudo rmmod pru_rproc
	sudo insmod "$ko"
}

ensure_pru_kick

rproc_by_name() {
	local want=$1 d
	for d in /sys/class/remoteproc/remoteproc*; do
		if [[ $(cat "$d/name") == "$want" ]]; then
			echo "$d"
			return 0
		fi
	done
	echo "remoteproc $want not found" >&2
	return 1
}

load_fw() {
	local rproc=$1 fwname=$2 src=$3 state
	state=$(cat "$rproc/state")
	if [[ $state != offline ]]; then
		echo stop | sudo tee "$rproc/state" >/dev/null
		sleep 0.4
	fi
	sudo cp "$src" "/lib/firmware/$fwname"
	echo "$fwname" | sudo tee "$rproc/firmware" >/dev/null
	echo start | sudo tee "$rproc/state" >/dev/null
}

find_rpmsg() {
	local substr=$1 dst_want=$2 d dest dev
	for d in /sys/class/rpmsg/rpmsg*; do
		[[ -e $d/dst ]] || continue
		dest=$(cat "$d/dst")
		dev=$(readlink -f "$d/device")
		if [[ $dev == *"$substr"* && $dest == "$dst_want" ]]; then
			echo "/dev/$(basename "$d")"
			return 0
		fi
	done
	return 1
}

M4=$(rproc_by_name 5000000.m4fss)
PRU0=$(rproc_by_name 30074000.pru)
PRU1=$(rproc_by_name 30078000.pru)

load_fw "$PRU0" am62x-pru0-fw "$FW/am62x-pru0-fw"
load_fw "$PRU1" am62x-pru1-fw "$FW/am62x-pru1-fw"
load_fw "$M4" am62-mcu-m4f0_0-fw "$FW/am62-mcu-m4f0_0-fw"

ZEPHYR_DEV=""
PRU0_DEV=""
for _ in $(seq 1 30); do
	ZEPHYR_DEV=$(find_rpmsg 5000000.m4fss 1024 || true)
	PRU0_DEV=$(find_rpmsg 30074000.pru 30 || true)
	if [[ -n $ZEPHYR_DEV && -n $PRU0_DEV ]]; then
		break
	fi
	sleep 0.2
done

if [[ -z $ZEPHYR_DEV || -z $PRU0_DEV ]]; then
	echo "RPMsg devices did not show up" >&2
	echo "zephyr=${ZEPHYR_DEV:-missing} pru0=${PRU0_DEV:-missing}" >&2
	echo "--- rpmsg class ---" >&2
	ls -l /sys/class/rpmsg >&2 || true
	echo "--- remoteproc state ---" >&2
	for d in /sys/class/remoteproc/remoteproc*; do
		echo "$(basename "$d") $(cat "$d/name") $(cat "$d/state")" >&2
	done
	exit 1
fi

sudo chmod a+rw "$ZEPHYR_DEV" "$PRU0_DEV"
SETCAP=$(command -v setcap || true)
if [[ -z $SETCAP && -x /usr/sbin/setcap ]]; then
	SETCAP=/usr/sbin/setcap
fi
if [[ -n $SETCAP ]]; then
	sudo "$SETCAP" cap_sys_rawio+ep "$BIN"
else
	echo "setcap is not installed; /dev/mem access may fail" >&2
fi
# Group kmem can open the node. The driver still requires CAP_SYS_RAWIO,
# which setcap grants. This is not a udev rule.
sudo chmod 0660 /dev/mem

mkdir -p "$RUN"
rm -f "$RUN/zephyr.in" "$RUN/pru0.in" "$RUN/pru1.in"
mkfifo "$RUN/zephyr.in" "$RUN/pru0.in"

tmux kill-session -t "$SESSION" 2>/dev/null || true
tmux new-session -d -s "$SESSION" -n linux
tmux new-window -d -t "$SESSION" -n zephyr
tmux new-window -d -t "$SESSION" -n pru0
tmux new-window -d -t "$SESSION" -n pru1

tmux send-keys -t "$SESSION:zephyr" "$BIN erpc $ZEPHYR_DEV $RUN/zephyr.in" C-m
tmux send-keys -t "$SESSION:pru0" "$BIN peer pru0 $PRU0_DEV $RUN/pru0.in" C-m
tmux send-keys -t "$SESSION:pru1" "$BIN watch" C-m
tmux send-keys -t "$SESSION:linux" "$BIN linux $RUN" C-m
tmux select-window -t "$SESSION:linux"

echo "tmux session '$SESSION' is up"
echo "  zephyr $ZEPHYR_DEV"
echo "  pru0   $PRU0_DEV"
echo "  pru1   shared ram"

if [[ -t 0 && -t 1 ]]; then
	exec tmux attach -t "$SESSION"
fi
