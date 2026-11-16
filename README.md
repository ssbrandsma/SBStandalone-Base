# StandaloneBase

The current private **0.7.5** candidate integrates UPnP My Music/original playlist, transactional installation, VPN and secure HTTPS management. See [delivery and hardware acceptance status](docs/RELEASE_0.7.5.md). Local builds do not publish or deploy.

The minimum advertised SqueezePlay firmware is 7.7.2. Extended Storage initial
creation accepts the measured `7.7.2 r9663` layout with the same strict geometry
and capacity checks as 7.7.3; see [Extended Storage](docs/EXTENDED_STORAGE.md).

The web interface includes an authenticated **Virtual Radio** at `/remote` for
live LCD capture and remote operation through the real Jive action system. See
[`docs/VIRTUAL_RADIO_IMPLEMENTATION.md`](docs/VIRTUAL_RADIO_IMPLEMENTATION.md).
Photorealistic and Classic layouts, asset provenance, normalized geometry, and
hotspot calibration are documented in
[`docs/VIRTUAL_RADIO_PHOTOREALISTIC.md`](docs/VIRTUAL_RADIO_PHOTOREALISTIC.md).

StandaloneBase is a three-process native runtime plus SqueezePlay applet for a Logitech Squeezebox Radio. It provides a minimal local LMS-compatible bootstrap (`sbbase`), the existing configuration web server (`sbwebserver`), and the existing loopback HTTPS compatibility proxy (`sbproxy`).

Browser management uses physical six-digit PIN pairing, with optional remembered browsers and HttpOnly cookies; see [Web Access pairing](docs/WEB_ACCESS_PAIRING.md).

The local LMS service now supplies the stock **Settings → Alarm Clock** menus
and alarm scheduling, reusing the Radio's resident AlarmSnooze UI and fallback
sound. See [alarms](docs/ALARMS.md) for sources, persistence, tests and the
physical acceptance checklist. Hardware validation is still pending.

The local **Settings → Sleep** menu provides a monotonic sleep countdown.
The browser's **Settings → Backup & Restore** section exports and validates
configuration-only backups and can restore them with a SqueezePlay restart.
See [sleep timer](docs/SLEEP_TIMER.md) and [backup/restore](docs/BACKUP_RESTORE.md)
for behavior, exclusions and the remaining physical test checklist.

SVG artwork can be converted through the local image endpoint and standalone
ARMv5 renderer; see [SVG rendering](docs/SVG_RENDERING.md).

The authenticated web interface also provides read-only live system logs and a
stateless System Performance/process monitor. See
[`docs/SYSTEM_LOG_VIEWER.md`](docs/SYSTEM_LOG_VIEWER.md) and
[`docs/PROCESS_MONITOR_IMPLEMENTATION.md`](docs/PROCESS_MONITOR_IMPLEMENTATION.md).

This repository is an engineering baseline. Host protocol tests are automated. Local-LMS activation is intentionally not performed until the hardware checklist is approved and run on a recoverable Radio.

The development VPN subsystem adds shared native and web controls for compatible
Radio/Touch firmware. See the [VPN user guide](docs/VPN.md),
[implementation](docs/VPN_IMPLEMENTATION.md), and [validation report](docs/VPN_DELIVERY.md).
See the [production-readiness review](docs/VPN_READINESS_REVIEW.md) for final build evidence
and pending hardware acceptance. This remains a private development candidate.

## Build and test

On Linux with CMake, a C compiler, pthreads, and libcurl development files:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`scripts/build-arm.sh` automatically reuses the hardware-validated toolchain and static TLS prefix in `C:/Projects/SBHttpsProxy` when run under WSL. Override `SBHTTPSPROXY_ROOT`, `ARM_TOOLCHAIN_DIR`, or `ARM_TLS_PREFIX` for another location. `packaging/build-package.sh` stages the applet and creates the installer ZIP.

See `docs/deployment.md`, `docs/recovery.md`, and `docs/testing.md` before device use.

