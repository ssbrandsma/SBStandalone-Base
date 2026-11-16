# UPnP/DLNA network music

The 0.7.5 release profile includes `sbupnp` 0.3.1, the separated native network playback runtime, and the original Jive My Music/playlist integration. VPN and the secure HTTPS portal remain in the same Base package. The optional MediaRenderer stays disabled until explicitly enabled under Standalone Base > Network Playback.

Current delivery and qualification status: [0.7.5](RELEASE_0.7.5.md). The earlier [merge audit](UPNP_MERGE_REVIEW_075.md) records the gaps that led to this integration. Historical source-project research is retained under [research/upnp-0.7.4](research/upnp-0.7.4/README.md).

## Build and verification

Host prerequisites include CMake, Git, C compiler, OpenSSL/zlib/raster codec development headers, Python with Lupa, Lua and the existing Node/jsdom frontend test dependencies. Enable sibling adapter fixtures with their actual source paths when those repositories are available.

```sh
cmake -S . -B build-upnp/host -DSB_BUILD_UPNP=ON -DSB_TEST_FIRMWARE_ADAPTERS=ON
cmake --build build-upnp/host -j4
sudo sh scripts/test-release.sh build-upnp/host
sh scripts/build-release.sh all
```

The test wrapper creates a private network namespace and dummy LAN. Do not run network fixtures directly on a real player LAN. The release script uses the existing ARM SDK/static dependencies, builds Baby and Fab4, and verifies the local ZIPs. It never deploys or publishes. `SB_RELEASE_OUTPUT` selects a private output directory. `SB_PACKAGE_UPNP=0` is an explicit diagnostic Base-only build; it is not a complete 0.7.5 network-music release.

pupnp/ixml 22.2.2 remains pinned to `fac0494691434c35243131f66a0b27ea0c29f700`. Dependency edits or a different revision fail configuration without deleting changes. Renderer descriptions are generated in the build directory. No SBupnpdlna checkout is needed for the build.

## Playback and firmware profiles

MediaServer discovery, opaque ContentDirectory browsing, capability-based search/sort and bounded direct-container queue actions use the existing shared queue and SqueezePlay decoder. The original playlist supports stable entry/context/revision actions, pagination, Play/Remove/Move/Clear, stale-action rejection and natural next.

Admitted profiles are Baby `7.7.3 r16676`, Baby Community Firmware `9.0.2 r17160`, and Fab4 `7.7.3 r16676` with the existing TinySC arbitration profile. Admission does not bypass runtime decoder/ownership checks. Unsupported versions use safe Base fallback. The installed runtime manifest must also match 0.7.5 and its IPC/queue contract; a stale or incomplete bootstrap does not activate network playback. Actual 0.7.5 hardware acceptance is pending.

The daemon selects an up, multicast-capable private IPv4 LAN interface, excluding loopback, point-to-point and common tunnel interfaces. VPN default-route changes do not select the tunnel as the discovery LAN. A retired/down bound LAN causes supervised rediscovery. Multiple independent LANs can require the daemon's explicit `--interface` option.

## Supported resource scope

Network playback currently accepts numeric private/link-local IPv4 HTTP MP3 resources, with audio MIME type and known Content-Length. Hostname/HTTPS/public URLs, redirects, chunked audio and other network codecs remain unsupported. Pages contain at most 50 objects, the UI retains at most 200, and the shared native queue holds at most 256 tracks. Direct container actions pin UpdateID across pages and do not recursively expand nested containers. Renderer seek and single-resource Next/Previous return standard unsupported faults.

Original Jive Now Playing artwork/navigation are retained. Spotify/Qobuz/Radio expose genuine current metadata and their available capabilities; no foreign queue is invented. Native queue actions cannot operate a retired provider.

## Installation

