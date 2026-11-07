# Standalone Music WP1 Performance

Measured 2026-11-01/02 on the WSL Linux host, using native C synthetic fixtures and disposable /tmp databases. These are not Baby/Fab4 hardware measurements. The [measurement artifact](STANDALONE_MUSIC_WP1_MEASUREMENTS.json) includes exact query plans, CPU, memory and build hashes; [test results](STANDALONE_MUSIC_WP1_TEST_RESULTS.md) gives reproduction commands.

Every run starts with a fresh database and imports deterministic 1k/10k/50k tracks. Import transactions contain up to 1000 tracks; incremental time updates ten tracks in one transaction. Warm queries are 50-row pages, mean/p95 of twenty requests. A separate first query is measured after connection reopen and integrity checking. OS page cache was not dropped: the first-connection measurements are **not cold storage/cache results**. Host load, concurrent checks and WSL scheduling affect small timing differences.

| Tracks | DB bytes | Index bytes | Bytes/track | Import seconds | Ten updates ms | Total CPU seconds | Peak process RSS KiB |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1000 | 483328 | 253952 | 483.3 | 0.782 | 35.331 | 0.862 | 3428 |
| 10000 | 4001792 | 2166784 | 400.2 | 8.135 | 25.162 | 8.038 | 3328 |
| 50000 | 20127744 | 10977280 | 402.6 | 47.912 | 43.113 | 47.746 | 3220 |

| Tracks | Artists first / warm mean / p95 ms | Albums first / mean / p95 ms | Track page first / mean / p95 ms | Search first / mean / p95 ms |
|---:|---:|---:|---:|---:|
| 1000 | 1.002 / 1.111 / 1.116 | 1.095 / 0.979 / 1.288 | 0.519 / 0.607 / 0.779 | 1.535 / 1.328 / 1.672 |
| 10000 | 1.233 / 0.762 / 1.066 | 1.845 / 1.425 / 2.084 | 1.164 / 1.321 / 2.310 | 15.693 / 18.990 / 27.001 |
| 50000 | 1.417 / 1.143 / 2.615 | 5.620 / 7.492 / 12.266 | 10.274 / 7.906 / 10.815 | 167.840 / 108.256 / 123.442 |

General track search intentionally matches title OR album OR contributor prefix; its relation checks cost more than an ordinary indexed browse. Field-specific title/artist/album filters and ID filters are available. Percent/underscore are literal escaped characters. There is no full-text engine or arbitrary SQL/substring search.

## Plans and tuning

The initial 50k album path exposed a missing media predicate in the correlated lookup. Adding it allowed `track_album(media_id,album_id,disc,track,id)` to be used instead of repeatedly walking unrelated tracks. The final plans show `track_name` for track ordering, `album_name` and covering `track_album` for albums, and `contributor_tracks` for artist relationships. The small artist-name list uses a temporary ordering B-tree; no redundant name index was added solely to remove that tiny sort. Folder/recent/reverse-genre indexes correspond to supported actual queries. Index size comes from SQLite dbstat, including automatic unique/primary indexes.

Counts and pages share a read transaction. Queries return only bounded native rows; C never collects the entire SQL result set. Large offsets/counts and general filters can still require more SQLite work, so progress deadlines and allocator limits are part of the resource policy, not promises of constant-time queries.

## Memory and descriptors

The 512 KiB SQLite page cache is configured before quick_check. Mmap is disabled, temp storage is memory, and the shared vendored SQLite allocator has an 8 MiB hard ceiling (or an existing stricter ceiling). Fixed mailbox/DTO storage and a 256 KiB worker stack are additional; allocator cap is not a process RSS cap. The inactive product creates none of these resources.

- 1000 tracks: SQLite memory 600696 before / 600696 after the query repetition; high-water 674464 bytes; open descriptors 6 / 6.
- 10000 tracks: SQLite memory 618168 before / 618168 after the query repetition; high-water 712768 bytes; open descriptors 6 / 6.
- 50000 tracks: SQLite memory 618168 before / 618168 after the query repetition; high-water 728832 bytes; open descriptors 6 / 6.

Whole-process RSS includes the executable/static runtime, harness stacks and library. It is not an additional-memory measurement against a running Squeezebox. The compact seeded corpus shares artist/genre/artwork references; its database bytes/track do not predict worst-case long names, eight contributors per track or real artwork files. Original images are not stored in SQLite.

## ARM footprint and budgets

Both isolated archives and the complete fixture executable compile statically for ARMv5TE Baby and ARMv6 Fab4. Archive file bytes include relocation/symbol information and are not resident RAM. Complete fixture-executable sizes below also include test/fixture/assertion code, not just the backend.

| Target | Library archive bytes | SQLite archive bytes | Stripped fixture executable bytes |
|---|---:|---:|---:|
| baby | 44874 | 1186162 | 757756 |
| fab4 | 44830 | 1180314 | 749564 |

Normal runtime binaries have **zero size growth** from WP1. sbbase, sbproxy, SVG renderer and both storage helpers retain their WP0 hashes. sbwebserver retains its size but its hash changes on rebuild because `system_info.c:419-420` embeds __DATE__/__TIME__; its source was not changed. The SQLite/library archives are not linked or packaged into those runtime binaries.

Conservative future acceptance targets, pending real devices:

| Metric | Fab4 target | Baby provisional target |
|---|---|---|
| Added idle worker CPU | <=0.5% over ten minutes | <=0.5%, verify actual headroom |
| Added idle private memory | <=3 MiB | <=3 MiB; actual Baby RAM still unverified |
| Ordinary warm browse p95 | <=150 ms | <=250 ms initial qualification target |
| First-storage-page p95 | <=500 ms | <=750 ms provisional |
| Database average | <=2 KiB/track excluding covers | Same corpus-dependent target |
| Ten-record incremental update | <=5 s after scheduling | <=5 s after scheduling |
| Control-loop SQL blocking | None; only worker executes SQL | Same |

These are targets, not projections obtained by multiplying host speeds. True cold storage, slow-media behavior, power-loss durability, I/O while decoding audio and old target kernels are unmeasured. QEMU proves only user-mode functional behavior on the host filesystem/kernel. WP2 must add bounded ingest scheduling; WP6a must validate actual filesystem/journal policy. The existing sbbase slow-client network wait remains outside this work package.
