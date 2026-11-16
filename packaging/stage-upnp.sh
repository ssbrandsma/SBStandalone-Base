#!/bin/sh
# Optional production network-music overlay, called by build-package.sh.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
platform=$1
stage=$2
case "$platform" in baby|fab4) ;; *) exit 2;; esac
test -d "$stage"
build="$root/build-upnp/$platform"
service="$build/sbupnp"
runtime="$root/build-arm/$platform/music-session"
test -f "$service"
test -f "$runtime/sbbase_music"
test -f "$runtime/sbwebserver_music"
grep -qx 'pupnp=fac0494691434c35243131f66a0b27ea0c29f700' "$build/manifest.txt"
expected=$(sed -n 's/^sha256=//p' "$build/manifest.txt")
test "$(sha256sum "$service" | cut -d' ' -f1)" = "$expected" || { echo 'Stale UPnP build manifest' >&2; exit 1; }
for binary in "$service" "$runtime/sbbase_music" "$runtime/sbwebserver_music"; do
 readelf -h "$binary" | grep -q 'Machine:.*ARM'
 if readelf -l "$binary" | grep -q INTERP; then echo "Dynamic binary: $binary" >&2; exit 1; fi
 if readelf -d "$binary" | grep -q NEEDED; then echo "Shared dependency: $binary" >&2; exit 1; fi
done
cp "$service" "$stage/sbupnp"
cp "$runtime/sbbase_music" "$stage/sbbase-network"
cp "$runtime/sbwebserver_music" "$stage/sbwebserver-network"
cp "$root/applet/NetworkMusic.lua" "$root/applet/PlaybackFenceV6.lua" "$stage/"
for module in NetworkProfile OwnershipHooks OwnershipRuntime OwnershipClient OwnershipHttp; do
 cp "$root/applet/$module.lua" "$stage/"
done
python3 - "$stage" "$platform" <<'PYCOMPAT'
import json,sys,os
from pathlib import Path
Path(sys.argv[1], 'network-music.compat.json').write_text(json.dumps(dict(
 schema='SB_NETWORK_MUSIC_1',version=os.environ['SB_PACKAGE_VERSION'],target=sys.argv[2],resource_wire=3,upnp_ipc=1,
 ownership_wire=1,queue_limit=256,renderer_default=False),sort_keys=True)+'\n')
catalog=Path(sys.argv[1], 'config.json')
value=json.loads(catalog.read_text())
value['standalone_base_version']=os.environ['SB_PACKAGE_VERSION']
value['applets']=[item for item in value.get('applets',[]) if item.get('name')!='StandaloneBase']
catalog.write_text(json.dumps(value,indent=2)+'\n')
PYCOMPAT
for notice in CURL-COPYING WOLFSSL-LICENSING GPL-2.0 GPL-3.0 SBWEBSERVER-LICENSE STANDALONEBASE-LICENSE MPL-2.0 CA-BUNDLE-NOTICE; do
 cp "$root/native/sbupnp/licenses/$notice" "$stage/$notice"
done
cp "$root/native/sbproxy/LICENSE" "$stage/SBPROXY-LICENSE"
for notice in PUPNP-LICENSE SBUPNP-LICENSE UPNP-THIRD-PARTY-NOTICES MUSL-COPYRIGHT GCC-RUNTIME-EXCEPTION PUPNP-SOURCE-NOTICES; do
 cp "$build/notices/$notice" "$stage/$notice"
done
cp "$build/notices/GPL-3.0" "$stage/GCC-GPL-3.0"
cp "$build/manifest.txt" "$stage/upnp-build-manifest.txt"
cp "$runtime/manifest.txt" "$stage/network-runtime-manifest.txt"
chmod 0755 "$stage/sbupnp" "$stage/sbbase-network" "$stage/sbwebserver-network"
(cd "$stage" && sha256sum sbupnp sbbase-network sbwebserver-network) > "$stage/network-music.sha256"
