# WP4a.2d real Fab4 measurements

All values below are actual .244 Fab4 observations, not host/QEMU predictions.
Corpus is one existing album, 15 entries/11 MP3s. Do not extrapolate to a large
library, different filesystem or Baby hardware.

| Measurement | Observation |
|---|---|
| Scanner elapsed | 8014 ms |
| Parent process CPU during scan window | 1136 ms (includes native worker work) |
| Reaped metadata-helper child CPU | 100 ms |
| Helper peak RSS | 144 KiB, target-kernel reported |
| SQLite file | 122880 bytes / 120 KiB |
| Dedicated tmpfs | 8192 KiB max, 128 KiB used, 8064 KiB available |
| Instrumented mixed HTTP median | 246.06 ms |
| Instrumented maximum | 585.89 ms |
| Protocol sample | 32 mixed requests, including 20 repeated track pages |

HTTP timings include existing SSH direct-tcpip channel setup plus HTTP request,
native dispatch and response. They are not pure SQL time or measured visual menu
latency. Actual Jive responsiveness needs physical observation. The configured
25% scanner duty is cooperative scheduling, not an exact CPU ceiling. Reported
scan CPU is about 1.236 CPU seconds over 8.014 elapsed seconds; not a guarantee.

Five stable post-scan observations span 59.75 seconds:

| Process | PID/start | RSS KiB | FDs | CPU tick delta |
|---|---|---|---|---|
| sbbase-music | 2698/7738883 | 2468-2468 | 64-64 | 344 |
| jive | 3192/55166 | 33236-33236 | 50-50 | 575 |
| watchdog | 639/923 | 1748-1748 | 2-2 | 2 |
| sbproxy | 3282/56496 | 892-892 | 16-16 | 0 |
| sbwebserver | 3312/56733 | 452-452 | 16-16 | 18 |

The target lacks getconf; idle CPU is reported as raw /proc user+system ticks,
without claiming a verified tick-rate conversion. RSS is whole process memory,
not incremental library overhead. Base includes inherited descriptors from the
established GUI launcher (including an unused framebuffer FD); its 64 count was
stable, not a claim of only scanner-owned descriptors. No ALSA/decoder ownership
was requested. Separate FD hygiene/handoff qualification remains relevant before
audio. The metadata helper closes inherited nonprotocol descriptors.

MemFree ranged 9932-9996 KiB, with substantial filesystem cache. No memory-limit
failure, new server, process duplicate or unexpected exit was observed. TinySC
stayed absent and lifecycle fresh. The factory watchdog retained its own FD1
/dev/watchdog and original PID/configuration; it was never stopped/reconfigured.
USB sda1 counters were identical in all five samples: six writes, 29 sectors.

Raw evidence: observation.json, performance-summary.json, native-fds.txt,
scan-and-browse.log, protocol-summary.json and protocol/*.json under .local/wp4a2d.
No reboot, audio/decoder, large-library or physical offline stress benchmark.

Follow-up 2026-11-05: the native Music binary was replaced with the expired-Comet-
client correction. The old session/database was preserved after shutdown and a
new bounded session started. Current hashes, PID, scan measurements, RAM usage,
activation evidence and the additional rollback prerequisite are recorded in
[the dated Jive follow-up](STANDALONE_MUSIC_WP4A2D_JIVE.md#reconnect-correction-deployed-2026-11-05).
Earlier measurements/hashes here describe the original 2026-11-04 session.

Latest follow-up: stock SlimBrowser useContextMenu parsing is corrected and
deployed (PID 3933; scan 4561 ms; three preserved/current 120 KiB RAM DBs). See
[the second dated Jive follow-up](STANDALONE_MUSIC_WP4A2D_JIVE.md#stock-slimbrowser-parameter-correction-2026-11-05)
for current hashes, validation, previous-session preservation and rollback.
Physical category visibility passed the owner's first follow-up; filled lists
and navigation await the next owner check.

Final owner observation: filled Artists -> Fleetwood Mac -> Rumours -> 11 tracks
is confirmed on the physical Touch after the two fixes. See the Jive report for
the exact confirmation scope; audio remains disabled.
