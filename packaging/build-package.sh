#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# The cross-toolchain is Linux ELF; Git Bash must enter WSL first.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    command -v wsl.exe >/dev/null 2>&1 || { echo "WSL is required for the Linux ARM toolchain" >&2; exit 1; }
    windows_script=$(cygpath -m "$root/packaging/build-package.sh")
    linux_script=$(MSYS_NO_PATHCONV=1 wsl.exe --exec wslpath -u "$windows_script" | tr -d '\r')
    test -n "$linux_script" || { echo "Could not resolve project path in WSL" >&2; exit 1; }
    echo "Running in WSL: packaging/build-package.sh $*"
    MSYS_NO_PATHCONV=1 exec wsl.exe --exec sh "$linux_script" "$@"
    ;;
esac

platform=${1:-baby}
case "$platform" in
 all) sh "$0" baby; sh "$0" fab4; exit ;;
 baby|fab4) ;;
 *) echo "Usage: $0 [baby|fab4|all]" >&2; exit 2 ;;
esac
sh "$root/scripts/verify-arm.sh" "$platform" "$root/build-arm/$platform"
config=${SB_PACKAGE_CONFIG:-$root/config.json}
test -f "$config" || { echo "missing package catalog: $config" >&2; exit 1; }
module="$root/artifacts/sbubifs-authorized.ko"
expected_sha=63652ce67df06a78abb84a4986253bdab02fbd7b7c000779c60b3d393ba9566b
image="$root/artifacts/ubi/sbdata-empty.ubifs"
expected_image_sha=34f26e33d80393c1f1f497e85dff6b78dbc6fc0e2df98ed1150ea45c6631065d
test "$(wc -c <"$module" | tr -d ' ')" = 199119 || { echo "incorrect authorized module size" >&2; exit 1; }
test "$(sha256sum "$module" | awk '{print $1}')" = "$expected_sha" || { echo "incorrect authorized module SHA-256" >&2; exit 1; }
test "$(wc -c <"$image" | tr -d ' ')" = 1806336 || { echo "incorrect sbdata image size" >&2; exit 1; }
test "$(sha256sum "$image" | awk '{print $1}')" = "$expected_image_sha" || { echo "incorrect sbdata image SHA-256" >&2; exit 1; }
stage=${SB_PACKAGE_STAGE:-$root/build-package/$platform/StandaloneBase}
output=${SB_PACKAGE_OUTPUT:-$root/build-package}
mkdir -p "$output"
output=$(CDPATH= cd -- "$output" && pwd)
zipfile="$output/StandaloneBase-0.7.3-$platform.zip"
rm -rf "$stage"
mkdir -p "$stage"
cp "$root/applet/Platform.lua" "$root/applet/StandaloneBaseMeta.lua" "$root/applet/StandaloneBaseApplet.lua" "$root/applet/StorageManager.lua" "$root/applet/ExtendedStorageState.lua" "$root/applet/TimeSync.lua" "$root/applet/TimeResolver.lua" "$root/applet/RemoteBridge.lua" "$root/applet/ServerMigration.lua" "$root/applet/NetworkAddress.lua" "$root/applet/strings.txt" "$stage/"
cp "$root/build-arm/$platform/sbbase" "$root/build-arm/$platform/sbwebserver" "$root/build-arm/$platform/sbproxy" "$root/build-arm/$platform/sb-storage-helper" "$root/build-arm/$platform/sb-storage-updater" "$stage/"
cp "$root/build-arm/$platform/svgpngrenderer" "$stage/"
cp "$root/applet/ImageRender.lua" "$root/applet/ArtworkUrlNormalizer.lua" "$stage/"
cp "$root/applet/Artwork.lua" "$stage/"
cp "$root/applet/InstallerCompat.lua" "$stage/"
cp "$root/applet/HttpTrace.lua" "$stage/"
cp "$root/applet/FirmwareUpdates.lua" "$stage/"
cp "$root/applet/AlarmPlayback.lua" "$stage/"
cp "$root/applet/LifecycleHeartbeat.lua" "$stage/"
cp "$root/build-arm/$platform/sb-tinysc-control" "$stage/"
chmod 0755 "$stage/sb-tinysc-control"
cp "$root/native/svgpngrenderer/THIRD_PARTY.md" "$stage/SVG-THIRD-PARTY-NOTICES"
chmod 0755 "$stage/svgpngrenderer"
cp "$root/scripts/storage-setup.sh" "$root/scripts/storage-boot.sh" "$stage/"
cp "$root/scripts/patch-squeezeplay-artwork.sh" "$stage/"
cp "$module" "$stage/sbubifs-authorized.ko"
cp "$image" "$stage/sbdata-empty.ubifs"
cp "$root/native/sbwebserver/web/index.html" "$stage/index.html"
cp "$root/native/sbwebserver/web/css/style.css" "$stage/style.css"
cp "$root/native/sbwebserver/third_party/yxml/COPYING" "$stage/YXML-LICENSE"
cp "$root/native/sbwebserver/web/js/repositories.js" "$stage/repositories.js"
cp "$root/applet/PlaybackFenceV4.lua" "$root/applet/RadioStop.lua" "$root/applet/SbCapture.lua" "$stage/"
cp "$root/native/sbwebserver/web/js/app.js" "$stage/app.js"
cp "$root/native/sbwebserver/web/js/backup.js" "$stage/backup.js"
cp "$root/native/sbwebserver/web/js/firmware.js" "$stage/firmware.js"
cp "$root/native/sbwebserver/web/js/remote.js" "$stage/remote.js"
cp "$root/native/sbwebserver/web/js/logs.js" "$stage/logs.js"
cp "$root/native/sbwebserver/web/js/performance.js" "$stage/performance.js"
cp "$root/native/sbwebserver/web/vendor/uplot/uPlot.iife.min.js" "$stage/uPlot.iife.min.js"
cp "$root/native/sbwebserver/web/vendor/uplot/uPlot.min.css" "$stage/uPlot.min.css"
cp "$root/native/sbwebserver/web/vendor/uplot/LICENSE" "$stage/UPLOT-LICENSE"
cp "$root/native/sbwebserver/web/assets/virtual-radio/radio-front.webp" "$stage/radio-front.webp"
cp "$root/native/sbwebserver/web/assets/virtual-radio/touch-front.webp" "$stage/touch-front.webp"
cp "$config" "$stage/config.json"
cp "$root/config/catalog.example.json" "$stage/catalog.json"
cp "$root/native/sbproxy/cacert.pem" "$stage/cacert.pem"
chmod 0755 "$stage/sbbase" "$stage/sbwebserver" "$stage/sbproxy" "$stage/sb-storage-helper" "$stage/sb-storage-updater" "$stage/storage-setup.sh" "$stage/storage-boot.sh" "$stage/patch-squeezeplay-artwork.sh"
cp "$root/build-arm/$platform/build-manifest.txt" "$stage/"
printf '%s\n' "$platform" > "$stage/build-target"
if [ "$platform" = fab4 ]; then
  rm "$stage/sbubifs-authorized.ko" "$stage/sbdata-empty.ubifs"
