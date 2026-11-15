#!/bin/sh
# Optional production network-music overlay, called by build-package.sh.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
platform=$1
stage=$2
upnp=$3
case "$platform" in baby|fab4) ;; *) exit 2;; esac
test -d "$stage"
service="$upnp/build/$platform/sbupnp"
runtime="$root/build-arm/$platform/music-session"
test -f "$service"
test -f "$runtime/sbbase_music"
test -f "$runtime/sbwebserver_music"
test "$(git -C "$upnp/.local/pupnp" rev-parse HEAD)" = fac0494691434c35243131f66a0b27ea0c29f700
for binary in "$service" "$runtime/sbbase_music" "$runtime/sbwebserver_music"; do
 if readelf -l "$binary" | grep -q INTERP; then echo "Dynamic binary: $binary" >&2; exit 1; fi
done
cp "$service" "$stage/sbupnp"
cp "$runtime/sbbase_music" "$stage/sbbase-network"
cp "$runtime/sbwebserver_music" "$stage/sbwebserver-network"
cp "$root/applet/NetworkMusic.lua" "$root/applet/PlaybackFenceV6.lua" "$stage/"
cp "$upnp/.local/pupnp/COPYING" "$stage/PUPNP-LICENSE"
cp "$upnp/THIRD_PARTY_NOTICES.md" "$stage/UPNP-THIRD-PARTY-NOTICES"
cp "$upnp/licenses/MUSL-COPYRIGHT" "$stage/MUSL-COPYRIGHT"
cp "$upnp/licenses/GCC-RUNTIME-EXCEPTION" "$stage/GCC-RUNTIME-EXCEPTION"
cp "$upnp/licenses/GPL-3.0" "$stage/GCC-GPL-3.0"
cp "$upnp/build/$platform/manifest.txt" "$stage/upnp-build-manifest.txt"
cp "$runtime/manifest.txt" "$stage/network-runtime-manifest.txt"
chmod 0755 "$stage/sbupnp" "$stage/sbbase-network" "$stage/sbwebserver-network"
(cd "$stage" && sha256sum sbupnp sbbase-network sbwebserver-network) > "$stage/network-music.sha256"
