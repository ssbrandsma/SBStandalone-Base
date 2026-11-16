#!/bin/sh
# Build the optional daemon from this Base checkout with the existing ARM SDK.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
platform=${1:-baby}
case "$platform" in
 all) sh "$0" baby; sh "$0" fab4; exit ;;
 baby) architecture='-march=armv5te -mtune=arm926ej-s' ;;
 fab4) architecture='-march=armv6 -mtune=arm1136jf-s' ;;
 *) echo 'usage: build-upnp-arm.sh baby|fab4|all' >&2; exit 2 ;;
esac
sdk=${ARM_TOOLCHAIN_DIR:-/mnt/c/Projects/SBHttpsProxy/build/toolchains/armv5-eabi--musl--stable-2020.02-2}
cmake=${CMAKE:-/mnt/c/Projects/SBHttpsProxy/build/toolchains/cmake-3.31.8-linux-x86_64/bin/cmake}
if [ ! -x "$cmake" ]; then cmake=cmake; fi
"$cmake" -S "$root/native/sbupnp" -B "$root/build-upnp/$platform" \
 -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
 -DCMAKE_C_COMPILER="$sdk/bin/arm-buildroot-linux-musleabi-gcc" \
 "-DCMAKE_C_FLAGS=-Os -marm -mfloat-abi=soft $architecture -ffunction-sections -fdata-sections" \
 -DCMAKE_EXE_LINKER_FLAGS=-static -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_FLAGS_RELEASE=-Os -DNDEBUG' -DBUILD_TESTING=ON
"$cmake" --build "$root/build-upnp/$platform" -j4
for binary in sbupnp test_sbupnp_core; do
 readelf -h "$root/build-upnp/$platform/$binary" | grep -q 'Machine:.*ARM'
 if readelf -l "$root/build-upnp/$platform/$binary" | grep -q INTERP; then exit 1; fi
 if readelf -d "$root/build-upnp/$platform/$binary" | grep -q NEEDED; then exit 1; fi
done
