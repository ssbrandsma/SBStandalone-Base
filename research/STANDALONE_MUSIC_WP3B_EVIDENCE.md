# WP3b evidence and source index

Evidence completed2026-11-03. This checkout contains uncommitted earlier WP0/WP1/WP2/WP6a/WP3a work as well as WP3b; those changes were retained. No commit, push, tag, release package, deployment, device activation or physical-device test was performed.

## Portable artifacts

- [Source/reference/build hash index](STANDALONE_MUSIC_WP3B_SOURCE_INDEX.json):30 source/build/test entries,14 reference files,25 host/ARM artifacts and validation logs. All35 preimplementation research report hashes still match the input snapshot.
- [Measurement ledger](STANDALONE_MUSIC_WP3B_MEASUREMENTS.json):final successful1k/10k/50k measurements, focused CTest outputs, sanitizer and both QEMU outputs.
- [Actual request/response examples](STANDALONE_MUSIC_WP3B_EXAMPLES.json):observed native host fixture menu, artists, albums, tracks, genres, folders, info and offline response;1000 generated tracks. Not device traffic.
- Source-derived golden: `tests/music/fixtures/browse.json`; source pins and all four menu objects are asserted by the HTTP tests.

## Source and reuse map

| Implementation | Purpose |
| --- | --- |
| native/sbmusic/music_async.h/.c | bounded coordinator, notification FD, job cancellation/deadlines |
| native/sbmusic/music_library.h/.c | existing worker/broker reuse, scoped browse/summary projection, fair read turns |
| native/sbbase/include/music_browse.h; src/music_browse.c | native command-to-page API, serialization, generations/revisions, menu nodes |
| native/sbbase/src/music_browse_http.inc | separately compiled test-only Comet adapter, bounded batches/subscriptions and backpressure |
| native/sbbase/src/main.c | guarded poll/connection/startup/shutdown hooks |
| native/sbbase/include/nowplaying.h; src/nowplaying.c | reuse existing JSON tokenizer/escaping via small helpers |
| native/sbbase/CMakeLists.txt; native/sbmusic/CMakeLists.txt | non-installed browse test target and reusable libraries |
| tests/music/test_browse.c; test_browse*.py; benchmark_browse.py | generated corpus, SQL oracle, lifecycle/concurrency/security and performance |
| scripts/build-browse-arm.sh | both static ARM test builds and manifests |

Original WP0 DTOs/feature gating and WP1 version3 schema remain unchanged. Existing WP2 scanner and WP6a media contracts are reused; no storage discovery or scanner startup enters Base. Existing WP3a output queue and ownership controller are retained. No external runtime parser, network fetch, LMS, TinySC or Perl is required. SQLite source/version/provenance remain as recorded in WP1; no vendor files were changed. Packaging still enumerates the six existing product executables; browse test binaries are not installed or packaged.

Reference provenance is detailed in [protocol](STANDALONE_MUSIC_WP3B_PROTOCOL.md). Collected Jive/LMS files are read-only research inputs. SlimMenus is separately [pinned upstream source](https://raw.githubusercontent.com/LMS-Community/squeezeplay/cca1bba4abeb3fef2b7c4a48a4e4da8fc255666d/src/squeezeplay/share/applets/SlimMenus/SlimMenusApplet.lua), SHA256 `6e2db5b9ab17c14fe2702bda9407f226d3a49f1438241a242b4bd54a3b9e63a8`; it is absent from the collected firmware subset. No claim of physical UI compatibility follows from this source trace.

## Local raw evidence

Under `.local/wp3b/`: `inputs.json`, `baseline-tests.log`, `final-tests.log`, `build-final.log`, `browse-arm.log`, `full-arm.log`, `sanitizers-native.log`, `sanitizers-http.log`, `qemu.log`, `elf-check.log`, `proxy-probe.log`, `performance-final.jsonl`, and `run_*` scripts. The private files may not be included in a source distribution; portable ledgers above retain their hashes and selected complete outputs. Earlier failed benchmark logs are retained locally as diagnostics and excluded from successful results.

ARM manifests: `build-arm/{baby,fab4}/browse/manifest.txt` and normal `build-manifest.txt`. GCC8.4 musl SDK; static ELF32 ARM EABI5, Baby v5TEJ/Fab4 v6 attributes, no interpreter/dynamic dependencies. Some SDK-linked object attributes report Thumb-2 even though frontend compilation uses -marm; functional QEMU arm926/arm1136 runs pass. This does not substitute for testing original hardware/kernel.

## Final review

All prior report hashes unchanged; no unrelated refactoring was introduced by WP3b. `git diff --check` passes. New WP3b text artifacts use UTF-8 without BOM. Source hashes, JSON decoding, local documentation links, balanced fences and absence of debug tracing are checked at handoff. Baseline76/76 -> final82/82; focused6/6, ASan/UBSan, static builds and QEMU all pass within documented coverage. Missing host libcurl development files still exclude proxy-specific CMake tests. Existing image-renderer warnings are unrelated.

Standalone Music is disabled by default; normal Base creates no library, scanner or browse worker/menu. No existing service or physical library was changed. WP4 and device activation require a separate explicit request.
