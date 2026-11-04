#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
sdk=${ARM_TOOLCHAIN_DIR:-/mnt/c/Projects/SBHttpsProxy/build/toolchains/armv5-eabi--musl--stable-2020.02-2}
cc="$sdk/bin/arm-buildroot-linux-musleabi-gcc"
mkdir -p .local/wp0
for target in baby fab4; do
  flags='-Os -marm -march=armv5te -mtune=arm926ej-s -mfloat-abi=soft'
  if [ "$target" = fab4 ]; then flags='-Os -marm -march=armv6 -mtune=arm1136jf-s -mfloat-abi=soft -DSB_TARGET_FAB4=1'; fi
  "$cc" $flags -std=c99 -Wall -Wextra -Wpedantic -static -I native/sbbase/include \
    tests/music/test_contracts.c native/common/music_contracts.c native/sbbase/src/nowplaying.c \
    -o ".local/wp0/contracts-$target"
  "$cc" $flags -std=c99 -Wall -Wextra -Wpedantic -static -I native/sbbase/include \
    tests/music/test_commands.c native/sbbase/src/lms_command.c native/sbbase/src/nowplaying.c \
    -o ".local/wp0/commands-$target"
  if command -v qemu-arm >/dev/null 2>&1; then
    qemu-arm ".local/wp0/contracts-$target"
    qemu-arm ".local/wp0/commands-$target"
  else
    echo "qemu-arm missing: target unit execution pending for $target" >&2
  fi
done
