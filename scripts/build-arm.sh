#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# The cross-toolchain is Linux ELF; Git Bash must enter WSL first.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    command -v wsl.exe >/dev/null 2>&1 || { echo "WSL is required for the Linux ARM toolchain" >&2; exit 1; }
    windows_script=$(cygpath -m "$root/scripts/build-arm.sh")
    linux_script=$(MSYS_NO_PATHCONV=1 wsl.exe --exec wslpath -u "$windows_script" | tr -d '\r')
    test -n "$linux_script" || { echo "Could not resolve project path in WSL" >&2; exit 1; }
    echo "Running in WSL: scripts/build-arm.sh $*"
    MSYS_NO_PATHCONV=1 exec wsl.exe --exec sh "$linux_script" "$@"
    ;;
esac


# Reuse the hardware-validated SDK and static TLS libraries from SBHttpsProxy.
# Override SBHTTPSPROXY_ROOT when that project is not beside SBStandaloneBase.
if [ -z "${SBHTTPSPROXY_ROOT:-}" ]; then
  if [ -d /mnt/c/Projects/SBHttpsProxy ]; then
    SBHTTPSPROXY_ROOT=/mnt/c/Projects/SBHttpsProxy
  else
    SBHTTPSPROXY_ROOT="$root/../../SBHttpsProxy"
  fi
fi
toolchain="${ARM_TOOLCHAIN_DIR:-$SBHTTPSPROXY_ROOT/build/toolchains/armv5-eabi--musl--stable-2020.02-2}"
target=arm-buildroot-linux-musleabi
cc="$toolchain/bin/$target-gcc"
strip="$toolchain/bin/$target-strip"
size="$toolchain/bin/$target-size"
tls_prefix="${ARM_TLS_PREFIX:-$SBHTTPSPROXY_ROOT/build/arm/prefix}"
platform=${1:-baby}
case "$platform" in
  all) sh "$0" baby; sh "$0" fab4; exit ;;
  baby|fab4) ;;
  *) echo "Usage: $0 [baby|fab4|all]" >&2; exit 2 ;;
esac
out="$root/build-arm/$platform"
flags="-Os -marm -march=armv5te -mtune=arm926ej-s -mfloat-abi=soft -ffunction-sections -fdata-sections"

if [ "$platform" = fab4 ]; then
  flags="-Os -marm -march=armv6 -mtune=arm1136jf-s -mfloat-abi=soft -ffunction-sections -fdata-sections -DSB_TARGET_FAB4=1"
fi

test -x "$cc" || { echo "Missing ARM compiler: $cc" >&2; exit 1; }
test -f "$tls_prefix/lib/libcurl.a" || { echo "Missing static libcurl in $tls_prefix" >&2; exit 1; }
test -f "$tls_prefix/lib/libwolfssl.a" || { echo "Missing static wolfSSL in $tls_prefix" >&2; exit 1; }
wolf_prefix="${ARM_WOLFSSL_PREFIX:-$root/build-proxy-tls/prefix}"
if [ ! -f "$wolf_prefix/sbproxy-rsa-capacity" ] ||
   [ "$(tr -d '\r\n' < "$wolf_prefix/sbproxy-rsa-capacity")" != 4096 ] ||
   [ ! -f "$wolf_prefix/sbproxy-static-rsa" ] ||
   [ "$(tr -d '\r\n' < "$wolf_prefix/sbproxy-static-rsa")" != aes-gcm-v1 ]; then
  SBHTTPSPROXY_ROOT="$SBHTTPSPROXY_ROOT" sh "$root/scripts/build-proxy-tls.sh"
fi
test -f "$wolf_prefix/lib/libwolfssl.a"
image_prefix="${ARM_IMAGE_PREFIX:-$root/build-image-arm/prefix}"
test -f "$image_prefix/lib/libz.a" || { echo "Run scripts/build-image-libs.sh first" >&2; exit 1; }
mkdir -p "$out"

"$cc" -static $flags -std=c99 -Wall -Wextra -Wpedantic -Wl,--gc-sections \
  -I"$root/native/sbbase/include" "$root/native/sbbase/src/main.c" "$root/native/sblifecycle/tinysc.c" -pthread "$root/native/sbbase/src/ownership_bridge.c" "$root/native/common/music_resource_ipc.c" \
  "$root/native/sbbase/src/protocol.c" "$root/native/sbbase/src/nowplaying.c" \
  "$root/native/sbbase/src/alarms.c" "$root/native/sbbase/src/sleep.c" \
  "$root/native/sbbase/src/output_queue.c" "$root/native/sbbase/src/slim_controller.c" \
  "$root/native/sbbase/src/lms_command.c" "$root/native/common/music_contracts.c" -o "$out/sbbase"

