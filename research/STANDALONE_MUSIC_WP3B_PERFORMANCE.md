# WP3b performance and resource observations

Final successful host run2026-11-02/03. Raw values, including samples/CPU/response sizes, are retained in [measurement ledger](STANDALONE_MUSIC_WP3B_MEASUREMENTS.json). The earlier failed50k run is diagnostic evidence only and is not included as a passing result.

## Host query benchmark

Each size uses locally generated compact metadata fixtures. Native benchmark:120 queries spanning artists, albums and album tracks, at most50 rows. HTTP benchmark:120 mixed artist/album requests, requested200 rows at offsets0/50/100, with concurrent health polling every5ms. Some artist/end pages have fewer rows. OS caches are not explicitly flushed; this is not a universal cold-cache query workload. Native timings use integer milliseconds; zero means below that clock resolution, not no work.

| Tracks | HTTP p50 ms | p95 ms | p99 ms | Native p95 ms | Health p99 ms | Peak RSS KiB | CPU s | Wall s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1,000 | 2.563 | 5.329 | 5.681 | 1 | 0.942 | 8320 | 0.11 | 0.405 |
| 10,000 | 5.307 | 12.129 | 13.192 | 1 | 0.864 | 8320 | 0.28 | 0.890 |
| 50,000 | 5.561 | 18.360 | 20.247 | 3 | 0.858 | 8192 | 0.64 | 1.254 |

RSS is process ru_maxrss, not private allocations or Python harness memory. Each benchmark sampled12 open descriptors. Serial benchmark observed async/HTTP peaks1/1; saturation scenarios separately exercise the configured8/4 limits. No all-library in-memory cache is introduced. Host throughput and QEMU timings do not predict Baby/Fab4 hardware performance.

## Concurrent scanner and socket observations

The native correctness scenario runs an actual WP2 scanner on1000 compact valid FLAC fixture files on a validated private root, sharing the WP1 handle with browse requests. The final focused CTest observation completed5577ms with1828 reads,0 retryable results,max27ms query and4836KiB RSS, and checks exact1000-row contents. Its raw output is retained with the measurement ledger. The instrumented repeat completed6955ms with2110 reads,max29ms and146268KiB RSS; sanitizer overhead disqualifies that RSS as a target budget.

Long projected names produce88621-byte complete HTTP responses.37-byte/EAGAIN injection passes, with58.646ms maximum observed in the ASan HTTP scenario. A held exclusive SQLite lock produces a bounded error at about5.008s while health remains responsive (observed max0.433ms). These are observed scenarios, not worst-case scheduling guarantees.

QEMU CPU-specific functional runs: Baby max129.269ms and Fab4 max101.678ms in the3000-track mixed HTTP scenario. No device RAM/CPU or original-kernel latency measurements exist.

## Explicit budgets

Eight async slots, eight live browse calls, four native HTTP batch states,200 wire rows,50 native rows,262144-byte JSON per call,340648-byte host HTTP state (including262144 output and65536 deferred tail),21640-byte enriched WP1 result,256KiB coordinator stack. Existing WP1 cache policy remains512KiB and mmap disabled. These component limits are not a measured aggregate worst-case RSS; native structures, worker stacks, library heap and concurrent subscriptions also contribute. Before activation, measure simultaneous saturation and memory on real Baby/Fab4 with existing audio applications active.

Deadlines are at most5s, retries at most500, broker reservation20ms, inter-attempt gap2ms; no hard CPU percentage or kernel-I/O interruption guarantee. One-second summary publication and scan coalescing limit event traffic. Output feeds at most16KiB into the existing64KiB queue and closes bounded-tail overflow.

## Investigated regressions

The initial50k run failed its deadline because the album summary EXISTS omitted the media_id predicate required by the existing compound index. Adding `t.media_id=a.media_id` removes the repeated broad track scan without a schema/index change. The final1k/10k/50k run above is entirely green. A separate handoff gap was fixed by submitting the next native page before returning to poll, instead of relying on a later timer after consuming the last ready byte. No debug tracing remains in source.