fi
if [ -n "${SB_PACKAGE_UPNP_ROOT:-}" ]; then
  sh "$root/packaging/stage-upnp.sh" "$platform" "$stage" "$SB_PACKAGE_UPNP_ROOT"
fi
rm -f "$zipfile"
python3 - "$stage" "$zipfile" <<'PYZIP'
import sys, zipfile, stat
from pathlib import Path
stage=Path(sys.argv[1])
with zipfile.ZipFile(sys.argv[2], 'w', zipfile.ZIP_DEFLATED) as z:
    executables={'sbbase','sbproxy','sbwebserver','svgpngrenderer','sb-storage-helper','sb-storage-updater','storage-setup.sh','storage-boot.sh','patch-squeezeplay-artwork.sh','sbupnp','sbbase-network','sbwebserver-network'}
    for f in sorted(stage.iterdir()):
        info=zipfile.ZipInfo.from_file(f,f.name)
        info.create_system=3
        info.external_attr=(stat.S_IFREG | (0o755 if f.name in executables else 0o644)) << 16
        info.compress_type=zipfile.ZIP_DEFLATED
        z.writestr(info,f.read_bytes())
PYZIP
echo "$zipfile"

python3 "$root/packaging/target-metadata.py" "$root" "$platform" "$zipfile"
