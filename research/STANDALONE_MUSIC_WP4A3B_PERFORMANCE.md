# Performance

Pre-activation real Fab4 baseline: MemTotal 125848 KiB, MemFree 8432 KiB, cached 48428 KiB, overlay free 6112 KiB, RAM library filesystem 384/8192 KiB used. These are snapshots, not playback budgets or post-activation measurements. New-runtime streaming CPU, HTTP/start/STMs/release latencies, underruns and queue/resource counts remain unmeasured. Actual idle RSS/FD snapshots of the restored old stack are recorded below.

Software-only sanitized host integration indexed 1000 fixture files while issuing 682 successful concurrent browse queries, maximum observed query latency 54 ms and scan time 5863 ms. The decoder was mocked. ARM/QEMU production resource HTTP delivered 1 MiB with four concurrent streams and a 16384-byte buffer, approximately 0.71 s in the recorded runs; no descriptor growth was observed. These are emulator/host measurements and do not predict Fab4 audio performance.

Build artifacts and exact hashes are under build-arm/{baby,fab4}/{music,music-session,maintenance}; private per-run logs include the complete observations. Physical measurements must be added only if the corresponding hardware gates run.

## Actual post-rollback snapshots

| Process | PID | RSS KiB | Open FDs |
| --- | ---: | ---: | ---: |
| Jive | 3192 | 33236 | 50 |
| Browse Base | 3933 | 2480 | 65 |
| Existing proxy | 3282 | 896 | 16 |
| Existing web | 3312 | 452 | 16 |

These are real idle snapshots of the unchanged old process stack, not playback measurements of the new runtime. Overlay free space: 3516 KiB with the verified staging backup retained. RAM library usage: 384 KiB of 8192 KiB. No physical streaming/start/STMs/release latency, playback CPU, underruns or queue/resource memory was measured because activation was refused.

Source-derived rollback reservation is 1428 KiB; actual free space exceeded it. The original installation budget was 5754880 bytes versus 6258688 available and did not take space credit for live old inodes.
