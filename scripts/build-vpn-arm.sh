#!/bin/sh
# Build only; OpenVPN itself remains supplied by the firmware.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk=${ARM_TOOLCHAIN_DIR:-/mnt/c/Projects/SBHttpsProxy/build/toolchains/armv5-eabi--musl--stable-2020.02-2}
platform=${1:-baby}
case "$platform" in
 baby) architecture='-march=armv5te -mtune=arm926ej-s'; definition='' ;;
 fab4) architecture='-march=armv6 -mtune=arm1136jf-s'; definition='-DSB_TARGET_FAB4=1' ;;
 all) sh "$0" baby; sh "$0" fab4; exit ;;
 *) echo 'usage: build-vpn-arm.sh baby|fab4|all' >&2; exit 2 ;;
esac
out=${SB_VPN_OUTPUT:-$root/build-arm/$platform}
mkdir -p "$out"
"$sdk/bin/arm-buildroot-linux-musleabi-gcc" -std=c99 -Os -marm $architecture $definition -mfloat-abi=soft -static -pthread \
 -D_GNU_SOURCE -DMG_ENABLE_EPOLL=0 -DMG_ENABLE_POLL=1 -DMG_ENABLE_DIRLIST=0 -DMG_ENABLE_SSI=0 \
 -Wall -Wextra -I"$root/native/sbvpn" -I"$root/native/sbwebserver/third_party" \
 "$root/native/sbvpn/main.c" "$root/native/sbvpn/profile.c" "$root/native/sbvpn/store.c" \
 "$root/native/sbvpn/process.c" "$root/native/sbvpn/ownership.c" "$root/native/sbvpn/network.c" "$root/native/sbvpn/json.c" "$root/native/sbvpn/traffic.c" "$root/native/sbvpn/verification.c" \
 "$root/native/sbwebserver/third_party/mongoose.c" -o "$out/sbvpn"
"$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/sbvpn"
"$sdk/bin/arm-buildroot-linux-musleabi-readelf" -h -l -d "$out/sbvpn" > "$out/sbvpn-elf.txt"
if grep -Eq 'INTERP|NEEDED' "$out/sbvpn-elf.txt"; then exit 1; fi
sha256sum "$out/sbvpn" > "$out/sbvpn-sha256.txt"

attrs=$("$sdk/bin/arm-buildroot-linux-musleabi-readelf" -A "$out/sbvpn")
case "$platform" in baby) echo "$attrs" | grep -q 'Tag_CPU_arch: v5TE' ;; fab4) echo "$attrs" | grep -q 'Tag_CPU_arch: v6$' ;; esac
if echo "$attrs" | grep -q 'Tag_ABI_VFP_args: VFP registers'; then exit 1; fi
