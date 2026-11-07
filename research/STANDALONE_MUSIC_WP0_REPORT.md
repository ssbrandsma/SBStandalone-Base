# Standalone Music WP0 Report

WP0 is complete as a disabled foundation, 2026-11-01. No WP1 library, WP3a playback controller or WP6a storage adapter was begun. No production device/service/settings were modified, no TinySC/Perl runtime was introduced, and no playback/scan/database/indexing/music browsing was implemented.

Deliverables: [contracts](STANDALONE_MUSIC_WP0_CONTRACTS.md), [parser evaluation](STANDALONE_MUSIC_WP0_PARSER_EVALUATION.md), [protocol fixtures](STANDALONE_MUSIC_WP0_PROTOCOL_FIXTURES.md), [test results/baseline procedure](STANDALONE_MUSIC_WP0_TEST_RESULTS.md), [measurements](STANDALONE_MUSIC_WP0_MEASUREMENTS.json), [evidence index](STANDALONE_MUSIC_WP0_EVIDENCE.md), [source ledger](STANDALONE_MUSIC_WP0_SOURCES.json). All seven Phase 0 artifacts were read; current source and executed tests took precedence over earlier classifications.

## Actual implementation

- Added `native/common/music_contracts.h/.c`: bounded identity/DTO/provider types and pure guards for source/media, physical owner/yield, stream epoch/events, queue-update revisions, queries and HTTP resource/range validity. Provider interfaces are declarations only. No registry, queue implementation, generation allocator, decoder controller, SQL, mounts or production IPC exists.
- Added `native/sbbase/include/lms_command.h` and `src/lms_command.c`: fixed-size, allocation-free grammar/argument validation and explicit implemented/unsupported/unknown/malformed/oversized classification. Existing JSON argument decoding is reused. Command names no longer match substrings inside arbitrary arguments/reply strings.
- Changed only the dispatch entry in sbbase and the shared JSON string/argument utility: valid surrogate pairs now decode correctly, request shape is bounded and scalar arguments must be strings/numbers. Existing alarm/sleep/Now Playing handler logic, HTTP/Comet framing and subscription machinery remain in place.
- Reserved CMake `SB_ENABLE_MUSIC` OFF, with ON rejected in WP0; the shared runtime capability is always 0. No local music endpoint/menu/capability is advertised. Normal compile/packaging flow adds only native foundation sources, no parser candidate dependency or new public listener.
- Added deterministic C/Python tests, source-derived protocol fixtures, synthetic boundary tests, generated CC0 media, loopback performance tools and isolated host/ARM parser tools. Existing independent ID3/metadata-wait tests now also run without libcurl development dependencies.

Reused: protocol.c framing, np_lms_arguments/metadata leases/status, existing CometD request/reply/subscription code, existing alarm/installer menus, HLS ID3 parser/image validation, current static toolchain/build verification and CTest. Production sbproxy, artwork/metadata routing, app Lua and decoder/ALSA were not replaced or duplicated. Existing public routes remain the same; deterministic rejection of invalid/unsupported commands is the intentional dispatch behavior change even while the future music feature is off.

## Compatibility and constraints

Metadata ownership is explicitly separate from physical decoder release. The test claims a valid Music metadata lease while the physical-owner guard remains Radio and cannot commit. Stale owner/connection/request IDs, refusals, expired acknowledgements, duplicate EOF/terminal events and removed media generations are rejected by guard tests. No existing app takeover path calls these new guards.

Radio's loopback exclusion, Qobuz's SlimProto disconnect and Spotify's false control capability flags remain future handoff work. The existing typed argument layer now prevents an unknown command containing play/stop/menu/status from accidentally dispatching them. Some legacy envelope/ID extraction and bounded batch-output behavior remain untouched; this is a compatibility layer, not a complete Bayeux/networking rewrite. Future native queue/query commands are explicitly unsupported rather than empty success. Absolute playlist index was never correctly implemented and is not advertised as working.

Private resource interface design uses unpredictable tokens, expiry, source/media/owner/stream generations, validated descriptors, GET/HEAD/ranges, cancellation and revocation. Host SCM_RIGHTS transfer/FD cleanup is proven in a socketpair-only test; target-kernel permissions and Mongoose provider integration are pending. No IPC server or filesystem resource route was added.

`send_data` can still wait two seconds per EAGAIN retry on sbbase's shared loop, potentially longer overall with partial progress. This can delay every control service in that process. Bounded asynchronous output queues are a prerequisite for WP3a; no broad refactor was justified in this foundation. Existing separate web/proxy processes remain independent.

## Validation outcome

