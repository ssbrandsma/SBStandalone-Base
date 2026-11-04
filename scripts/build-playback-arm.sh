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
 *) echo 'usage: build-playback-arm.sh baby|fab4|all' >&2; exit 2 ;;
esac
sh "$root/scripts/build-music-arm.sh" "$platform"
music="$root/build-arm/$platform/music"
out="$root/build-arm/$platform/playback"
image=${ARM_IMAGE_PREFIX:-$root/build-image-arm/prefix}
mkdir -p "$out"
flags="$flags -D_FILE_OFFSET_BITS=64 -ffunction-sections -fdata-sections -std=c99 -Wall -Wextra -Wpedantic -UNDEBUG"
common="$root/native/common/music_resource_ipc.c $root/native/common/music_http.c"
core="$root/native/sbbase/src/music_queue.c $root/native/sbbase/src/music_load.c $root/native/sbbase/src/music_file.c $root/native/sbbase/src/music_playback.c $root/native/sbmusic/music_async.c $root/native/sbbase/src/music_browse.c $root/native/sbbase/src/nowplaying.c $root/native/sbbase/src/lms_command.c $root/native/sbbase/src/slim_controller.c $root/native/sbbase/src/output_queue.c $root/native/sbbase/src/protocol.c"
libs="$music/libsbmusic-storage.a $music/libsbmusic.a $music/libsbmusic-sqlite.a"
"$cc" $flags -static -pthread -Wl,--gc-sections -I"$root/native/sbbase/include" \
 "$root/tests/music/test_queue.c" "$root/native/sbbase/src/music_queue.c" "$root/native/common/music_contracts.c" -o "$out/test_music_queue"
"$cc" $flags -static -Wl,--gc-sections -I"$root/native/common" \
 "$root/tests/music/test_music_http.c" "$root/native/common/music_http.c" -o "$out/test_music_http_policy"
"$cc" $flags -static -pthread -Wl,--gc-sections -I"$root/native/sbwebserver/src" \
 "$root/tests/music/test_stream.c" "$root/native/sbwebserver/src/music_stream.c" "$root/native/common/music_resource_ipc.c" -o "$out/test_music_stream"
"$cc" $flags -static -pthread -Wl,--gc-sections -I"$root/native/sbbase/include" \
 "$root/tests/music/test_load.c" $core $common $libs -o "$out/test_music_load"
"$cc" $flags -static -pthread -Wl,--gc-sections -I"$root/native/sbbase/include" -I"$root/native/sbwebserver/src" -I"$root/native/common" -I"$root/native/sbmusic" \
 "$root/tests/music/test_playback.c" "$root/native/sbwebserver/src/music_stream.c" $core $common \
 "$music/libsbmusic-scanner.a" "$music/libsbmusic-metadata.a" $libs -o "$out/test_music_playback"
"$cc" $flags -D_GNU_SOURCE -DSB_MUSIC_BROWSE_TESTING=1 -DSB_MUSIC_PLAYBACK_TESTING=1 \
 -static -pthread -Wl,--gc-sections -I"$root/native/sbbase/include" \
 "$root/native/sbbase/src/main.c" "$root/native/sblifecycle/tinysc.c" -pthread "$root/native/sbbase/src/ownership_bridge.c" "$root/native/sbbase/src/alarms.c" "$root/native/sbbase/src/sleep.c" \
 $core $common $libs -o "$out/sbbase_music_test"
"$cc" $flags -D_GNU_SOURCE -DSB_MUSIC_PLAYBACK_TESTING=1 -static -pthread -Wl,--gc-sections \
 -DMG_ENABLE_EPOLL=0 -DMG_ENABLE_POLL=1 -DMG_ENABLE_DIRLIST=0 -DMG_ENABLE_SSI=0 \
 -I"$root/native/sbwebserver/src" -I"$root/native/sbwebserver/third_party" -I"$root/native/sbbase/include" -I"$image/include" \
 "$root/native/sbwebserver/src/"*.c "$root/native/sbwebserver/third_party/mongoose.c" \
 "$root/native/sbbase/src/nowplaying.c" "$root/native/sbbase/src/alarms.c" $common "$image/lib/libz.a" -o "$out/sbwebserver_music_test"
for binary in test_music_queue test_music_http_policy test_music_stream test_music_load test_music_playback sbbase_music_test sbwebserver_music_test; do
 "$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/$binary"
 file "$out/$binary"
 if readelf -l "$out/$binary" | grep -q INTERP; then exit 1; fi
 if readelf -d "$out/$binary" | grep -q NEEDED; then exit 1; fi
done
{ printf '%s\n' "flags=$flags"; "$cc" --version | head -1;
  for binary in test_music_queue test_music_http_policy test_music_stream test_music_load test_music_playback sbbase_music_test sbwebserver_music_test; do
    sha256sum "$out/$binary"
  done
} > "$out/manifest.txt"
