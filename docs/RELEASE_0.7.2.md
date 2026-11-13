# Standalone Base 0.7.2

## Changes

- Support the measured primary-first Radio firmware 7.7.3 r16676 layout:
  kernel/cramfs at IDs 0/1, production ubifs at ID 2, and backups at IDs 3/4.
- Retain the previously supported backup-first layout and all destructive
  initialization gates. The new profile requires exact sizes, geometry,
  health and firmware identity; mixed pairs and malformed metadata are refused.
- Match native inventory ID/name tokens exactly, preventing kernel_bak from
  satisfying a requirement for kernel.
- Report the selected layout profile in storage diagnostics.

Both Baby and Fab4 static packages are published. The new Extended Storage
profile applies to Baby only; Fab4 platform restrictions are unchanged. No
kernel, bootloader, module or filesystem-image changes are included. Native
Music remains disabled in normal packages; experimental fence modules are
excluded. Radio remains at the separately published 0.9.20.

See [layout analysis and qualification](EXTENDED_STORAGE_773_PRIMARY_FIRST.md).

## Validation

Both complete ARM builds passed static ELF, CPU and dependency validation.
All 25 selected host regressions passed, covering storage, firmware recovery,
protocol, Comet, Now Playing and browser pairing. Both targets passed QEMU
version checks, Base self-test, storage fixtures, standby and Radio connection
regressions. The storage helpers in the packages exactly match the separately
validated builds; that implementation also passed ASan/UBSan checks.

On the owner's 7.7.3 Radio, RAM-only qualification and subsequent targeted
component installation returned AVAILABLE and INITIALIZATION_SUPPORTED=1.
The owner then initiated storage setup and reported success. Read-only follow-up
confirmed volume 5 creation, healthy sbdata, image writing, driver loading,
mounting and migration of all 646 files/85 directories, ending EXIT_CODE=0.
The device still reported REBOOT_REQUIRED=1 with the original boot ID, so
post-reboot bind activation/persistence is not claimed. No reboot was issued
as part of release publication.

Existing unrelated compiler warnings in image-render path sizing and isolated
music-scanner indentation remain. Proxy-specific host TLS tests were not rerun;
the proxy's source change for this release is its version string.

## Publication

Published to the existing 49.12.198.91 download service:

- StandaloneBase-0.7.2-baby.zip
- StandaloneBase-0.7.2-fab4.zip

Final hashes and URLs are in `packaging/releases/0.7.2.json`. All public ZIPs,
Base/Radio/combined XML routes, XML checksum sidecars, public config, mounted
bootstrap config and running bootstrap container were verified. Live bootstrap
queries advertise Base 0.7.2 and Radio 0.9.20 for both platforms on firmware
7.7.2, 7.7.3 and 9.0.2. Existing Radio/Spotify/Qobuz metadata is preserved.

The frozen `0.7.2.package-config.json` records the build-time catalog, including
Base 0.7.1. Root config.json and current external XML catalogs advertise 0.7.2.
The immutable ZIPs are not repacked to embed their own checksums. Historical
release files and archives remain unchanged. Server catalog rollback backups
were retained; private build, test, snapshot and verification receipts are in
`.local/release-072/`.
