# Validation results

## Production integration qualification, 2026-11-15

- Host daemon/core build and CTest pass against pupnp 22.2.2.
- ASan/UBSan builds instrument the SDK as well as integration source; core and
  independent SSDP/HTTP/SOAP diagnostic tests pass.
- Baby and Fab4 static daemon builds pass core tests under QEMU ARM926/ARM1136.
  Executable sizes are 392,224 and 360,248 bytes respectively (unstripped).
- Base network runtime and web resource bridge cross-build statically for both
  targets. Physical Linux 2.6 execution remains unverified.
- The production fixture passes private IPC, duplicate-daemon exclusion,
  capabilities, metadata, browse/search, stale generations, 137-item pagination,
  Base authentication and asynchronous gateway checks.
- Full native chain passes for a track and a 137-item container: real SOAP,
  authenticated Base selection, atomic typed queue, SlimProto token and exact
  MP3 bytes through the actual web resource bridge. Only the firmware peer and
  physical ownership attestation are simulated; no local database is initialized.
- One hundred repeated browse requests changed host RSS from 2,252 to 2,392 KiB;
  descriptors remained 8 and threads remained 10. This is a short allocation
  check, not a prolonged device soak.
- Independent renderer SOAP/GENA tests pass discovery, descriptions, actual-state
  correlation against a simulated Base, ownership/standby rejection, volume/mute,
  subscriptions, renewal, unsubscribe, callback origin restrictions and disable.
- Native network playback harness passes without SQLite: typed network identity,
  resource wire v3 socket transfer, exact MP3 bytes over HTTP, simulated decoder
  events, natural two-track advancement, old-event rejection, pause/resume/stop,
  token revocation and Stop before transaction commit.
- Targeted Base Lua integration, playback and backlog-drain regressions pass.
  The earlier full 132-test run had 124 passes, seven failures and one skip;
  backlog-drain subsequently passed. Executable-identity and internal-file
  maintenance failures reproduce in targeted runs. Deployment rollback/recovery
  failures have not yet been resolved. No complete clean regression pass is claimed.

No physical player has been deployed or audibly tested in this continuation.
The renderer test's simulated backend and the playback harness's simulated decoder
are separate from physical hardware validation. Package overlay staging has been
implemented and both target overlays stage with verified checksums; final license
audit, complete installer qualification and hardware
acceptance remain outstanding.

## Historical foundation results

Current local candidate artifacts are under `build/releases/upnp-0.2.0/`:

| Installer candidate | Bytes | SHA-256 |
| --- | ---: | --- |
| StandaloneBase-0.7.3-UPnP-0.2.0-baby-candidate.zip | 3,698,313 | `7523b20222e10cfdc501ffd16b46c66f252ee740efaa6b2ee132afa3ba6b6100` |
| StandaloneBase-0.7.3-UPnP-0.2.0-fab4-candidate.zip | 3,595,898 | `f7ccf986b638f663f1964163b71667b3bddd12a6c33cf0f5bd6111bb8733aa0c` |

The directory also contains a qualification manifest and source archives for
Standalone Base, sbupnp and pupnp. Base production binary source is commit
`186686b`; subsequent test-only additions and notice staging are recorded in
Base commits `7775c8c` and `7701d8e`. UPnP production code is in `938ad82`,
with full-chain tests and documentation in `055078d`. No public publishing or
physical activation was performed. Complete Base licensing/corresponding-source
review and the failing maintenance deployment gates remain release blockers.

### Original diagnostic results (2026-11-14)

Date: 2026-11-14. These results apply to the development diagnostic and core,
not to a finished My Music/renderer product.

## New source

