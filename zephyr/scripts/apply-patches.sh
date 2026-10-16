#!/bin/bash
# Re-apply local Zephyr patches after west update. west update checks out
# the manifest revision and drops edits under zephyr/upstream/.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
shopt -s nullglob
patches=("$root"/zephyr/patches/*.patch)

if ((${#patches[@]} == 0)); then
	exit 0
fi
if [[ ! -e $root/zephyr/upstream/.git ]]; then
	echo "zephyr/upstream is not a west checkout; run west update first" >&2
	exit 1
fi

for p in "${patches[@]}"; do
	if git -C "$root/zephyr/upstream" apply --check "$p"; then
		git -C "$root/zephyr/upstream" apply "$p"
	elif git -C "$root/zephyr/upstream" apply --reverse --check "$p"; then
		echo "already applied: $(basename "$p")"
	else
		echo "patch does not apply: $p" >&2
		exit 1
	fi
done