Existing pre-change host baseline: **60/60 passed, 156.74 s**. Final available host suite: **67/67 passed, 162.91 s**. Focused music guards/dispatch/wire/FD tests and ASan/UBSan checks passed. Full Baby and Fab4 production cross-builds passed with static EABI/dependency verification; target contract/command executables passed under user-mode QEMU. QEMU is not a physical device test. Build warnings in pre-existing image code were not changed.

Native host full-proxy integration remains pending because libcurl development headers/library are absent; a configure probe records the exact failure. Independent HLS ID3 and metadata-wait tests passed, and normal ARM sbproxy built unchanged. No real Radio/Spotify/Qobuz sound/handoff, USB/SD, firmware/reboot or old-kernel execution was performed. Their device-level behavior is not claimed as regression-tested.

Short host endpoint samples found no FD growth (6 before/after), one sbbase thread, zero sampled CPU ticks during idle and unchanged observed 6400 KiB post-workload RSS. Sampling duration/count differed between baseline and final run, and build scheduling/WSL filesystem noise can affect latency; these are observations, not statistically proven equal performance or device CPU/RAM budgets. Exact values follow below.

## Metadata recommendation

TagLib v1.13.1 was isolated, hash-pinned, cross-built for both targets and exercised against generated MP3 ID3v1/v2.2/v2.3/v2.4, FLAC/Ogg comments and JPEG/PNG covers. Required ordinary-field/property fixtures passed on host and both ARM tools under QEMU. The existing bounded HLS parser was measured as a deliberately smaller subset.

The unrestricted TagLib tool reaches about 19 MiB host peak memory on a valid 1 MiB title tag, exceeding the proposed scanner budget despite good ordinary metadata results. Keep it as a correctness reference. Recommend a bounded native C metadata interface and focused required-format readers, reusing current text/image validation. That full C reader is not implemented or proved here; WP2 must demonstrate equivalent corpus correctness, bounded bytes/heap and maintenance feasibility. A restricted TagLib wrapper remains an alternative only if it passes the same adversarial budget gate and maintained-version/toolchain review. Nothing from the parser spike enters the production build.

## Recommended next package

Proceed to **WP1 only after explicit approval**: small portable SQLite schema/worker/query implementation against synthetic data, using the query/media-generation contracts; no scan, playback or hardware adapter yet. WP3a can subsequently consume the independently tested owner/stream contracts, but first needs a bounded output-queue design and explicit app yield adapters. WP6a can consume the media identity contract once actual mount/identity evidence is collected. Do not enable runtime music or advertise capabilities merely because WP0 passes.

Remaining technical risks: unmeasured Baby memory; old-kernel/file-provider behavior; real physical app handoff; metadata parser pathological inputs; database filesystem/durability policy; incomplete envelope/output backpressure; source-derived fixture/UI type and pagination quirks; missing native-host proxy development dependencies. Each is named in the companion documents with concrete next tests. Phase 0 performance budgets remain targets, not measurements.

## Recorded measurement tables

| Component | Baby before / after bytes | Fab4 before / after bytes |
|---|---:|---:|
| sbbase | 132176 / 136272 | 132176 / 136272 |
| sbwebserver | 291976 / 291976 | 291976 / 291976 |
| sbproxy | 874644 / 874644 | 874644 / 874644 |
| svgpngrenderer | 497800 / 497800 | 497800 / 497800 |
| sb-storage-helper | 66620 / 66620 | 66620 / 66620 |
| sb-storage-updater | 33852 / 33852 | 33852 / 33852 |

Only sbbase grows by 4096 stripped bytes per target. Other component file sizes remain unchanged; hashes/manifests are recorded in the measurement artifact.

| Host endpoint | Baseline p50 / p95 ms (100 requests) | After p50 / p95 ms (200 requests) |
|---|---:|---:|
| health | 0.393 / 0.726 | 0.370 / 0.607 |
| nowplaying | 0.382 / 0.664 | 0.362 / 0.667 |
| comet_date | 0.490 / 0.984 | 0.449 / 1.060 |

| TagLib host fixture (25 iterations) | CPU microseconds/file | VmHWM KiB | Peak live C++ new payload bytes |
|---|---:|---:|---:|
| comments.flac | 996.56 | 1792 | 11459 |
| comments.ogg | 1653.68 | 1792 | 11114 |
| id3v22.mp3 | 740.32 | 1664 | 6020 |
| id3v23.mp3 | 952.52 | 1792 | 6656 |
| id3v24.mp3 | 727.88 | 1792 | 6490 |
| large-tags.mp3 | 15282.08 | 19284 | 15732199 |
