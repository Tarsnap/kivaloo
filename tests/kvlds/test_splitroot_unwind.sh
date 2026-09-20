#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc=${CC:-cc}

"$cc" -std=c99 -O2 -Wall -Wextra -Werror \
    -I"$root/libcperciva/events" \
    -I"$root/libcperciva/util" \
    -I"$root/lib/datastruct" \
    -I"$root/kvlds" \
    "$root/tests/kvlds/splitroot_unwind.c" \
    -o "$tmp/splitroot_unwind"

"$tmp/splitroot_unwind"