ARM_TOOLCHAIN_DIR="$toolchain" SB_LIFECYCLE_OUTPUT="$out" \
  sh "$root/scripts/build-lifecycle-arm.sh" "$platform"

"$cc" -static $flags -std=c99 -Wall -Wextra -pthread -Wl,--gc-sections \
  -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -DMG_ENABLE_EPOLL=0 -DMG_ENABLE_POLL=1 \
  -DMG_ENABLE_DIRLIST=0 -DMG_ENABLE_SSI=0 -I"$root/native/sbwebserver/src" -I"$image_prefix/include" -I"$root/native/sbbase/include" \
  -I"$root/native/sbwebserver/third_party" "$root/native/sbwebserver/src/"*.c \
  "$root/native/sbwebserver/third_party/mongoose.c" "$root/native/sbbase/src/alarms.c" "$root/native/sbbase/src/nowplaying.c" "$image_prefix/lib/libz.a" -o "$out/sbwebserver"

"$cc" -static $flags -std=c99 -Wall -Wextra -Wpedantic -pthread -Wl,--gc-sections \
  -D_GNU_SOURCE -I"$wolf_prefix/include" -I"$tls_prefix/include" "$root/native/sbproxy/sbproxy.c" \
  "$root/native/sbproxy/hls_playlist.c" "$root/native/sbproxy/hls_audio.c" "$root/native/sbproxy/hls_relay.c" \
  "$root/native/sbproxy/hls_id3.c" "$root/native/sbproxy/hls_metadata.c" \
  "$tls_prefix/lib/libcurl.a" "$wolf_prefix/lib/libwolfssl.a" -o "$out/sbproxy"

"$cc" -static $flags -std=c99 -Wall -Wextra -Wpedantic -Wl,--gc-sections \
  "$root/native/sbstorage/sb_storage_helper.c" -o "$out/sb-storage-helper"

"$cc" -static $flags -std=c99 -Wall -Wextra -Wpedantic -Wl,--gc-sections \
  "$root/native/sbstorage/sb_storage_updater.c" -o "$out/sb-storage-updater"

image_prefix="${ARM_IMAGE_PREFIX:-$root/build-image-arm/prefix}"
test -f "$image_prefix/lib/libpng16.a" || { echo "Run scripts/build-image-libs.sh first" >&2; exit 1; }
"$cc" -static $flags -std=c99 -Wall -Wextra -Wl,--gc-sections \
  -I"$root/native/svgpngrenderer/third_party" -I"$image_prefix/include" \
  "$root/native/svgpngrenderer/main.c" "$root/native/svgpngrenderer/raster.c" \
  "$image_prefix/lib/libpng16.a" "$image_prefix/lib/libjpeg.a" "$image_prefix/lib/libz.a" \
  -lm -o "$out/svgpngrenderer"

"$strip" --strip-all "$out/sbbase" "$out/sbwebserver" "$out/sbproxy" "$out/sb-storage-helper" "$out/sb-storage-updater" "$out/svgpngrenderer"
file "$out/sbbase" "$out/sbwebserver" "$out/sbproxy" "$out/sb-storage-helper" "$out/sb-storage-updater"
"$size" "$out/sbbase" "$out/sbwebserver" "$out/sbproxy" "$out/sb-storage-helper" "$out/sb-storage-updater"
file "$out/svgpngrenderer"
"$size" "$out/svgpngrenderer"
for binary in sbbase sbwebserver sbproxy sb-storage-helper sb-storage-updater svgpngrenderer; do
  if readelf -l "$out/$binary" | grep -q INTERP; then echo "$binary unexpectedly has an ELF interpreter" >&2; exit 1; fi
  if readelf -d "$out/$binary" 2>/dev/null | grep -q NEEDED; then echo "$binary unexpectedly has shared dependencies" >&2; exit 1; fi
done

sh "$root/scripts/verify-arm.sh" "$platform" "$out" "$toolchain/bin/$target-readelf"

{
  echo "target=$platform"
  echo "flags=$flags"
  "$cc" --version | head -1
  sha256sum "$cc" "$tls_prefix/lib/libcurl.a" "$wolf_prefix/lib/libwolfssl.a" \
    "$image_prefix/lib/libz.a" "$image_prefix/lib/libpng16.a" "$image_prefix/lib/libjpeg.a"
} > "$out/build-manifest.txt"

# WP1/WP6a/WP2 build isolated music components; the runtime stays disabled.
ARM_TOOLCHAIN_DIR="$toolchain" sh "$root/scripts/build-music-arm.sh" "$platform"
