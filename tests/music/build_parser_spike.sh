#!/bin/sh
# Isolated tools only: no production target links TagLib.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
target=${1:-host}
cmake=${CMAKE_BIN:-cmake}
sdk=${ARM_TOOLCHAIN_DIR:-/mnt/c/Projects/SBHttpsProxy/build/toolchains/armv5-eabi--musl--stable-2020.02-2}
deps="$root/.local/wp0/deps"
mkdir -p "$deps"
archive="$deps/taglib-1.13.1.tar.gz"
if [ ! -f "$archive" ]; then
  curl --fail -L --max-time 90 https://codeload.github.com/taglib/taglib/tar.gz/refs/tags/v1.13.1 -o "$archive"
fi
printf '%s  %s\n' c8da2b10f1bfec2cd7dbfcd33f4a2338db0765d851a50583d410bacf055cfd0b "$archive" | sha256sum -c -
if [ ! -d "$deps/taglib-1.13.1" ]; then tar -xf "$archive" -C "$deps"; fi
build="$root/.local/wp0/taglib-$target"
prefix="$root/.local/wp0/taglib-$target-prefix"
case "$target" in
  host) cc=gcc; cxx=g++; strip=strip; flags=-Os; cross= ;;
  baby|fab4)
    cc="$sdk/bin/arm-buildroot-linux-musleabi-gcc"
    cxx="$sdk/bin/arm-buildroot-linux-musleabi-g++"
    strip="$sdk/bin/arm-buildroot-linux-musleabi-strip"
    flags='-Os -marm -march=armv5te -mtune=arm926ej-s -mfloat-abi=soft'
    if [ "$target" = fab4 ]; then flags='-Os -marm -march=armv6 -mtune=arm1136jf-s -mfloat-abi=soft'; fi
    cross=-DCMAKE_SYSTEM_NAME=Linux ;;
  *) echo 'usage: build_parser_spike.sh host|baby|fab4' >&2; exit 2 ;;
esac
configure() {
"$cmake" -S "$deps/taglib-1.13.1" -B "$build" $cross \
  -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
  -DWITH_ZLIB=OFF -DBUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX="$prefix"
}
# CMake may reset options once when reusing a cache with another compiler path.
configure
configure
"$cmake" --build "$build" -j4
"$cmake" --install "$build" --prefix "$prefix"
"$cxx" $flags -std=c++11 -static -I"$prefix/include" tests/music/parser_taglib.cpp \
  "$prefix/lib/libtag.a" -o ".local/wp0/parser-taglib-$target"
"$cc" $flags -static tests/music/parser_hls.c native/sbproxy/hls_id3.c -o ".local/wp0/parser-hls-$target"
"$strip" ".local/wp0/parser-taglib-$target" ".local/wp0/parser-hls-$target"
wc -c ".local/wp0/parser-taglib-$target" ".local/wp0/parser-hls-$target"