| Check | Observed result |
| --- | --- |
| Host build, GCC 13.3.0 | Pass, own source uses `-Wall -Wextra -Wpedantic -Werror` |
| Host CTest | 1 core suite passed |
| Baby static build, GCC 8.4.0 | Pass |
| Fab4 static build, GCC 8.4.0 | Pass |
| ELF interpreter/shared dependencies | Neither present in Baby/Fab4 executables |
| QEMU `-cpu arm926` | Version execution and cross-built core suite pass |
| QEMU `-cpu arm1136` | Version execution and cross-built core suite pass |
| AddressSanitizer + UndefinedBehaviorSanitizer | Own core and test source pass; linked pupnp archives not instrumented |
| Independent Python SSDP/HTTP/SOAP fixture | Pass; actual discovery, description and BrowseDirectChildren exchange |
| Short discovery outside fixture in WSL | Empty registry; no real MediaServer established |

The core suite covers private numeric HTTP origins, relative URLs, prohibited
schemes, loopback/public addresses, selected management ports, credentials,
header injection, UDN selection, service URL resolution, UTF-8, malformed XML,
DTD/entity and deep-nesting rejection, bounded DIDL pages, opaque IDs, container
searchability, MP3 resource selection over an unsupported FLAC alternative,
duration, exact capability tokens, search-grammar escaping, registry deduplication,
address replacement and expiry.

The independent fixture advertises `Fixture NAS` with a real standard SSDP
response, serves a description, verifies the SOAP action and paging arguments,
and returns a custom container ID `server/opaque&id` and a compatible MP3 item.
This checks that discovery and browsing use the server's hierarchy rather than
synthetic categories. No audio data is served or requested.

Reproduce with `sh scripts/build.sh all` and `sh scripts/verify-host.sh` in WSL.
The network fixture requires a routed RFC1918 interface and local multicast.

## Existing ecosystem checks

- Existing Base `build-wp4a6` CTest build: **107 passed, 11 failed, 1 skipped**
  out of 119. The build was not rebuilt; this checks the available existing
  artifacts and is not a newly qualified Base release.
- Spotify Connect: **32 pytest tests passed** using Windows Python.
  WSL Python lacked pytest; the Windows environment supplied it.
- Standalone Radio: **29 Lua suites passed** through Windows Python/Lupa using
  `python test/run-tests.py`.

Base failures:

```text
storage
extended_storage
storage_firmware_recovery
music_playback_ownership
music_backlog_drain
maintenance_executable_identity
maintenance_internal_files
maintenance_deployment_rollback
maintenance_music_deployment_rollback
maintenance_music_recovery
applet_only_deployment
```

`music_idle_fence` was skipped. Logs are retained in ignored
`.local/base-regression.log`. Several failures are storage/deployment fixture
assertions or restore refusals; music backlog draining also failed its expected
response assertion. Root causes were not established or fixed. Reference
repository source was unchanged, so these are baseline observations, not proof
of an UPnP-induced regression and not grounds to claim the baseline is healthy.

## Measurements and limits

The final static executable sizes are **321088 bytes (Baby)** and
**289112 bytes (Fab4)**, recorded by each target's
`build/<target>/manifest.txt`; these binaries are not stripped. No DLL/so
runtime is required for the cross executables. This does not remove the need
to distribute static runtime notices with any future package.

A separate 3-second host discovery measurement observed maximum RSS **2176 KiB**,
elapsed **3.21 seconds**, and user/system CPU time **0.00/0.00 seconds** at the
measurement tool's precision. This is one host observation without servers,
not a target-device, sustained-idle, browsing, playback or renderer benchmark.
The tool's zero socket-message counters are not a network traffic measurement.
Private output is `.local/host-resources.txt`.

No physical Baby/Fab4 run, My Music UI capture, actual server library Search,
MP3 playback, FLAC qualification, transport/queue verification, renderer test,
ownership handoff, standby test, audible confirmation or hardware soak occurred
for this UPnP implementation. Those features are predominantly unimplemented,
as listed in `STANDALONE_UPNP_IMPLEMENTATION.md`.

No release installer or public announcement was produced. Build executables
are development artifacts only; nothing was deployed or published.
