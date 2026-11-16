# UPnP/DLNA merge review for 0.7.5

Implementation follow-up: see [0.7.5 delivery](RELEASE_0.7.5.md). The gaps below describe the pre-integration audit, not the current software candidate.

Review date: 16 November 2026. This is an audit of the current working tree, not a release or device deployment.

## Decision

The daemon import is complete, but the full finalized UPnP product has not been merged. The current optional integration can be built for review; it is not yet the complete 0.7.5 implementation or a qualified release for the Community Firmware 9.0.2 Baby.

## Compared sources

- Source project: `C:/Projects/SBupnpdlna`, clean master at `75c9348712fc761728bdb6460d29424f74ae1d5c`.
- Active Base checkout: master at `7701d8e4eaaae03ed825f37e62c3e0c1c17fc905`, plus existing uncommitted UPnP, VPN, HTTPS and test changes.
- Later Base integration: `upnp-rc-finalization`, commit `db4c56f451014d8bef43c9ad766b6ecd70a951bf`, worktree `C:/Projects/SBupnpdlna/.local/rc-finalization`.
- Evidence and fresh logs: ignored `.local/upnp075/`. Existing import qualification remains in `.local/upnp-integration/`.

All eight imported daemon C/header/include files match source master byte for byte: core.c/h, daemon.inc, main.c, renderer.inc, rpc.c/h and sdk_guard.c. Base's shared IPC remains the single implementation. Current build integration generates renderer descriptions, pins pupnp/ixml, and packages notices without requiring the source project at runtime.

The existing UPNP.md explicitly excluded later Base queue/playlist and installer work. This explains the incomplete final integration; it is not an unexplained daemon copy failure. The source project's finalization report points to the separate Base branch, so checking only the source project's daemon directory misses required changes.

## Missing functionality and release work

| Area | Current checkout | Required before full 0.7.5 acceptance |
| --- | --- | --- |
| Original Jive playlist | Still uses the earlier status page; rejects `useContextMenu:` and has no `sb_playback_playlist_page` implementation | Port the finalized original Jive playlist contract, stable entry/context/revision actions, stale-provider checks and original DB/137-entry qualification |
| Transactional installation | InstallerTransaction.lua and its tests are absent; InstallerCompat.lua remains the older master implementation | Integrate the final installer transaction, native-image inode preservation, rollback/uninstall/recovery, native ZIP checks and deployment guard |
| Graceful runtime shutdown | RC webserver stop/resource cleanup changes are absent | Port and test the runtime shutdown hooks together with installation/lifecycle integration, retaining current HTTPS/VPN behavior |
| Community Firmware 9.0.2 | Applet requires exactly `7.7.3 r16676` to activate network Base/web binaries | Qualify the existing decoder/ownership fence for the intended firmware, then admit that exact profile; do not simply remove the safety gate |
| Release versions | Native Base, menu, config, ZIP names and generated XML/manifest still report 0.7.3 | Set a coherent 0.7.5 release profile and regenerate hashes/catalog metadata after final binaries are built |
| Release contents | UPnP defaults OFF; standard packages omit the daemon/network runtime | Explicitly select `SB_PACKAGE_UPNP=1` for the intended release, or define a deliberate release profile |
| Notices/source inventory | Later RC packaging/source-export notices are not all integrated; prior export assumes wolfSSL 5.8.2 | Refresh notices, consumed dependency hashes and matching sources for current portal wolfSSL 5.9.4 and all actual shipped components; retain the documented Baby module matching-source hold |
| Source control | Daemon, UPnP build script and integration documentation are still untracked, with related build/UI edits unstaged | Review and commit the complete intended source set before claiming reproducibility from a clean 0.7.5 checkout |
| Physical acceptance | Previous UPnP reports use simulated decoder/renderer events | Validate actual NAS discovery/browsing, original Jive playlist, renderer controls, ownership transitions, audible playback and installer recovery on Baby/Touch |

