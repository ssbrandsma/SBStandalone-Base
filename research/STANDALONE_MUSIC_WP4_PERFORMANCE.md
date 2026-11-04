# WP4 performance observations

Measured 2026-11-03 on WSL Ubuntu 24.04 x86_64. These are host and QEMU observations, never Baby/Fab4 hardware guarantees. Raw values and per-thread CPU counters: [measurement ledger](STANDALONE_MUSIC_WP4_MEASUREMENTS.json).

Benchmark design:1k/10k/50k native WP1-generated indexed fixtures, with only the selected original MP3/FLAC files materialized under a validated disposable root. The mock decoder reports Playing while actual repeated authorized HTTP GETs execute concurrently with120 mixed artist/album/unfiltered-track browse requests and health probes. This is a deliberately harder query mix than WP3b's artist/album wire benchmark; it is not a gapless audio or realtime-output benchmark. Warm/cold OS cache state is not controlled.

Measure end-to-end browse and health percentiles, completed HTTP GETs, process CPU totals, per-thread CPU, wall time, open FD samples and peak RSS. Streaming throughput is separately measured with the actual webserver and1MiB fixture; queue maximum allocation and pure-core RSS are recorded separately. Component bounds are not an additive measured worst-case process RSS.

Budgets:256 committed+256 staged queue records; one pending selection;50-record native DB pages; eight shared async jobs; one secure-open worker; eight resource slots/read jobs; four HTTP streams;16KiB read buffers; roughly32KiB socket audio backlog; up to200 status/browse wire rows;262144-byte response ceiling;5s async database deadline,20s action/reconnect deadline,10s owner/controller deadlines,15s stalled-reader budget. Worker stacks are256KiB where constructed here. No exact CPU percentage is inferred from sleeps/polling.

The actual-scanner test shares the single WP1 handle with browsing and native playback state and verifies exact final row count. Earlier probes that only exercised rejected browse commands are not performance evidence; final output explicitly counts valid successful queries. Host measurements must be repeated on real hardware alongside existing audio applications before assigning device budgets.

## Final host measurements

| Indexed tracks | Browse p50 / p95 / p99 ms | Health p99 ms | Base peak RSS KiB | Web peak RSS KiB | Base CPU s | Workload wall s | HTTP GETs |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 4.44 / 14.55 / 16.28 | 3.90 | 9236 | 2304 | 0.31 | 0.887 | 29 |
| 10,000 | 12.64 / 21.19 / 22.21 | 1.47 | 9236 | 2176 | 0.75 | 1.517 | 50 |
| 50,000 | 18.63 / 63.36 / 67.30 | 1.17 | 9364 | 2304 | 2.77 | 3.473 | 116 |

Each row executes 120 mixed browse queries. Repeated real GET p99 was 31.91/32.14/31.79 ms; sampled FDs were Base16/web6. CPU totals are process counters since startup, not interval-isolated CPU percentages. Database projection work runs on the serialized worker; unfiltered track sorting is more expensive than bounded album projections. No compilation ran concurrently with these measurements. No ARM throughput prediction follows from this table.

The queue allocation, including committed and staged maximum records, is 233,608 bytes. Pure queue test peak RSS was 1,664 KiB; 140-track/three-disc loader including rollback tests took333 ms and peak RSS2,816 KiB. These are short process measurements, not a complete embedded-memory budget.

Actual webserver transferred1,048,576 bytes in0.68935s (1,521,109 bytes/s). Four simultaneous clients and fifth-client rejection were exercised; descriptor count6 before/5 after includes the deliberately closed IPC peer. No accumulating descriptor leak was observed. Buffer size is16KiB; larger throughput, NAS latency and real decoder backpressure remain unqualified.

The native playback/scanner regression indexed1,000 real small FLAC fixtures in5,694ms while1,785 successful database queries ran (zero retryable, maximum24ms). This observation included other acceptance/build activity; it is not a clean scanner-throughput benchmark. ASan's later isolated run indexed the same count in7,120ms with2,067 successful queries, zero retryable and maximum26ms. Mock decoder Playing state and real resource opens/IPC were maintained; no audible output was measured.

QEMU Baby/Fab4 140-track loads took538/455ms; real HTTP transfers were roughly1.52/1.51MB/s under user-mode emulation. These reflect the host/emulator, not target hardware. Removal, shutdown, cancellation, bounded retiring jobs and stale completions are correctness-tested; no guaranteed kernel-I/O cancellation latency or device CPU/RAM budget is claimed. Old-kernel and physical audio timing must be measured before activation.

An initial large-library benchmark setup searched only the first unfiltered page and failed to locate its selected fixture. That run was discarded. The benchmark now obtains the actual indexed album ID and requests its bounded page; the 120-query measured workload remains mixed and includes unfiltered tracks. Production source was unchanged by this fixture correction.
