# WP2 evidence and reproduction index

2026-11-02. Current repository source is authoritative. [Source ledger](STANDALONE_MUSIC_WP2_SOURCES.json) records exact hashes/line anchors for implementation, tests, build integration and relevant existing components. [Measurements](STANDALONE_MUSIC_WP2_MEASUREMENTS.json) records final host runs, ABI/artifact hashes, log hashes and limits. Historical Phase 0/WP0/WP1/WP6a ledgers remain their original snapshots; WP2 deliberately extends some of those files rather than rewriting historical evidence.

## Local implementation evidence

| Source | Evidence |
|---|---|
| `native/sbmusic/music_scanner.h/.c` | Disabled factory, validated scoped root, native lifecycle/counters, immutable revocation, fixed traversal/record/error limits, bounded exec helper, transactional batches, successful-root finalization |
| `native/sbmusic/music_metadata.h/.c` | Descriptor-only MP3/FLAC/Ogg readers, bounded pread, shared Unicode conversion, artwork references, parser result validation |
| `native/sbmusic/music_metadata_helper.c` | Actual exec worker, 16 MiB virtual/512 KiB stack/CPU/FD/core limits, inherited-fd cleanup, own VmHWM, no file path opening |
| `native/common/metadata_text.h`, `native/sbproxy/hls_id3.c` | Shared extraction of original HLS text/scalar routines; existing HLS capacities/behavior retained |
| `native/sbmusic/music_library.h/.c`, `music_library_scan.inc`, `schema.h` | Existing SQLite worker/connection, broker, schema v3/migrations, source/root/epoch guards, lookup/touch/finish/abort, stable IDs and bound SQL |
| `native/sbmusic/music_storage.h`, `music_storage_linux.c` | Existing mount/root validation extended with a descriptor-anchored child scope; no second filesystem abstraction or mount commands |
| `native/sbwebserver/src/image_render.c` | Existing asynchronous image worker/limits inspected; no duplicate renderer/cache/thumbnail service added |
| `native/sbbase/include/nowplaying.h`, `native/common/music_contracts.h` | Existing bounded presentation/source/generation/resource contracts retained |
| `tests/music/wp2_fixtures.py`, `test_scanner.py`, `test_scanner.c`, `metadata_fault.c` | Deterministic CC0-derived corpus, exact DB assertions, malformed mutations, lifecycle/policy/isolation/fault tests and diagnostic harness |
| `tests/music/benchmark_scanner.py`, `test_scanner_qemu.py` | Scalable distinct physical fixture files, simultaneous query worker, complete root rescans and actual ARM helper exec under QEMU wrappers |
| `tests/music/test_library_storage.py` | Original preservation/recovery regression extended to explicit v1/v2 -> v3 migration |
| CMake / `scripts/build-music-arm.sh` / packaging selection | Isolated archive/helper builds on both targets; no runtime activation or new packaged native executable |

Prior reports/contracts/source indexes/test results were reviewed and fingerprinted under ignored `.local/wp2/inputs.json`. Original captured firmware evidence remains under `.local/fab4-music-research`; no new device connection was used. Captured Fab4 kernel is 2.6.26.8. Existing firmware udev mounting and TinySC startup paths are not changed. The earlier WP6a warning that stock SqueezeCenter can independently restart TinySC still applies to future product activation; new scanner code never invokes it.

## Primary references

- [ID3v2.3](https://id3.org/id3v2.3.0) and [ID3v2.4](https://id3.org/id3v2.4.0-structure): tag/frame structure, text encodings, sizes, extended headers and unsynchronization.
- [FLAC RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html): file-level metadata layouts.
- [FFmpeg's primary MP3 demuxer source](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/mp3dec.c): independent Xing/Info/VBRI frame-count/offset reference; no FFmpeg runtime/code dependency is added.
- [Vorbis I](https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html) and [Ogg framing](https://xiph.org/ogg/doc/framing.html): headers/comments, lacing, page sequence/CRC and granules.
- [Linux setrlimit](https://man7.org/linux/man-pages/man2/getrlimit.2.html) and [priority semantics](https://man7.org/linux/man-pages/man2/setpriority.2.html): distinction between virtual memory/CPU limits and best-effort scheduling. No prlimit/pidfd/cgroup dependency is introduced.
- [SQLite locking](https://www.sqlite.org/lockingv3.html) and [WAL](https://www.sqlite.org/wal.html): existing WP1 journaling/sidecar policy, not evidence of removable-driver durability.

Format rules are implementation references, not a claim of exhaustive conformance or copied decoder code. No new runtime parser dependency is introduced. A development-only clang-format installation resides in ignored `.local/wp2/formatter`; it is neither packaged nor needed to execute tests. Tests need existing checked-in fixtures, C/POSIX tools, Python and optional QEMU, with no live network downloads.

## Raw artifacts and checks

Ignored `.local/wp2/` contains baseline/final CTest logs, focused scanner and sanitizer logs, normal/isolated ARM build logs, QEMU logs, own-VmHWM performance JSON, controlled lifecycle JSON, runtime-before hashes, ELF inspection and the repeated missing-libcurl configuration probe. Earlier exploratory measurement files are not substituted for final results. The checked-in measurement ledger embeds the final result values and hashes, so source/artifact claims can be audited even though raw workstation paths are not distributed.

No actual device measurements, mounted removable media, service changes, hidden device directories, destructive recovery, user library modification or deployment are claimed. See [test results](STANDALONE_MUSIC_WP2_TEST_RESULTS.md) for coverage limits and [design](STANDALONE_MUSIC_WP2_SCANNER_DESIGN.md) for the explicit persistence and activation gates.
