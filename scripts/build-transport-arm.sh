#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk=${ARM_TOOLCHAIN_DIR:-/mnt/c/Projects/SBHttpsProxy/build/toolchains/armv5-eabi--musl--stable-2020.02-2}
cc="$sdk/bin/arm-buildroot-linux-musleabi-gcc"
ar="$sdk/bin/arm-buildroot-linux-musleabi-ar"
platform=${1:-baby}
case "$platform" in
  all) sh "$0" baby; sh "$0" fab4; exit ;;
  baby) flags='-Os -marm -march=armv5te -mtune=arm926ej-s -mfloat-abi=soft' ;;
  fab4) flags='-Os -marm -march=armv6 -mtune=arm1136jf-s -mfloat-abi=soft -DSB_TARGET_FAB4=1' ;;
  *) echo 'usage: build-transport-arm.sh baby|fab4|all' >&2; exit 2 ;;
esac
flags="$flags -ffunction-sections -fdata-sections -std=c99 -Wall -Wextra -Wpedantic"
out="$root/build-arm/$platform/transport"
mkdir -p "$out"
for module in slim_controller output_queue protocol; do
  "$cc" $flags -I"$root/native/sbbase/include" -c "$root/native/sbbase/src/$module.c" -o "$out/$module.o"
done
"$cc" $flags -c "$root/native/common/music_contracts.c" -o "$out/music_contracts.o"
"$ar" rcs "$out/libsbtransport.a" "$out/slim_controller.o" "$out/output_queue.o" "$out/protocol.o" "$out/music_contracts.o"
"$cc" $flags -static -UNDEBUG -Wl,--gc-sections -I"$root/native/sbbase/include" \
  "$root/tests/music/test_transport.c" "$out/libsbtransport.a" -o "$out/test_music_transport"
"$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/test_music_transport"
file "$out/test_music_transport"
"$sdk/bin/arm-buildroot-linux-musleabi-size" "$out/test_music_transport" "$out/libsbtransport.a"
{ printf '%s\n' "flags=$flags"; "$cc" --version | head -1; sha256sum "$out/test_music_transport" "$out/libsbtransport.a"; } > "$out/manifest.txt"
