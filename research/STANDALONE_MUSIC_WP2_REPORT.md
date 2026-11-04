# WP2 native scanner and metadata extraction

Completed 2026-11-02. WP2 implements a bounded native scanner and metadata extraction behind the existing disabled product gate. No physical device/service/user library was accessed or modified, and no insertion listener, music UI, playback, unmount or TinySC action was added. The integration plan below was recorded before implementation.

## Integration plan

Reuse WP0 source/media/generation contracts, WP6a anchored root validation/open and the single-flight WP1 database worker. Extend the existing database request vocabulary with bounded scanner lookup/stamp/epoch operations rather than introducing another connection. Preserve IDs by source/path, with conservative uniquely matched inode/fingerprint rename support only on explicitly suitable filesystems. Add required totals/bit depth through a transactional owned-schema migration. Keep historical research indexes unchanged.

Move the existing HLS ID3 Unicode conversion into a common parameterized helper while retaining its current behavior and tests. Implement focused bounded native MP3/FLAC/Vorbis metadata readers behind a descriptor-only API; unrestricted TagLib remains a reference, not a runtime dependency. Store artwork descriptors and fingerprints only; reuse the existing image infrastructure later in WP5, without new decoders/caches/thumbnails.

Use one low-priority scanner coordinator thread, following the existing worker/pipe/try-lock pattern. Isolate each metadata probe in an exec'd bounded helper process, analogous to the existing image helper, with descriptor passing/inheritance, finite read/memory/CPU/wall budgets and cancellation/kill/reap. A thread alone cannot isolate parser faults; a whole scanner process would require another database IPC/connection. The coordinator performs no SQL and uses short WP1 requests/batches, leaving scheduling opportunities for other queries. No busy polling or unbounded work queue.

Traverse descriptor-relative with fixed depth/path/FD/error limits. Skip symlinks, special files, internal/cache/metadata directories and unsupported formats. Treat inability to inspect a relevant subtree/file, mutation, parser limit/error, cancellation or generation loss as incomplete; never sweep on incomplete scans. Successfully committed additions/updates may remain, but missing-track removal is a separately validated successful-root transaction. No implicit database placement/open is permitted: caller provides an already approved library; removable persistence remains unqualified, with no internal-flash fallback.

Establish the 71-test baseline, then add deterministic valid/malformed/lifecycle fixtures, exact DB checks, concurrent-query benchmarks at 1k/10k/50k where practical, sanitizer runs and both static ARM/QEMU targets. Report host results separately from emulation and unmeasured hardware; keep the missing-libcurl proxy limitation explicit. Stop after WP2.

## Delivered implementation

`native/sbmusic/music_scanner.h/.c` supplies explicit request/cancel/status/media-revocation/stop/destroy APIs, descriptor-relative iterative traversal, fixed queues/errors/depth/FD limits, short ingestion batches, incremental fingerprints, safe successful-root epochs, process isolation and cooperative pacing. `music_metadata.h/.c` and `music_metadata_helper.c` implement MP3/FLAC/Ogg Vorbis metadata and reference-only artwork discovery. No decoder or new image-processing subsystem is included.

WP1 gains worker-only exchange over its existing connection/mailbox, pure DTO validation and scan-token operations in `music_library_scan.inc`. Schema v3 transactionally adds bit depth, numbering totals, full release date/total samples, persisted root scope, scan counter and fingerprint index. Read-only old schema refuses migration while preserving its bytes. Existing committed track IDs remain stable across updates and confidently supported renames. Unchanged rescans do not invoke metadata parsers; failed/incomplete/offline/cancelled scans never sweep missing records. Earlier fully committed additions/updates may remain useful.

WP6a gains validated child-root handles and scoped path revalidation, without an automounter. Real host tests can select a private directory within an actual kernel mount. Per-source persisted scope refuses accidental root switching and automatic adoption of a nonempty unbound catalogue. Media revocation is latched for the scanner handle and cannot be undone by a replayed old online snapshot. Reinsertion uses a new validated handle and generation; catalogue identities survive.

The existing HLS Unicode conversion is shared through `native/common/metadata_text.h` while preserving its wrapper behavior, parser tests and normal ARM proxy binary hash. Existing artwork renderer/cache, Now Playing DTOs, Radio, Spotify and Qobuz interfaces remain unchanged. Native metadata/helper archives and test tools build separately; normal packaging still selects only the six existing executables. No new network listener or production startup path exists.

## Validation and measured result

The baseline passes **71/71**; the final available regression suite passes **72/72**. Focused native scanner/parser/lifecycle tests and ASan/UBSan/leak checks pass. Baby/Fab4 normal and isolated static builds pass. QEMU exercises ARM parsing, the coordinator and actual ARM exec helpers, plus existing WP0/WP1/WP6a tests. Host full proxy/TLS/HTTP tests remain blocked by missing libcurl development files; no unavailable coverage is counted.

The final 50,000-file host fixture scans in **260.7 s**, or about **192 files/s**, with **38.7 MB** database size. No-change and one-update full-root reconciliation take about **39.7/39.2 s**, with zero / one metadata reparses respectively. Own-process peak RSS is **4684 KiB**, isolated helper peak **1664 KiB**; these are separate whole-process host observations, not measured private Baby RAM. FDs return **3 -> sampled 19 -> 3**. Concurrent successful query p95 is about **4 ms**, with explicit retryable transaction conflicts. DELETE/FULL write amplification and full traversal costs remain significant; no unsafe journal settings or automatic scans were introduced to hide them.

## Limitations and next-package readiness

Root/path access remains limited to WP6a's combined 1023 bytes, while DB metadata paths can be longer; over-limit traversal is incomplete rather than truncated. Unicode uses the existing replacement policy and no NFC/full case folding. Bounded edge/stat fingerprints are not full cryptographic content hashes. Rename matching is opt-in on supported inode-stable environments, never enabled for FAT/NTFS by assumption. Compressed ID3 transforms, historical tag extensions, chained/multiplexed Ogg and exact gapless timing are outside the supported subset. Unknown/unsafe duration remains explicit rather than requiring a whole-file decode.

There is no descriptor-safe removable SQLite VFS or hardware filesystem qualification yet. Scanner construction requires an already approved separate library and refuses implicit persistence/internal-flash fallback. Real-media ro/rw locking/fsync, old target APIs/headroom, faulty-media kernel I/O, actual app playback impact and eventual asynchronous browse integration remain qualification gates. Blocking exchange is worker-only; it must not be called by a future protocol callback or mixed with raw consumption of the same mailbox. Sleeping/priority changes do not promise exact CPU/whole-device I/O quotas.

Recommended next package is **WP3a transport/physical ownership**, before any playback work; WP3b browsing can independently consume the library through a properly integrated asynchronous dispatcher. Neither package is started here. WP2's native scanner/library contracts are available for their development, with product activation still gated by storage/firmware and hardware validation. **Stop after WP2.** No commit, push, release or deployment was performed.

## Deliverables

- [Scanner design and API](STANDALONE_MUSIC_WP2_SCANNER_DESIGN.md)
- [Metadata formats and artwork semantics](STANDALONE_MUSIC_WP2_METADATA_FORMATS.md)
- [Test results](STANDALONE_MUSIC_WP2_TEST_RESULTS.md)
- [Performance and resource limits](STANDALONE_MUSIC_WP2_PERFORMANCE.md)
- [Evidence, source and measurement indexes](STANDALONE_MUSIC_WP2_EVIDENCE.md)
