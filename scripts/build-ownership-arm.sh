#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk=${ARM_TOOLCHAIN_DIR:-/mnt/c/Projects/SBHttpsProxy/build/toolchains/armv5-eabi--musl--stable-2020.02-2}
cc="$sdk/bin/arm-buildroot-linux-musleabi-gcc"
platform=${1:-baby}
case "$platform" in
 all) sh "$0" baby; sh "$0" fab4; exit ;;
 baby) flags='-Os -marm -march=armv5te -mtune=arm926ej-s -mfloat-abi=soft' ;;
 fab4) flags='-Os -marm -march=armv6 -mtune=arm1136jf-s -mfloat-abi=soft -DSB_TARGET_FAB4=1' ;;
 *) echo 'usage: build-ownership-arm.sh baby|fab4|all' >&2; exit 2 ;;
esac
out="$root/build-arm/$platform/ownership"
mkdir -p "$out"
flags="$flags -D_FILE_OFFSET_BITS=64 -std=c99 -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections -UNDEBUG"
common="$root/native/sbbase/src/ownership_bridge.c $root/native/common/music_resource_ipc.c $root/native/sbbase/src/slim_controller.c $root/native/sbbase/src/protocol.c $root/native/common/music_contracts.c"
"$cc" $flags -static -Wl,--gc-sections -I"$root/native/sbbase/include" "$root/tests/music/ownership_driver.c" $common -o "$out/test_ownership_driver"
"$cc" $flags -static -Wl,--gc-sections -I"$root/native/sbbase/include" "$root/native/sbbase/src/main.c" "$root/native/sblifecycle/tinysc.c" -pthread \
 "$root/native/sbbase/src/nowplaying.c" "$root/native/sbbase/src/alarms.c" "$root/native/sbbase/src/sleep.c" \
 "$root/native/sbbase/src/lms_command.c" "$root/native/sbbase/src/output_queue.c" $common -o "$out/sbbase"
for binary in test_ownership_driver sbbase; do
 "$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/$binary"
 file "$out/$binary"
 if readelf -l "$out/$binary" | grep -q INTERP; then exit 1; fi
 if readelf -d "$out/$binary" | grep -q NEEDED; then exit 1; fi
done
{ printf '%s\n' "flags=$flags"; "$cc" --version | head -1;
 sha256sum "$out/test_ownership_driver" "$out/sbbase";
} > "$out/manifest.txt"
