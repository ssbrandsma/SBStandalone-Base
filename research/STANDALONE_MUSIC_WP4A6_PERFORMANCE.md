# WP4a.6 — Performance (DRAFT; windowed profiling in the morning)

## Overnight deltas (noisy, not windowed)

- Jive 773: 34:08 → 52:11 CPU over ~2.7 h wall (mixed idle/play/UI).
- Base 10907: 14:03 → 24:17 CPU. Web 10903: 01:54 → 03:16.
- Load average 1.2–1.8 during play+browse (load ≠ CPU%; see method).
- No RSS samples tonight; no per-process %CPU samples; no IPC/stream
  throughput numbers. These need the windowed run below.

## Morning method (Fab4, lightweight)

For each window — idle / play-start / steady-play / pause / resume /
stop-or-complete — collect 3 samples 10 s apart:

- `ps -o pid,etime,time,vsz,rss,comm` for 773/10907/10903/844
  (RSS + CPU-time deltas → %CPU per window; never loadavg).
- `/proc/<pid>/fd | wc -l`, `/proc/loadavg`, `free`.
- Base log markers: strm-s/q lines bound the windows; count
  STAT/s and HTTP bytes from web log for throughput.
- Fence `fence.status` `now` field correlates Jive clock to wall.

Keep each SSH sample <1 s apart-batch; no continuous polling
(embedded target). Report intervals + raw numbers, no extrapolation.

## Budgets (carry-over, verify in the morning)

Base RSS a few MB, web <1 MB, no fd growth across plays, release
latency (stop → owner=0) <10 s (tonight: ~5–8 s observed), no
reconnect storms (1 flap all night, reconciled).
