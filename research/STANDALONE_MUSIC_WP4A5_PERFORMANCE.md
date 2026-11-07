# WP4a.5 — Performance

## Scanner (historical, Gate B)

1376 entries / 273 tracks in ~166 s on-device; peak RSS and CPU were within
the 32 MiB DB budget and single-core comfort (prior report). No rescan was
needed or run this session.

## Runtime footprints (this session, Fab4)

- Base RSS ~1.9 MB, music web RSS ~0.4 MB (post-test idle).
- System load 0.7–1.3 through cutovers, playback, and rollback.
- Library open: instant (`--music-open`, no scan); 273-track page loads are
  sub-second server-side (harness CometD reads are dominated by streaming
  channel hold-open, not server time — measured and accounted).

## Timing observations (new)

- Admission→play-action propagation takes seconds on-device (event-loop
  cadence, not instant). Harness polls with adapter refresh; do not assert
  immediate visibility.
- The ACK-timeout underflow made "instant" failures look like 15 s timeouts;
  with the fix, genuine IPC round-trips complete in milliseconds (trace shows
  back-to-back SEND/RECV lines).
- Cutover wall time ~4 min (45 s supervision + 60 s soak dominate); rollback
  ~30 s. No Jive-visible interruption beyond a SlimProto re-affirmation.

## Budgets respected

No new threads in steady state (base event loop + existing workers; web gain
is the pre-existing reader thread), no DB growth during playback, no USB
writes outside the store (zero observed), /dev tmpfs stable at 62%.
