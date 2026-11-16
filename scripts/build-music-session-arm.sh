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
 *) echo 'usage: build-music-session-arm.sh baby|fab4|all' >&2; exit 2 ;;
esac
sh "$root/scripts/build-music-arm.sh" "$platform"
music="$root/build-arm/$platform/music"
out="$root/build-arm/$platform/music-session"
image=${ARM_IMAGE_PREFIX:-$root/build-image-arm/prefix}
portal_prefix=${ARM_PORTAL_TLS_PREFIX:-$root/build-portal-tls/prefix}
test -f "$portal_prefix/lib/libwolfssl.a"
mkdir -p "$out"
flags="$flags -D_FILE_OFFSET_BITS=64 -ffunction-sections -fdata-sections -std=c99 -Wall -Wextra -Wpedantic -UNDEBUG"
common="$root/native/common/music_resource_ipc.c $root/native/common/music_http.c $root/native/common/upnp_ipc.c $root/native/common/remote_http.c"
core="$root/native/sbbase/src/music_queue.c $root/native/sbbase/src/music_load.c $root/native/sbbase/src/music_file.c $root/native/sbbase/src/music_playback.c $root/native/sbmusic/music_async.c $root/native/sbbase/src/music_browse.c $root/native/sbbase/src/nowplaying.c $root/native/sbbase/src/lms_command.c $root/native/sbbase/src/slim_controller.c $root/native/sbbase/src/output_queue.c $root/native/sbbase/src/protocol.c"
libs="$music/libsbmusic-storage.a $music/libsbmusic.a $music/libsbmusic-sqlite.a"
"$cc" $flags -D_GNU_SOURCE -DSB_MUSIC_RUNTIME=1 \
 -static -pthread -Wl,--gc-sections -I"$root/native/sbbase/include" -I"$root/native/sbmusic" \
 "$root/native/sbbase/src/main.c" "$root/native/sblifecycle/tinysc.c" -pthread "$root/native/sbbase/src/ownership_bridge.c" "$root/native/sbbase/src/alarms.c" "$root/native/sbbase/src/sleep.c" \
 $core $common "$root/native/sbmusic/music_snapshot.c" "$root/native/sbmusic/music_snapshot_view.c" "$music/libsbmusic-scanner.a" "$music/libsbmusic-metadata.a" $libs -o "$out/sbbase_music"
"$cc" $flags -D_GNU_SOURCE -DSB_MUSIC_RUNTIME=1 -static -pthread -Wl,--gc-sections \
 -DMG_ENABLE_EPOLL=0 -DMG_ENABLE_POLL=1 -DMG_ENABLE_DIRLIST=0 -DMG_ENABLE_SSI=0 \
 -DMG_TLS=MG_TLS_WOLFSSL -I"$portal_prefix/include" -I"$portal_prefix/include/wolfssl" -include "$root/native/sbwebserver/src/portal_tls_config.h" \
 -I"$root/native/sbwebserver/src" -I"$root/native/sbwebserver/third_party" -I"$root/native/sbwebserver/third_party/yxml" -I"$root/native/sbbase/include" -I"$image/include" \
 "$root/native/sbwebserver/src/"*.c "$root/native/sbwebserver/third_party/mongoose.c" "$root/native/sbwebserver/third_party/yxml/yxml.c" \
 "$root/native/sbbase/src/nowplaying.c" "$root/native/sbbase/src/alarms.c" $common "$image/lib/libz.a" "$portal_prefix/lib/libwolfssl.a" -o "$out/sbwebserver_music"
"$cc" $flags -static -pthread -Wl,--gc-sections \
 "$root/native/sbbase/src/music_session.c" "$root/native/common/music_resource_ipc.c" -o "$out/sbmusic-session"
for binary in sbbase_music sbwebserver_music sbmusic-session; do
 "$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/$binary"
 file "$out/$binary"
 if readelf -l "$out/$binary" | grep -q INTERP; then exit 1; fi
 if readelf -d "$out/$binary" | grep -q NEEDED; then exit 1; fi
done
{ printf '%s\n' "flags=$flags"; "$cc" --version | head -1; sha256sum "$out/sbbase_music" "$out/sbwebserver_music" "$out/sbmusic-session" "$music/sbmusic-metadata-helper"; } > "$out/manifest.tmp"
mv "$out/manifest.tmp" "$out/manifest.txt"