Read-only flash capacity reporting and the disabled expansion research milestone are documented in `docs/STORAGE.md`, `docs/STORAGE_TESTING.md`, and `docs/STORAGE_RECOVERY.md`.

The host-tested Extended Storage integration and its explicit physical-validation boundary are documented in `docs/EXTENDED_STORAGE.md`.

For retained boot support, native multi-UBIFS mounting and non-destructive recovery
after firmware updates, see [post-firmware recovery](docs/POST_FIRMWARE_RECOVERY.md).
The report distinguishes verified storage persistence from the remaining watchdog
stability issue; it is not a guarantee for every future firmware.

On Windows:
```
wsl.exe sh -lc 'cd /mnt/c/Projects/SBStandaloneBase/StandaloneBase && ./scripts/build-arm.sh'
$env:SB_RELEASE_BASE_URL = 'https://downloads.example.invalid/standalonebase'
.\packaging\build-package.ps1
```

To produce another version, update:
- Release version in `packaging/build-package.sh` and `packaging/target-metadata.py`
- Build both targets using `sh scripts/build-arm.sh all`, then package with `sh packaging/build-package.sh all`
- The displayed version in [StandaloneBaseApplet.lua]
- Native version constants in [sbbase.h (line 10)]and [sbproxy.c (line 19)]
- standalone_base_version in the tracked root `config.json`; update its applet versions, public download URLs and ZIP checksums when publishing applets


## deploy remote

Deployment endpoints, account names, filesystem paths and container names are
private local configuration. Copy `packaging/deployment.env.example` to
`.local/deployment.env`, populate it locally, and never commit that file.

The public root `config.json` includes Base and the other applet releases.
See [catalog synchronization](docs/APPLET_CATALOG_SYNC.md) for automatic GitHub
refresh, local merge rules, and the Applets page's manual update check.

## Artwork routing

See [local artwork routing](docs/ARTWORK_ROUTING.md) for the HTTP/HTTPS loopback
transport, SVG integration, firmware adapters, tests and hardware validation.

## HLS audio

See [the HLS implementation report](docs/HLS_AUDIO.md) for the native loopback
AAC/MP3 relay, supported formats, resource limits, tests and hardware status.
See [timed ID3 metadata](docs/HLS_TIMED_METADATA.md) for session-aware track
updates, bounded artwork and synchronization limits.
The [metadata-delivery optimization](docs/HLS_METADATA_DELIVERY.md) replaces
500 ms HTTP polling with bounded long polling and one-shot playback timers.

## Release 0.7.0

Baby and Fab4 share one source tree, with target-specific static binaries and installer ZIPs. Fab4 includes live landscape Virtual Touch capture and touchscreen controls. See [dual-platform build instructions](docs/DUAL_PLATFORM.md) and [Virtual Touch validation](docs/VIRTUAL_TOUCH.md).

## Release 0.7.1

Adds remembered-browser web access, larger access codes, firmware standby power/sequence handling and shared Radio connection fixes. The matching Standalone Radio standby guard is released separately. Baby and Fab4 packages retain Standalone Music disabled by default.

## Release 0.7.2

Supports the validated primary-first 7.7.3 r16676 Radio storage layout, retaining strict volume identity, geometry and health checks. Native compatibility matching now uses exact volume names. Baby and Fab4 packages retain the existing feature gates; the new layout support applies to Baby. See [layout validation](docs/EXTENDED_STORAGE_773_PRIMARY_FIRST.md).

## Release 0.7.3

Adds persistent XML applet repositories with public URL downloads, cached refreshes, source conflict handling and model/firmware filtering. Includes the remaining WP4a.6 research documents and diagnostic tools, with experimental playback inactive. See [release notes](docs/RELEASE_0.7.3.md).

Secure management development: [user guide](docs/SECURE_WEB_PORTAL.md), [TLS architecture](docs/TLS_ARCHITECTURE.md). HTTPS provisioning replaces the mandatory SSH tunnel; physical certificate verification and HTTPS pairing are required. See the acceptance notes before release.
