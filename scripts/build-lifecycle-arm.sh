#!/bin/sh
# Build-only; this does not install or enable lifecycle arbitration.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk=${ARM_TOOLCHAIN_DIR:-$root/../../SBHttpsProxy/build/toolchains/armv5-eabi--musl--stable-2020.02-2}
cc="$sdk/bin/arm-buildroot-linux-musleabi-gcc"
case "${1:-fab4}" in
 all) sh "$0" baby; sh "$0" fab4; exit ;;
 baby) platform=baby; architecture='-march=armv5te -mtune=arm926ej-s' ;;
 fab4) platform=fab4; architecture='-march=armv6 -mtune=arm1136jf-s' ;;
 *) echo 'usage: build-lifecycle-arm.sh baby|fab4|all' >&2; exit 2 ;;
esac
out=${SB_LIFECYCLE_OUTPUT:-$root/build-arm/$platform/lifecycle}
mkdir -p "$out"
"$cc" -std=c99 -Os -marm $architecture -mfloat-abi=soft -D_FILE_OFFSET_BITS=64 \
 -Wall -Wextra -Wpedantic -Werror -static -pthread \
 "$root/native/sblifecycle/tinysc.c" "$root/native/sblifecycle/control.c" \
 -o "$out/sb-tinysc-control"
"$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/sb-tinysc-control"
"$sdk/bin/arm-buildroot-linux-musleabi-readelf" -h -l -d "$out/sb-tinysc-control" > "$out/elf.txt"
if grep -Eq 'INTERP|NEEDED' "$out/elf.txt"; then exit 1; fi
sha256sum "$out/sb-tinysc-control" > "$out/sha256.txt"
