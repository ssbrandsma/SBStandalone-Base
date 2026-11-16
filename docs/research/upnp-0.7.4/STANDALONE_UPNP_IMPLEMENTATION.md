# Implementation and repository inspection

Status on 2026-11-15: production integration implemented and under qualification.
The changes now span this repository and Standalone Base's native queue/resource
bridge, authenticated async gateway, Jive My Music integration and supervision.
Network-only playback requires no SQLite library or local media root. The optional
MediaRenderer uses the same Base backend and remains disabled by default.

The SDK was upgraded to pupnp 22.2.2 after upstream security review. Host protocol,
sanitizer and native streaming tests pass; both ARM service builds pass QEMU core
tests. See `STANDALONE_UPNP_TEST_RESULTS.md` for exact evidence and limitations.
Remaining work includes full-chain queue/controller acceptance, complete deployment
regressions and distribution audit, then available physical hardware qualification.
No physical device has been contacted, changed, restarted or audibly tested.

The following repository inspection records the original foundation and its
then-outstanding work; it is historical context, not the current feature inventory.

## Existing repositories

| Repository | Inspected revision | Findings |
| --- | --- | --- |
| `C:/Projects/SBStandaloneBase/StandaloneBase` | `9f9c32e483c6aa45efb95b8182b1e67f47ec00f4` | Native ownership, queue, My Music, resource IPC, artwork, supervision and cross builds |
| `C:/Projects/SBSpotifyConnect` | `aa719b67711f257b13490baf5e81369aca8040d3` | Stream-only native producer, compressed Ogg bridge, Linux 2.6 wrappers, Base Now Playing publication |
| `C:/Projects/squeezebox` | `c389a7dfbb4fee8ed14e834e2bbd9f6f24a954da` | Existing Radio applet, original decoder integration, presets and Stop/reconnect behavior |

The Base repository root is the nested `StandaloneBase` directory. The requested
`SBupnpdlna` workspace initially contained no files or Git repository; it was
initialized for this work. No `AGENTS.md` files were found in the inspected
project trees. Spotify had an existing untracked `screenshots/` directory;
it was left untouched. No source edits were made in the three reference trees.

Relevant Base source and contracts:

- `native/common/music_contracts.h`: ownership enum currently contains None,
  Radio, Spotify, Qobuz and Music. Queue identities use local numeric track IDs.
- `native/sbbase/include/ownership_bridge.h` and
  `native/sbbase/src/ownership_bridge.c`: five adapter slots, idle admission,
  correlated generations and physical decoder proof. Adding UPnP requires
  updating all related bounds, masks and Jive contracts together.
- `native/sbbase/include/slim_controller.h`: start accepts an opaque loopback
  token, codec and port; it deliberately does not accept caller-provided URLs.
- `native/common/music_resource_ipc.h`: private SOCK_SEQPACKET channel with
  descriptor transfer and REGISTER/RESERVE/ACTIVATE/REVOKE/RESET. The existing
  136-byte wire format represents a local resource, not a network stream.
- `native/sbbase/src/music_playback.c`, `music_load.c`, `music_queue.c` and
  `music_file.c`: existing physical playback, bounded queue (256 entries),
  async SQLite load and local resource lifecycle. Do not fake local IDs for
  remote objects or issue parallel decoder commands outside this path.
- `native/sbbase/src/music_browse.c` and `main.c`: original Jive My Music
  responses and asynchronous browse machinery. Network sources need to be
  available without waiting for a mounted local SQLite library.
- `applet/MusicProduction.lua`, `NativeMusicOwnership.lua`,
  `OwnershipDecoder.lua`, `OwnershipRuntime.lua` and `PlaybackFenceV6.lua`:
  Jive-side admission and actual decoder fencing, including other producers.
  Current V6 source is build 14 and contains temporary diagnostic hooks.
- Existing artwork and Now Playing should remain the sole presentation path.
  A metadata lease does not authorize decoder access.

The WP4a.6 report is explicitly a draft with unresolved Pause/Resume and switch
verification; its historical 46/50 test count does not describe the current
119-test existing build. The V6 source contains changes newer than that report.
Do not infer current physical qualification from those older summaries.

## Toolchain located and exercised

Actual compiler:

```text
C:/Projects/SBHttpsProxy/build/toolchains/
  armv5-eabi--musl--stable-2020.02-2/bin/arm-buildroot-linux-musleabi-gcc
Buildroot 2020.02-00011-g7ea8a52, GCC 8.4.0
```

This is a Linux ELF compiler and was used through WSL Ubuntu-24.04.
Base's `scripts/build-arm.sh` and `scripts/build-music-arm.sh` already select it.
The CMake 3.31.8 Linux executable also exists under that toolchain directory.
Host GCC 13.3.0 and `qemu-arm` are available in WSL.

