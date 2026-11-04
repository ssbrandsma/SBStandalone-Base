# WP2 performance and resource measurements

2026-11-02. These are **host WSL/Linux measurements**, not ARM device projections. All datasets are disposable; no physical device was used. [Raw measurements](STANDALONE_MUSIC_WP2_MEASUREMENTS.json) and [benchmark driver](../tests/music/benchmark_scanner.py) preserve the exact values/method. Concurrent builds/regression work and OS caching were not controlled, so timings are observations, not universal guarantees.

## Dataset and method

Each record is a distinct regular file containing a real **298-byte generated FLAC silence fixture**, including STREAMINFO, comments and an embedded PNG reference. Files are grouped in folders of 100; they are copied, not hardlinked, so hardlink deduplication does not defeat the count. Each size runs initial scan, unchanged full-root reconciliation and a full-root reconciliation with one valid title change. Exact database counts and changed/unchanged counters are asserted. The initial run also executes query requests against the same WP1 worker/connection.

Benchmarks select work_duty_percent=100 and no payload pacing, explicitly overriding the default cooperative 50 work/rest setting. The scanner still uses bounded batches, child limits and cancellation guards. CPU includes separate coordinator and waited helper CPU, divided by file count; it is not a promised CPU duty percentage. RSS uses each executable's own `/proc/self/status` **VmHWM**, avoiding fork-inherited getrusage peaks. Query latency has millisecond resolution and a bounded 4096-sample reservoir across successful queries. Transaction-owner conflicts are counted separately, never treated as successful dirty reads. Large missing-track finalization is one atomic sweep, subject to the existing finite SQLite progress/deadline and kernel-I/O limits; its worst-case query latency is not established by these initial/no-change/one-update benchmarks.

## Final host runs

| Files | Initial seconds | Files/s | CPU ms/file | Unchanged seconds | One-update full reconciliation seconds | DB bytes |
|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 7.030 | 142.25 | 2.622 | 1.170 | 1.149 | 843,776 |
| 10,000 | 56.251 | 177.77 | 2.202 | 8.334 | 9.510 | 7,643,136 |
| 50,000 | 260.653 | 191.83 | 2.242 | 39.722 | 39.236 | 38,653,952 |

All no-change runs read **zero metadata parser bytes** and start no parser child. They still traverse, fingerprint bounded file edges and update scan-epoch stamps in batches. One-update runs parse one file; the table measures its complete root reconciliation, not just that parser's isolated latency. No inotify watcher/path-only update pipeline is implemented. Consequently 10k full incremental reconciliation exceeds Phase 0's proposed 5-second changed-file target; efficient explicit-path/watch scheduling remains future work, rather than a silently claimed passed budget.

The final 50k database is about **773 bytes/track**, below the provisional 2 KiB/track storage budget for this corpus. It differs from WP1's synthetic approximately 20.1 MB database: these records contain real file paths/stat/content fingerprints/artwork descriptors and the new fingerprint index/schema fields. Identical copied metadata does not make them the same track identity. Covers remain in source files; no image blobs are duplicated in SQLite.

| Files | Coordinator/DB/harness peak RSS KiB | Helper peak RSS KiB | Sampled process FDs baseline/peak/final | Successful queries / conflicts | Query p95 ms |
|---:|---:|---:|---|---|---:|
| 1,000 | 4,688 | 1,664 | 3 / 19 / 3 | 607 / 37 | 2 |
| 10,000 | 4,940 | 1,664 | 3 / 19 / 3 | 4,901 / 256 | 3 |
| 50,000 | 4,684 | 1,664 | 3 / 19 / 3 | 21,816 / 977 | 4 |

RSS is whole executable residency, including SQLite/runtime/code/harness. Separate process RSS is not unique private memory and should not simply be summed as exclusive allocations. The parser's 16 MiB **virtual** limit is not its observed RSS or a guarantee of a particular Baby footprint. Existing SQLite still has a 512 KiB cache and an 8 MiB shared allocator ceiling; scanner copied records, directory state and thread stacks are additional. Descriptor sampling can miss very short spawn peaks; the implementation separately has fixed descriptor bounds. At maximum directory depth, a conservative scanner-owned allowance is 28 descriptors, plus the caller/library/stdio; the helper closes unrelated fds and has a 64-fd ceiling. No-growth checks compare final descriptors to baseline.

## Write volume, shutdown and limits

Kernel-accounted write_bytes for initial / unchanged runs:

| Files | Initial bytes | Unchanged bytes |
|---:|---:|---:|
| 1,000 | 15,749,120 | 1,912,832 |
| 10,000 | 177,307,648 | 18,251,776 |
| 50,000 | 915,070,976 | 90,796,032 |

These are process/kernel accounting observations on the host filesystem, including DELETE/FULL transaction journals and epoch writes; they are not measured physical USB flash wear. Write amplification is significant, especially for frequent full-root scans. WP2 does not weaken journaling or enable WAL to hide it. Event/path-targeted incremental scheduling and lower-write epoch accounting are possible future optimizations requiring the same deletion guarantees. No automatic scan or periodic flash writes are activated by this package.

Idle worker shutdown after completion measured **1 ms** in all three initial benchmark samples. Controlled hanging-helper cancellation/removal/active-stop tests record total elapsed **31 / 31 / 31 ms**, including approximately 20 ms before injection; terminal-to-destroy was 0-1 ms. These are host fixture observations, not precise API acknowledgement/device timing guarantees. A kernel syscall stuck in uninterruptible I/O can delay helper reaping and worker destruction; the controller still returns asynchronously and does not join live I/O.

Bounded parser work: metadata tag/block/packet <=256 KiB, media reads <=768 KiB per probe, MP3 header window <=64 KiB, Ogg tail <=64 KiB, and finite block/page/comment counts. The unusually large ID3 fixture is rejected before allocation; it does not reproduce unrestricted TagLib's WP0 15 MiB allocation behavior. Per-file exec, proc/mount validation and prepared database operations are visible costs. They buy crash containment and conservative root checks; a future performance optimization must preserve those guards rather than merely skip them.

The six normal runtime binaries retain their sizes; five hashes remain identical, while sbwebserver's embedded build date/time changes its same-size hash. Scanner archives/helpers are isolated and excluded from normal package binary selection. The factored HLS Unicode helper leaves the normal ARM sbproxy binary hash unchanged. Disabled/Baby-no-storage operation starts no scanner/helper/database work and gains no linked scanner memory. Actual enabled Baby/Fab4 CPU, RAM, flash behavior and concurrent audio impact remain unmeasured and require separate authorized hardware qualification.
