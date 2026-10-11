#!/bin/bash
# Re-apply local Zephyr patches after west update. west update checks out
# the manifest revision and drops edits under zephyr/.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
shopt -s nullglob
patches=("$root"/zephyr-m4/patches/*.patch)

if ((${#patches[@]} == 0)); then
	exit 0
fi
if [[ ! -e $root/zephyr/.git ]]; then
	echo "zephyr/ is not a west checkout; run west update first" >&2
	exit 1
fi

for p in "${patches[@]}"; do
	if git -C "$root/zephyr" apply --check "$p"; then
		git -C "$root/zephyr" apply "$p"
	elif git -C "$root/zephyr" apply --reverse --check "$p"; then
		echo "already applied: $(basename "$p")"
	else
		echo "patch does not apply: $p" >&2
		exit 1
	fi
done