Baby flags: `-marm -march=armv5te -mtune=arm926ej-s -mfloat-abi=soft`.
Fab4 flags: `-marm -march=armv6 -mtune=arm1136jf-s -mfloat-abi=soft`.
The builds use static musl, size optimization and section garbage collection.
Spotify's separate Rust build uses a shared ARMv5TE baseline and Linux 2.6
wrappers. Those wrappers are not blindly copied: their source is explicitly
labelled PoC and warns about CLOEXEC fallback races.

No OS, firmware, filesystem or local library upgrade is needed for this build.
QEMU user-mode executes target instructions with the host kernel; it does not
qualify Linux 2.6 syscalls, actual firmware ABI behavior or physical audio.

## Protocol library

Selected for development: upstream pupnp **22.1.6**, revision
`8f59b9f3a3e778abc994b28ebcad8621a8f85a51`.
The exact release was fetched from upstream, rather than assuming the older
1.14 branch was necessary. Its C sources compile on GCC 8.4.0 without patches.
The configured static library retains client, device, SSDP, SOAP and GENA APIs;
IPv6, SSL, sample binaries, script support and dependency tests are disabled.

This avoids the LMS runtime, bridge-specific behavior, GStreamer and GLib.
The SDK dependency already has an XML parser; no additional XML dependency was
introduced. The existing SDK was sufficient, so no alternative cross compiler
or old libupnp version was required. Compilation is established; production
security and firmware runtime qualification remain open.

See `THIRD_PARTY_NOTICES.md` for licensing and reference-code boundaries.

## Implemented source

`native/sbupnp/src/core.c`:

- Numeric RFC1918 HTTP URL validation and relative URL resolution; same-address
  service/resource policy, selected privileged port rejection, no redirects.
- 256 KiB XML cap, UTF-8 validation, declaration/DTD/entity rejection,
  32-level pre-parse depth cap and 8192-element cap.
- Device selection by matching UDN, MediaServer and ContentDirectory versions,
  URLBase handling, friendly names and relative service URLs.
- Registry limited to 32 servers, stable UDN keys, TTL expiry and byebye removal;
  address/control changes replace a server generation rather than duplicating it.
- DIDL pages limited to 50 objects with opaque IDs, generic containers,
  searchable attribute, available title/artist/album/genre/artwork metadata,
  HTTP MP3 resource selection from protocolInfo, duration and resource properties.
- Capability token checking and escaped server-search expression construction.
  This is a helper; actual Search and capability RPCs are not wired yet.

`native/sbupnp/src/main.c` is a bounded-duration diagnostic, not production IPC:

- Uses pupnp's native M-SEARCH and alive/byebye callbacks.
- Callback only queues/coalesces up to 32 notices under a mutex. Description
  downloads and registry mutation run outside the callback.
- Requires the advertised address to match the SSDP sender; byebye must match
  the known server address.
- Fetches bounded descriptions with bounded individual HTTP operations.
- Calls BrowseDirectChildren with an explicit offset, count 50 and no sort.
- Emits JSON containing discovered servers or a server-defined browse page.
- Does not open audio/artwork resources, issue decoder commands or acquire owners.

## Remaining implementation (not verification limitations)

The following are absent, rather than merely awaiting a hardware test:

1. Production supervised lifecycle, authenticated Base IPC, cancellable client
   requests, session/generation handling and reconnect behavior.
2. My Music source registry and original Jive menu integration/localization.
3. BrowseMetadata, actual search/sort capability RPCs, Search RPC and UI,
   consistent pagination metadata/UpdateID validation and capability-driven UI.
4. Shared remote-resource representation, Base ownership contract extension,
   qualified streaming proxy/resource lifecycle and physical playback adapter.
5. Remote queues, actual transport controls, Now Playing, artwork integration,
   natural completion, standby fencing and cross-source handoff tests.
6. Opt-in renderer descriptions, AVTransport, RenderingControl,
   ConnectionManager, GENA and admission policy integration.
7. Broader network interoperability (DNS names, permitted HTTPS, cross-host
   resources, multi-interface reconnects), complete untrusted outer SOAP parsing
   limits, rate limits and a production resource/security audit.
8. Installer/package lifecycle, SDK runtime license audit, upgrade/rollback
   contract, end-user guide and truthful release announcement.

The current diagnostic is intentionally not packaged as a release-ready applet.
There is no physical validation, audible confirmation, controller qualification,
30-minute device soak or performance measurement for playback/renderer modes.
The full brief's acceptance criteria are not met.
