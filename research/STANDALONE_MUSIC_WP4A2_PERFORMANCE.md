# WP4a.2 measurements and limits

## Actual Fab4 native scan

Explicit scan-only run, one album folder, approved8MiB private tmpfs database:

| Measurement | Result |
| --- | ---: |
| Files visited | 15 |
| Tracks indexed / distinct IDs | 11 / 11 |
| Errors | 0 |
| Scanner elapsed | 3684ms |
| End-to-end wall including SSH | 4.082s |
| Tracks per scanner second | about2.99 |
| Reported metadata-helper peak RSS | 144KiB |
| SQLite size | 122880bytes (120KiB) |
| SQLite integrity_check | ok |

The scanner used batch4, configured25% duty and10ms yield; this is not a measured or guaranteed CPU percentage. Parent RSS, per-process scan CPU, open-descriptor peak and concurrent query latency were not sampled. This small scan is not a large-library or Baby performance prediction. No decoder/listener/native audio was constructed in scan-only mode.

Initial installed-service baseline: MemTotal125848KiB, MemFree52816KiB. Later with Spotify running: MemFree20044KiB, Cached40900KiB, Buffers16464KiB. A recovery sample with Spotify/TinySC stopped: MemFree45864KiB. These are different workloads; MemFree is not MemAvailable and page cache is reclaimable. Original baseline raw CPU tick/RSS/FD samples remain in the evidence JSON and are not percentages.

Host private-mount scan: two MP3/FLAC fixtures,36ms reported scan,1664KiB helper RSS, distinct IDs/integrity/capability removal passed. This is not ARM throughput. Both QEMU runs only tested production entry/refusal paths. Playback CPU/RAM, HTTP streaming throughput, queue/reconnect gaps and underruns were not measured. One unexpected hardware reboot occurred during the later Jive probe.