The firmware gate is functional: on the existing 9.0.2 Radio, presence of sbupnp alone does not activate `sbbase-network` / `sbwebserver-network`. The daemon may start while Base falls back to standard services. A successful host build therefore does not establish usable playback on that Radio.

The RC work spans 29 Base files. Several overlap the active HTTPS/VPN work (main applet, web main/server, packaging, metadata and tests). Port the relevant changes rather than copying the old worktree over this checkout. Preserve the current TLS listener, secure pairing/session classes, credential transport policy, VPN process/assets, entropy retry and banner fix. Nothing in this audit authorizes committing, publishing, rebooting or deploying UPnP to the player.

## Existing feature scope

The imported daemon supplies MediaServer discovery, ContentDirectory browsing/search/sort, bounded paging and MP3 resource selection, plus an optional renderer disabled by default. Network tracks use Base's shared queue, ownership adapter and SqueezePlay audio engine. Supported resources are numeric private/link-local IPv4 HTTP MP3 with audio MIME type and known Content-Length. Hostname/HTTPS/public URLs, redirects, chunked streams and other codecs are outside this implementation's current supported scope. These are existing deliberate limits, not files lost during the import.

## Fresh verification

Host combined build: passed with UPnP, Music runtime, proxy, VPN and HTTPS enabled. The freshly cross-built Baby network webserver also passed the complete ARM926-emulated HTTPS/VPN socket suite with a synthetic Unix resource endpoint: TLS 1.2/1.3, P-256 group policy, PIN/cookie scope, Host/Origin, forwarding, encrypted credential CRUD and identity recovery. The first attempt without a resource endpoint correctly waited for production rendezvous and exceeded the generic TLS fixture startup deadline; supplying the fixture endpoint addressed that test-harness mismatch without changing production code. This is transport acceptance, not firmware/audio acceptance. The full configured isolated host suite passed **148/148**, zero failures/skips, in **463.75 seconds**. After configuring the real sibling source paths and enabling firmware-adapter checks, the three additional adapter/recovery checks passed (two in 0.16 seconds, one in 0.41 seconds): **151 distinct tests passed in total**. The initial fresh mirror configuration did not discover those external source paths; this was a validation setup issue, not an omitted production module.

Fresh Baby ARMv5TE and Fab4 ARMv6 network runtimes and daemons built successfully. Both daemons passed version and core checks under ARM926/ARM1136 QEMU. Both optional packages passed CRC, source-byte checks, flat/no-runtime-secret inventory, executable modes and static soft-float EABI/CPU checks for all eleven executables (eight standard and three network). Network component manifests, seven daemon/SDK notices, generated XML SHA-1/target/version, current secure banner and cache version were checked. These are private **0.7.3-named audit packages**, not 0.7.5 releases or published artifacts. They show current worktree buildability and do not close the missing RC functionality or firmware gates. No physical device was contacted during this audit.

## Recommended integration sequence

1. Port the playlist and installer/lifecycle changes from the finalized Base branch, including later corrections and tests; preserve the active VPN/HTTPS implementation.
2. Admit the intended firmware only after decoder/ownership validation; keep unsupported profiles in safe fallback.
3. Refresh the complete release/source/notice inventory and resolve distribution holds.
4. Set 0.7.5 metadata, build both full optional packages, verify exact contents/ABI/protocols, and rerun regression plus original Jive/installer integration.
5. Perform controlled physical acceptance and publish only after separate release authorization.

### Private audit packages

Stored under ignored `.local/upnp075/packages/`.

| Target | Bytes | Members | SHA-256 |
| --- | ---: | ---: | --- |
| baby | 4419081 | 76 | `37cdf88f441702db68dde2afa9f1ed074752392289f17a78d279e45faf399f69` |
| fab4 | 4315315 | 74 | `fbb1a746b9bcaff0b377b5c9c854fd2f1a2807fe5596f9ca2aa57ef0245201f7` |

Only review documentation was changed in this audit. Production implementation, the source project, staged index, public packages/catalogs and device state were left unchanged. Current UPnP source remains uncommitted as described above.
