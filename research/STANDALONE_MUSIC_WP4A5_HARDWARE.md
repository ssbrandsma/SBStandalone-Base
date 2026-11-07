# WP4a.5 — Hardware: Touch validation and preservation

## Device (revalidated throughout)

Fab4 Touch 192.168.1.244, firmware 7.7.3 r16676, boot
`9162ba64-822a-46e1-8ab5-2ea3c6a66740`, uptime 76+ h, no reboot all session.
Final: base 32147/31252941 (LEVEL 1, fixed bits `da86f374…`), music web paired
on :8766, full library live.

## Preservation (all verified, pre/post every step)

- Jive 3192/55166, jive_alsa pair, watchdog 639/923, sbproxy 3282/56496,
  sbwebserver 3312/56733 — identities unchanged; no Jive restart/reboot,
  watchdog never weakened (FIXED asserts in every cutover/rollback).
- TinySC `STOPPED_VERIFIED`, count 0, fresh, throughout (every guard set).
- Radio/Spotify/Qobuz installs untouched (no file changes; no handoff code).
- USB user files untouched (`find -mmin` empty after all activity); snapshot
  mtime frozen; DB writes only in `.standalone-music` + RAM.
- No competing listeners at any commit point (sweeps + asserts).

## Radio auto-resume finding

After attempt 2's stream ended, Jive's Radio applet auto-resumed an internet
station (direct Jive-held HTTP to 145.58.50.40:80) and preempted the decoder;
SlimProto then flapped repeatedly (conn 2→6+) until the owner tapped Stop,
after which the socket closed and the device returned to idle. Per the
mission, cross-application fencing was explicitly out of scope — but the
mechanism (external preemption + reconnect churn) is now evidenced for the
handoff work package. No Radio/Spotify code was modified.

## Figures

Base RSS ~1.9 MB, web RSS ~0.4 MB (post-test idle); system load 0.7–1.3;
/dev tmpfs 62% (stage dirs accumulate ~2 MB per round — housekeep next WP).
Playback-window CPU/RAM was not sampled (gap, honestly noted); idle figures
above + scan figures (166 s full scan) stand in.
