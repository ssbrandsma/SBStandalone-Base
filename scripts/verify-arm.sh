#!/bin/sh
set -eu
platform=$1
out=$2
readelf=${3:-readelf}
for binary in sbbase sbwebserver sbproxy sb-storage-helper sb-storage-updater svgpngrenderer sbvpn; do
  f="$out/$binary"
  if test "$binary" = sbvpn && ! test -f "$f"; then continue; fi
  test -x "$f"
  "$readelf" -h "$f" | grep -q 'Machine:.*ARM'
  "$readelf" -h "$f" | grep -q 'Version5 EABI'
  attrs=$("$readelf" -A "$f")
  case "$platform" in
    baby) echo "$attrs" | grep -q 'Tag_CPU_arch: v5TE' ;;
    fab4) echo "$attrs" | grep -q 'Tag_CPU_arch: v6$' ;;
    *) exit 2 ;;
  esac
  if echo "$attrs" | grep -q 'Tag_ABI_VFP_args: VFP registers'; then exit 1; fi
  if "$readelf" -l "$f" | grep -q INTERP; then exit 1; fi
  if "$readelf" -d "$f" 2>/dev/null | grep -q NEEDED; then exit 1; fi
  if "$readelf" -Ws "$f" | grep -E 'GLOBAL.*UND' ; then exit 1; fi
done
printf 'Verified static ARM EABI binaries for %s\n' "$platform"
