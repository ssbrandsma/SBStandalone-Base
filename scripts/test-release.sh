#!/bin/sh
# Run the configured host suite without sending discovery traffic onto the LAN.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-$root/build-upnp/host}
test "$(id -u)" = 0 || { echo 'Root is required for the private network namespace' >&2; exit 2; }
exec unshare --net sh "$root/tests/upnp/run-isolated.sh" ctest --test-dir "$build" --output-on-failure
