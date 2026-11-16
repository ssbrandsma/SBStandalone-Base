#!/bin/sh
# Build local review artifacts only; this script never publishes or deploys.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
target=${1:-all}
case "$target" in baby|fab4|all) ;; *) echo 'usage: build-release.sh baby|fab4|all' >&2;exit 2;; esac
portal=${ARM_PORTAL_TLS_PREFIX:-$root/build-portal-tls/prefix}
if [ ! -f "$portal/lib/libwolfssl.a" ]; then sh "$root/scripts/build-portal-tls.sh"; fi
image=${ARM_IMAGE_PREFIX:-$root/build-image-arm/prefix}
if [ ! -f "$image/lib/libpng16.a" ] || [ ! -f "$image/lib/libjpeg.a" ]; then sh "$root/scripts/build-image-libs.sh"; fi
export SB_PACKAGE_UPNP=1
export SB_PACKAGE_VERSION=$(tr -d '\r\n' < "$root/VERSION")
export SB_PACKAGE_OUTPUT=${SB_RELEASE_OUTPUT:-$root/build-package/$SB_PACKAGE_VERSION}
sh "$root/scripts/build-arm.sh" "$target"
sh "$root/packaging/build-package.sh" "$target"
python3 "$root/scripts/verify-release.py" "$root" "$SB_PACKAGE_OUTPUT" "$target"
