# WP4a.4 — Host performance and physical measurement boundary

Linux/WSL, controlled distinct small MP3 fixtures in tmpfs, real SQLite working
database on temporary host disk. No internet downloads. Unthrottled benchmark
duty and uncontrolled OS cache. These are host results, not ARM predictions.

| Tracks | Full s | Files/s | Unchanged s | One-file update rescan s | Peak coordinator RSS KiB | DB bytes | Unchanged block-write bytes | Concurrent query p50/p95/p99 ms |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 6.149 | 162.6 | 1.742 | 1.677 | 4856 | 659456 | 24576 | 1.0/21.0/31.0 |
| 10,000 | 57.173 | 174.9 | 18.859 | 18.993 | 4728 | 5816320 | 24576 | 1.0/23.0/29.0 |
| 50,000 | 325.061 | 153.8 | 97.484 | 101.474 | 5112 | 29167616 | 24576 | 4.0/25.0/32.0 |

All unchanged runs preserve IDs/revision with zero metadata parses and zero
track-row updates. The remaining constant host block writes are administrative
scan bookkeeping in the working DB. Snapshot lifecycle tests prove zero persistent
publication for unchanged content; a physical unchanged scan is not yet verified.
Changed successful scans publish one whole closed DB, trading changed-scan write
amplification for avoiding unqualified live SQLite journaling on old FAT.
For context, historical WP2's50k unchanged FLAC corpus accounted90,796,032host
write bytes; this package's50k MP3 corpus accounts24,576. Corpora/database layout
differ, so this is not a controlled like-for-like speed/wear ratio. SQL counters
and snapshot checks directly verify the elimination of per-track unchanged writes.

`.local/wp4a4/benchmarks.json` records self/child CPU, parser RSS, shutdown,
baseline/peak/final FDs, process-open samples, successful query percentiles,
retryable conflicts and separate synthetic WP1 query plans/latency distributions.
Percentiles exclude unsuccessful conflict requests; counts report those separately.
One changed file still requires a root traversal but only one metadata parse.
The scanner FD estimate excludes some database/observer FDs; use sampled peak
for the actual coordinator process. Fixture generation time is excluded from
native scan times; host generation repeatedly reads the small repo fixture over
the Windows mount and is substantially slower than RAM generation would be.

Device policy uses nice10, 25% work-duty pacing, batch16 and 10ms yields; sleep
does not guarantee an exact CPU percentage. Parser child limits: 16MiB AS,
512KiB stack, 2 CPU seconds, 64 FDs, 256KiB metadata, 768KiB reads, ≤5s wall timeout.
One parser child, 16 pending records and 16 directory levels remain bounded.
SQLite default32MiB, configurable1–64MiB; TEMP membership/caches/thread stacks
and inactive refresh copies consume additional memory. RAM free-space checks
fail explicitly without flash fallback. A 50k host result does not establish that
it fits a Fab4 alongside Jive. Baby hardware has no qualified storage adapter.

See the hardware report for cancelled-attempt write counters. No complete-device
throughput, peak-RSS or CPU measurement is claimed. QEMU is functional evidence,
not target-kernel durability/performance evidence. Kernel-blocked I/O has no
proven hard cancellation/shutdown deadline.

## Additional host counters

| Tracks | Full self/child CPU ms | CPU ms/file combined | Sampled peak/final FD | Full host write bytes | Parser RSS KiB | Full shutdown ms |
|---:|---:|---:|---:|---:|---:|---:|
| 1000 | 3446.4/1299.4 | 4.746 | 20/3 | 20656128 | 1664 | 2 |
| 10000 | 31197.2/11006.5 | 4.220 | 20/3 | 243523584 | 1664 | 2 |
| 50000 | 197218.2/57216.7 | 5.089 | 20/3 | 1278996480 | 1664 | 1 |
