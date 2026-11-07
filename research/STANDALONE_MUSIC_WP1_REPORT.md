# Standalone Music WP1 Report

Completed **2026-11-02**. WP1 implements an isolated portable native SQLite library and tests. Standalone Music remains disabled; no scanner, playback, HTTP/SlimProto handler, Jive browsing, automounter or physical device change was introduced.

Documents: [schema and C API](STANDALONE_MUSIC_WP1_SCHEMA.md), [test results](STANDALONE_MUSIC_WP1_TEST_RESULTS.md), [performance](STANDALONE_MUSIC_WP1_PERFORMANCE.md), [machine-readable measurements](STANDALONE_MUSIC_WP1_MEASUREMENTS.json), [evidence](STANDALONE_MUSIC_WP1_EVIDENCE.md).

## Implementation and reuse

`native/sbmusic/music_library.h/.c` provides asynchronous open/close, native source registration, transactions, track upsert/delete/detail, bounded browsing/filtering, revision tracking, diagnostics and explicit errors. `schema.h` contains the compact owned schema and migration policy. SQLite 3.54.1 is a hash-verified official amalgamation, built as a separate static target with no loadable extensions. Neither LMS/TinySC/Perl nor a firmware SQLite ABI is used.

The WP0 source/media/track/query/page/row contracts are reused. Small internal extensions add a typed search selector, mixed-folder row kind and instance context for the formerly API-only query provider. `sb_music_library_queries()` implements that provider without a global singleton. Existing Now Playing/metadata ownership, command parsing, artwork, decoder/ALSA and app Lua are unchanged by WP1. Existing persistence patterns were inspected (settings/alarm fsync/rename and bounded proxy/image work); database durability uses SQLite transactions rather than a second handwritten replacement mechanism.

The minimum schema covers media, tracks, albums, contributors and roles, genres, folders, artwork references, many-to-many links and library metadata. It preserves committed record IDs through metadata updates and explicit rename. Album keys disambiguate duplicate titles and group multiple discs. User metadata and future fingerprints/scan epochs are stored; no filesystem discovery occurs. Prepared/bound statements are used throughout the production module, finalized per operation. No public raw SQL or LMS serialization exists.

## Threading and storage policy

One explicitly created worker exclusively owns each database connection. A single-flight copied request/result mailbox and nonblocking notification FD integrate with a future existing control loop; there is no second event loop. Submit/receive/cancel use try-locks and never execute SQL. Destroy refuses until close/open-failure completion has been received, so the caller cannot join a live SQL operation. Generations, monotonically increasing request IDs, transaction tokens and snapshot revisions reject stale work. Query count and page are read in one snapshot; persisted revisions advance atomically even with two handles.

Database filename/create permission are explicit, with no default internal-flash path or automatic directory creation. Every handle begins with no trusted online source until registration. DELETE journal is default; WAL requires an explicit safe-storage policy. FULL sync, foreign keys, a 512 KiB cache, no mmap and an 8 MiB shared SQLite allocator cap are configured. Busy waits are bounded; progress cancellation/deadlines cover SQLite VM work, not uninterruptible kernel I/O. The ordinary disabled product starts no database thread and opens no database.

Offline events revoke availability and abort pending metadata work. I/O errors invalidate runtime source state and require explicit close/reopen. Corrupt, foreign and unsupported newer schemas are reported/preserved. Native v1->v2 migration is transactional; read-only migration is refused. No destructive rebuild, orphan cleanup or user-data deletion policy is hidden in open/recovery. Source identity allocation, mount verification and actual filesystem guarantees remain future storage-adapter responsibilities.

## Validation

The pre-change host baseline passed **67/67** tests. The final available host suite passed **69/69** in **267.18 s**, including new storage/recovery and 10k/50k correctness suites. Complete page traversal checks exact counts/order/relationships. Coverage includes Unicode/long paths, duplicate titles, multi-artists, compilation/disc order, source reinsertion/two-source identity, stable rename/update/delete, read-only, lock contention, revisions, a real hot-journal process crash, I/O injection, allocation failure, migration/newer-schema/corrupt copies and FD stability. Focused address/undefined-behavior sanitizer checks passed for module/harness; upstream SQLite was not sanitizer-instrumented.

Normal static Baby/Fab4 builds and new isolated archives build successfully. WP0 contract/command tests and library correctness for DELETE/WAL pass under user-mode QEMU on both targets. This is not old-kernel, actual audio or removable-media qualification. The full native host proxy suite remains blocked by missing libcurl headers/library; independent HLS tests and ARM proxy builds pass. No proxy coverage is claimed beyond that.

## Measured outcome and overhead

The synthetic 50k database is **20,127,744 bytes**, including **10,977,280 index bytes** (about 403 total bytes/track). The final host run imports it in about **47.9 s**, updates ten tracks in **43.1 ms**, and observes warm p95 album/track-page times of about **12.3/10.8 ms**; broad title-or-album-or-artist search is about **123.4 ms**. These are host observations under development load, not device projections. First-connection samples are separately recorded, with OS cache uncontrolled.

Across repeated queries the SQLite live allocation bytes stay at the same measured level and FD counts stay **6/6**. Whole-process benchmark peak RSS is roughly **3.1-3.4 MiB**, including harness/runtime; this is not a measured delta against Base. Fixed mailbox storage, a 256 KiB worker stack and code residency are additional considerations when the feature is enabled later. Archive/harness sizes and exact numbers are in the performance artifact.

WP1 adds **zero bytes to the sizes of the six normal runtime binaries**, because the library is not linked into them. Five hashes remain identical to WP0; sbwebserver's same-size rebuild changes its embedded date/time and resulting hash. Its source was not changed. The new archives live under `build-arm/<target>/music/` and are excluded by the existing explicit packaging list. No release, commit, push or deployment was performed.

## Readiness and remaining risks

WP2 can consume transactional native inserts and fingerprints/epochs, but still needs bounded parser/scanner scheduling and short ingestion batches so interactive reads can run between transactions. WP3b can consume native paged records/revisions, but still needs Jive/LMS serialization and notification behavior. WP6a can supply validated DB roots and media generations, but still needs real identity/mount/eject handling and filesystem-specific journal qualification. None of these packages has been started.

Remaining limits: actual Baby memory/headroom and old kernels are unmeasured; read-only WAL sidecars and FAT/NTFS/FUSE locking/fsync need hardware evidence; process-crash tests do not prove power-loss durability; non-ASCII case/accent normalization is intentionally limited; old identity records can accumulate without future explicit maintenance; arbitrary raw-byte filenames are not silently converted; broad relation search and large offsets need further device profiling. The existing sbbase slow-client network wait remains a prerequisite for future playback transport. These limitations are explicit and do not block the independent WP1 backend/test deliverable.

Recommended next work is **WP2**, or the independent WP3b/WP6a packages, only after an explicit new instruction. Stop after WP1.