Use the new transactional stock-installer path after a backed-up Lua bootstrap of an older Base installation. Full SSH overwrite of a network-runtime ZIP is refused. Staging verifies checksums, Lua, target, native ELF/IPC contracts and executable modes before committing the tree. Persistent credentials, TLS identity, media and renderer preferences remain outside the transaction. Activation/recovery and watchdog/UnionFS behavior still require controlled hardware acceptance. Do not restart Jive or reboot without explicit owner approval.

## Earlier import verification (historical)

The following records the earlier limited daemon import, before the 0.7.5 changes above. Its excluded playlist/installer work has now been ported; its old package hashes and counts are historical.

## Integration verification (2026-11-16)

Before editing, both worktrees, staged/unstaged changes and relevant untracked
files were inventoried. No applicable AGENTS.md was found. Source was clean on
master at 75c9348; target master was at 7701d8e, with no staged changes and
existing VPN/TLS/test work (37 modified tracked and 42 untracked files at the
saved inventory). References 938ad82/75c9348 and 186686b/7775c8c/7701d8e were
available. NetworkMusic, common UPnP IPC and upnp_http.inc already matched the
Base integration commits and were retained. Between the source references the
daemon core/guards are unchanged; later RC Base/playlist/installer work is not
part of this import.

Additional concurrent VPN/TLS edits appeared during this task. They were not
reverted or staged. A single atomic state of all concurrent user work cannot be
verified; the ARM/package check therefore used a frozen separate Base checkout
under `.local/upnp-integration/checkout`, with the current dirty worktree copied
as source input and no SBupnpdlna dependency. Ignored source-repo caches and
other linked worktrees were not treated as import inputs or exhaustively audited.

Actual checks:

- Normal CMake host build with `SB_BUILD_UPNP=ON`: 143/143 CTest tests passed,
  zero skips, 382.90 seconds. This includes the three targeted UPnP tests and
  existing Base/VPN/TLS tests. All network fixtures ran inside a private dummy
  LAN namespace; firmware decoder/idle events and renderer backend are simulated.
  This first profile used `BUILD_SBPROXY=OFF`. The normal proxy-enabled profile
  was then built and its six additional proxy/HLS tests were exercised: five
  passed directly; artwork_proxy_streaming initially timed out on its negative
  `no-artwork.invalid` DNS lookup because the namespace has no reachable DNS
  resolver. Its unchanged test passed when repeated with `RES_OPTIONS=attempts:1`.
  No proxy code, timeout assertion or fixture was weakened or modified.
- Default CMake configuration: UPnP and the optional Music runtime are OFF.
- Normal ARM build entry point with `SB_PACKAGE_UPNP=1`: Baby and Fab4 built
  from the separate Base checkout using existing SDK/image/TLS prefixes.
- Normal package entry point: both targets with the overlay and both without.
  Optional ZIPs passed CRC, static ARM EABI/soft-float, executable-mode,
  binary-byte/hash/manifest, XML checksum and seven component-license checks.
  All 59 Baby / 57 Fab4 standard members remain byte- and mode-identical;
  the overlay adds exactly 15 members. All four packages are local test outputs,
  not published releases.
- ARM926/ARM1136 QEMU: daemon version and core tests passed on both targets.
- Renderer generator: deterministic output, identical description arrays to
  source 75c9348. Dirty pinned-dependency test: configuration refused the edit
  and left its marker intact; the test marker was then removed.
- Source HEAD/status/content and the target index were rechecked: source remains
  unchanged and no changes were staged. All eight imported daemon files match
  the source bytes. Base's original IPC/UI gateway files were not replaced.

Evidence, inventory, package hashes and an integration-only review patch are in
`.local/upnp-integration/`. The separate patch excludes unrelated concurrent
VPN/TLS changes. No device was contacted or installed; hardware, audible output
and Linux 2.6 compatibility were not tested because physical installation was
explicitly excluded. No publication or new protocol/codec/UI setting was added.

The initial host attempt in CodexSBUpnpRC lacked OpenSSL development headers
required by the pre-existing TLS work. Verification used the existing
SBVPNValidation environment instead; Lupa was installed only into this target
repo's ignored test-support directory to avoid an idle-fence skip.
