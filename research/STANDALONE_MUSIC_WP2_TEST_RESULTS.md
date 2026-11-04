# WP2 validation results

2026-11-02. All data is disposable host fixture data. No physical Squeezebox, production service, existing user library, USB/SD medium or stock TinySC setting was accessed or modified.

## Results and boundaries

- Pre-change available host baseline: **71/71 passed, 152.46 s**.
- Final complete available host suite: **72/72 passed, 162.03 s**. Existing WP0, WP1, WP6a, HLS, Now Playing, applet/management, CometD and other available regressions remain enabled.
- New scanner integration test covers native parsing, traversal, exact database content, incremental identity/deletion safety, process isolation, cancellation, removal, active shutdown and policy errors.
- ASan/UBSan plus leak detection pass for the coordinator, metadata reader, common Unicode helper, storage operations, WP1 integration and harness. Upstream SQLite is a separately built uninstrumented archive. Deliberately crashing/limited exec helpers are normal builds; direct parser probes exercise instrumented parsing.
- Both static Baby/Fab4 music builds and normal six-binary builds pass. Final ARM parser/coordinator/**ARM exec helpers** pass under qemu-arm, using wrapper scripts solely to route exec through QEMU. Existing WP0 contract/command and WP1 DELETE/WAL and WP6a storage QEMU tests pass.
- The native host full proxy/TLS/HTTP suite remains unavailable because **CURL_LIBRARY/CURL_INCLUDE_DIR development dependencies are missing**. The 72 tests use BUILD_SBPROXY=OFF and do not claim that unavailable coverage. Independent HLS tests and normal ARM proxy builds pass.

Logs, exact source/artifact hashes and measurement values are indexed in [evidence](STANDALONE_MUSIC_WP2_EVIDENCE.md) and [measurements](STANDALONE_MUSIC_WP2_MEASUREMENTS.json). No target CPU/RAM, decoder behavior, original-kernel removal race or removable-filesystem write durability is inferred from QEMU/host success.

## Corpus and correctness

[wp2_fixtures.py](../tests/music/wp2_fixtures.py) deterministically generates metadata around the existing checked-in CC0 silence/image corpus. No live download or external encoder is needed during tests. Structural MPEG CBR/VBRI header fixtures explicitly test metadata branches without claiming those zero-payload frames are decoded music. Existing real short generated MP3/FLAC/Ogg files supply the audio-format corpus.

| Area | Assertions |
|---|---|
| MP3 | ID3v1/v2.2/v2.3/v2.4, UTF-16/UTF-8, tag unsynchronization, extended headers, album artist, artist lists, compilation, track/disc totals, year, numeric genre, Xing/Info duration, separate CBR/VBRI branches, no-tag fallback |
| FLAC | STREAMINFO rate/channels/bit depth/sample duration; repeated artists/genres, full date, numbering/totals and PICTURE reference |
| Ogg Vorbis | Valid identification/comments/setup framing, CRC/page/lacing handling, EOS duration and base64 picture reparse reference |
| Artwork | ID3/FLAC direct ranges, Vorbis reparse marker, sidecar precedence/path-length encoding/fingerprint revision, no image blobs or thumbnail/cache writes |
| Malformed input | Truncation, invalid declared sizes, 1 MiB/oversized tags, invalid Unicode replacement, corrupt FLAC/Ogg, wrong signatures/extensions, 60 deterministic mutations across three formats |
| Traversal | Unicode/long filenames, symlink loop and /etc escape ignored, cache/metadata directories skipped, unreadable permission masks, depth bounds, real scoped mount/root checks |
| Initial/rescan/update | Exact count/metadata/IDs; no-change scan has zero parser bytes; one changed file updates one existing ID |
| Rename/hardlinks | Supported unique inode evidence preserves ID; hardlink aliases do not create duplicate rows; uncertain identities are not matched by title/tags |
| Missing tracks | Successful complete root removes exactly the missing row; parse/permission/depth failures retain previously committed missing records |
| Scope policy | Changing a source's bound root or automatically adopting a nonempty unbound catalogue is refused without deleting its records |
| Lifecycle | Repeated request BUSY, stale IDs, repeated requests on the same worker, cancellation, revocation latch/new generation, reinsertion/reopen and active/idle shutdown |
| Parser process faults | SIGSEGV, hang before output, hang after output, and a deliberately mutating fixture helper all fail safely; owned child is killed/reaped and no sweep occurs |
| Database faults | Read-only refusal/hash preservation; injected VFS write EIO preserves rows; process-group crash mid-scan followed by reopen preserves committed IDs/content |
| Migration | Disposable v1 and v2 copies migrate to v3 with exact old rows/IDs/default fields; read-only old schema refuses migration without changing bytes |
| Concurrency/resources | Shared WP1 connection broker with simultaneous query worker; bounded conflicts rather than dirty reads; descriptors return to baseline; resource/status counters remain bounded |

Positive storage tests validate an actual `/dev/shm` kernel mount and derive a private scoped root through the new WP6a child-root API. They do not create an ordinary directory and call it a mounted filesystem. Read-only media tests inject only the ro observation for that real host mount and use an explicitly separate writable fixture DB; this is not actual read-only USB/SD qualification. Fault helpers deliberately mutate only their supplied disposable file. Interrupted-scan tests kill a newly created process group containing only that fixture scanner/helper, never an unrelated process.

Committed additions/updates can remain after incomplete scans, as documented; exact old committed missing records remain. Tests verify IDs/path/title/fingerprint and selected full metadata values, not just a success exit code. Queries during ingestion may return explicit transaction-owner CONFLICT and retry between finite batches. No dirty rows are represented as a valid browse snapshot.

## Resource observations and reproduction

See [performance](STANDALONE_MUSIC_WP2_PERFORMANCE.md) for complete 1k/10k/50k runs with concurrent queries, own-process VmHWM, helper memory, FD sampling and kernel write-byte accounting. Lifecycle samples include about 20 ms before deliberate cancel/remove/stop injection; total elapsed bounds are recorded, so they are conservative upper bounds rather than sub-millisecond API-ack claims. Kernel uninterruptible I/O remains outside forced cancellation guarantees.

From WSL/Linux repository root:

```sh
cmake -S . -B build-dual-tests -DBUILD_SBPROXY=OFF
cmake --build build-dual-tests -j4
ctest --test-dir build-dual-tests --output-on-failure
python3 tests/music/test_scanner.py build-dual-tests/tests/test_music_scanner \
  build-dual-tests/native/sbmusic/sbmusic_metadata_helper build-dual-tests/tests/music_metadata_fault
python3 tests/music/benchmark_scanner.py build-dual-tests/tests/test_music_scanner \
  build-dual-tests/native/sbmusic/sbmusic_metadata_helper --output .local/wp2/performance.json
sh scripts/build-music-arm.sh all
python3 tests/music/test_scanner_qemu.py
python3 tests/music/test_storage_qemu.py
python3 tests/music/test_library_qemu.py
sh tests/music/cross_contracts.sh
```

Use the existing configured CMake 3.31.8/toolchain path where system CMake is unsuitable. Run the existing host integration suite sequentially because it shares loopback ports. The metadata helper applies real process limits; sanitizer coverage therefore uses instrumented direct probes/coordinator plus a normal limited exec helper, rather than claiming an ASan shadow mapping fits a 16 MiB address space. Native asserts stay enabled in test builds.

No commit, push, release, deployment or product activation occurred. WP3a/WP3b/WP4/WP6b were not started.
