#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

if [ ! -f "$root/cpusupport-config.h" ] || [ ! -f "$root/apisupport-config.h" ]; then
	echo "Configure/build the project before running this regression." >&2
	exit 2
fi
if [ ! -f "$root/liball/liball.a" ] ||
    [ ! -f "$root/liball/optional_mutex_pthread/liball_optional_mutex_pthread.a" ]; then
	echo "Build the project libraries before running this regression." >&2
	exit 2
fi

CFLAGS_POSIX=
LDADD_POSIX=
if [ -f "$root/posix-flags.sh" ]; then
	. "$root/posix-flags.sh"
fi

${CC:-cc} ${CFLAGS_POSIX:-} -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
    -DCPUSUPPORT_CONFIG_FILE=\"cpusupport-config.h\" \
    -DAPISUPPORT_CONFIG_FILE=\"apisupport-config.h\" \
    -std=c99 -O2 -ffunction-sections -fdata-sections \
    -I"$root" -I"$root/lbs" \
    -I"$root/libcperciva/alg" -I"$root/libcperciva/datastruct" \
    -I"$root/libcperciva/events" -I"$root/libcperciva/netbuf" \
    -I"$root/libcperciva/network" -I"$root/libcperciva/util" \
    -I"$root/lib/proto_lbs" -I"$root/lib/wire" \
    "$root/tests/worker-assign-failure.c" \
    "$root/liball/liball.a" \
    "$root/liball/optional_mutex_pthread/liball_optional_mutex_pthread.a" \
    -Wl,--gc-sections ${LDADD_POSIX:-} -lpthread \
    -o "$tmp/worker-assign-failure"

"$tmp/worker-assign-failure"
