# WP4 evidence and provenance

Evidence for the frozen implementation and disposable-fixture validation. Companion reports:

- [Implementation report](STANDALONE_MUSIC_WP4_REPORT.md)
- [Queue and selection](STANDALONE_MUSIC_WP4_QUEUE.md)
- [HTTP streaming](STANDALONE_MUSIC_WP4_HTTP_STREAMING.md)
- [SlimProto and status](STANDALONE_MUSIC_WP4_SLIMPROTO.md)
- [Ownership and app gates](STANDALONE_MUSIC_WP4_OWNERSHIP.md)
- [Tests](STANDALONE_MUSIC_WP4_TEST_RESULTS.md)
- [Performance](STANDALONE_MUSIC_WP4_PERFORMANCE.md)

New code is native C plus an inactive Lua adapter seam. No LMS/TinySC/Perl runtime, new decoder, media mount/eject, boot-script change, extra server, physical deployment, commit/push/tag or unrelated refactor is part of WP4. The working tree already contained the earlier uncommitted work packages; their changes were retained.

The read-only prior reports, current application/firmware references, fixture provenance, final source snapshot, binary manifests and raw validation logs are indexed by SHA-256 in [the source index](STANDALONE_MUSIC_WP4_SOURCE_INDEX.json). Request/response examples are observed host fixtures with explicitly mocked firmware events, not captured device traffic. Random context/token values are fixture-incarnation data and their authority ends when the private processes exit.

Private raw work files live under `.local/wp4/`; portable source/measurement/example ledgers accompany these reports. No physical device, production service or existing user library was modified. Stop after WP4; no device activation or subsequent package has begun.

## Portable artifacts

- [Measurements](STANDALONE_MUSIC_WP4_MEASUREMENTS.json): host1k/10k/50k raw CPU/RSS/latency samples, encoded HTTP transfer, sanitizer and QEMU observations.
- [Observed examples](STANDALONE_MUSIC_WP4_EXAMPLES.json): bounded original Comet commands/replies, serialized SlimProto starts and explicitly mocked STAT events. Tokens are expired disposable capabilities.
- [Source index](STANDALONE_MUSIC_WP4_SOURCE_INDEX.json): SHA-256 hashes, reference locations, prior report hashes, binary manifests and validation transcripts. `.local/` raw logs are private work artifacts, so relevant observations and transcripts are embedded in the portable ledgers.

## Reproduction

Configure/build the existing Linux `build-dual-tests` setup with proxy tests excluded only when libcurl development files are unavailable; run its CTest suite sequentially. New target names are `test_music_queue`, `test_music_http_policy`, `test_music_stream`, `test_music_load`, `test_music_playback`, `sbbase_music_test` and `sbwebserver_music_test`. The Lua test is `tests/music/test_ownership_lua.lua`.

For cross-builds run `sh scripts/build-playback-arm.sh all` with the existing ARM SDK and image/zlib prefix. Final recompilation reused unchanged WP1/WP2/WP6a static archives from the successful normal dual build; seven current WP4 executables per platform were rebuilt, stripped and checked for static linkage. Manifests record flags/compiler and binary hashes. QEMU uses `-cpu arm926` for Baby and `-cpu arm1136` for Fab4. The five native tests run directly; HTTP scripts take wrapper executable paths for Base/web and the host scanner/helper solely for fixture preparation.

`tests/music/test_playback_http.py` accepts `benchmark 1000|10000|50000 <test_music_browse>` after its four normal executable arguments. `SB_WP4_EXAMPLES=<output.json>` captures bounded examples in the default flow. Fixture media are deterministic synthetic data produced locally by `wp2_fixtures.py` and existing native fixture builders; no live music downloads are required. Normal queue/load/stream tests use disposable `/tmp` and validated `/dev/shm` fixture roots.

ASan/UBSan compilation uses `-fsanitize=address,undefined -fno-omit-frame-pointer`, existing archives as explicitly qualified in the test report, `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1`. Executable-only wrappers preload libasan plus the existing partial-send shim; Python and helper preparation are not incorrectly instrumented by that preload.

No new dependency was added. Existing musl, pthreads, SQLite and zlib remain the native build dependencies; Python/Lua/QEMU are development-test tools. Existing proxy/libcurl availability is documented separately. Normal feature gate remains OFF and refuses activation; test-only code does not supply a production ownership bridge.
