#!/bin/sh
# Build isolated WP1/WP6a/WP2 archives/helpers; never start them in normal Base.
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
  *) echo 'usage: build-music-arm.sh baby|fab4|all' >&2; exit 2 ;;
esac
flags="$flags -ffunction-sections -fdata-sections -D_FILE_OFFSET_BITS=64"
out="$root/build-arm/$platform/music"
mkdir -p "$out"
sqlite_flags='-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_ENABLE_DBSTAT_VTAB -DSQLITE_DQS=0'
stamp=$( { sha256sum "$root/native/sbmusic/third_party/sqlite/sqlite3.c" "$root/native/sbmusic/third_party/sqlite/sqlite3.h"; printf '%s\n' "$flags $sqlite_flags"; "$cc" --version; } | sha256sum | cut -d' ' -f1 )
if [ ! -f "$out/sqlite.sha256" ] || [ "$(cat "$out/sqlite.sha256")" != "$stamp" ] || [ ! -f "$out/libsbmusic-sqlite.a" ]; then
  "$cc" $flags $sqlite_flags -pthread -c "$root/native/sbmusic/third_party/sqlite/sqlite3.c" -o "$out/sqlite.o"
  "$ar" rcs "$out/libsbmusic-sqlite.a" "$out/sqlite.o"
  printf '%s\n' "$stamp" > "$out/sqlite.sha256"
fi
"$cc" $flags -std=c99 -Wall -Wextra -Wpedantic -pthread -c "$root/native/sbmusic/music_library.c" -o "$out/music_library.o"
"$cc" $flags -std=c99 -Wall -Wextra -Wpedantic -c "$root/native/common/music_contracts.c" -o "$out/music_contracts.o"
"$ar" rcs "$out/libsbmusic.a" "$out/music_library.o" "$out/music_contracts.o"
for module in music_storage music_storage_linux music_storage_library music_storage_adapter; do
  "$cc" $flags -std=c99 -Wall -Wextra -Wpedantic -pthread -c "$root/native/sbmusic/$module.c" -o "$out/$module.o"
done
"$ar" rcs "$out/libsbmusic-storage.a" "$out/music_storage.o" "$out/music_storage_linux.o" "$out/music_storage_library.o" "$out/music_storage_adapter.o"
"$cc" $flags -static -pthread -Wl,--gc-sections -I"$root/native/sbmusic" \
  "$root/tests/music/test_storage.c" "$out/libsbmusic-storage.a" "$out/libsbmusic.a" "$out/libsbmusic-sqlite.a" -o "$out/test_music_storage"
"$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/test_music_storage"
for module in music_metadata music_scanner; do
  "$cc" $flags -std=c99 -Wall -Wextra -Wpedantic -pthread -c "$root/native/sbmusic/$module.c" -o "$out/$module.o"
done
"$ar" rcs "$out/libsbmusic-metadata.a" "$out/music_metadata.o"
"$ar" rcs "$out/libsbmusic-scanner.a" "$out/music_scanner.o"
"$cc" $flags -std=c99 -static -Wl,--gc-sections -I"$root/native/sbmusic" \
  "$root/native/sbmusic/music_metadata_helper.c" "$out/libsbmusic-metadata.a" -o "$out/sbmusic-metadata-helper"
"$cc" $flags -std=c99 -static -Wl,--gc-sections -I"$root/native/sbmusic" \
  "$root/tests/music/metadata_fault.c" "$out/libsbmusic-metadata.a" -o "$out/music_metadata_fault"
"$cc" $flags -std=c99 -static -pthread -Wl,--gc-sections -I"$root/native/sbmusic" \
  "$root/tests/music/test_scanner.c" "$out/libsbmusic-scanner.a" "$out/libsbmusic-storage.a" \
  "$out/libsbmusic.a" "$out/libsbmusic-metadata.a" "$out/libsbmusic-sqlite.a" -o "$out/test_music_scanner"
for binary in sbmusic-metadata-helper music_metadata_fault test_music_scanner; do
  "$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/$binary"
done
"$cc" $flags -std=c99 -static -pthread -Wall -Wextra -Wpedantic -Wl,--gc-sections -I"$root/native/sbmusic" \
  "$root/native/sbmusic/music_library_tool.c" "$root/native/sbmusic/music_snapshot.c" \
  "$out/libsbmusic-scanner.a" "$out/libsbmusic-storage.a" "$out/libsbmusic.a" \
  "$out/libsbmusic-sqlite.a" -o "$out/sbmusic-library"
"$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/sbmusic-library"
"$cc" $flags -static -pthread -Wl,--gc-sections -I"$root/native/sbmusic" \
  "$root/tests/music/test_library.c" "$out/libsbmusic.a" "$out/libsbmusic-sqlite.a" -o "$out/test_music_library"
file "$out/test_music_library"
"$sdk/bin/arm-buildroot-linux-musleabi-size" "$out/test_music_library" "$out/libsbmusic.a" "$out/libsbmusic-sqlite.a"
"$sdk/bin/arm-buildroot-linux-musleabi-strip" --strip-all "$out/test_music_library"
{ printf '%s\n' "flags=$flags" "sqlite_flags=$sqlite_flags" "sqlite_input=$stamp"; sha256sum "$out"/libsbmusic*.a "$out"/test_music_* "$out/sbmusic-metadata-helper" "$out/music_metadata_fault" "$out/sbmusic-library"; } > "$out/manifest.txt"
