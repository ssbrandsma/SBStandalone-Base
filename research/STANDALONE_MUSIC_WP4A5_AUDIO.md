# WP4a.5 — Audio: first real MP3 playback

## Attempt 1 (base 13052): failed before any audio

`playlistcontrol cmd:load track_id:46` → `resource_ack_timeout` at
ACTIVATING. No strm-s, nothing audible (nothing could be). Root cause found
via attempt-2 instrumentation (below): the ACK-timeout check
`now - sent_ms > 15000` uses unsigned arithmetic with `now` sampled at tick
start and `sent_ms` sampled mid-tick; a millisecond straddle makes
`now < sent_ms` and the subtraction wraps to `2^64-1` — an *instant* 15 s
timeout. Attempt 1's ACTIVATE (~10% per-message chance) hit it.

## Attempt 2 (base 12920): audible success

- Load `{"count": 1}`; `MUSIC_STRM_SEND cmd=s epoch=1 owner=3 conn=1`.
- Decoder HTTP 200 on `/music/stream/…`; `np_state=playing`,
  `99 Luftballons _ Nena`, for ~33 s (jiffies 309954415→309987766).
- **Owner confirmed audible playback live.**
- Stream then terminated (firmware `STMf/STMc/STMs`, terminal state=7):
  Jive's Radio applet auto-resumed (direct Jive→internet HTTP to
  145.58.50.40:80, held on Jive fd 36) and preempted the firmware decoder —
  external preemption, not a pipeline failure. Repeated SlimProto
  disconnect/reconnect cycles (conn 2,3,4,…) followed while Radio played.
- Teardown RESET hit the same timer underflow (`now=309987805
  sent=309987806` in the log) → `FAIL … phase=8`, then `RECOVERED` to IDLE.
  The phase reads 8 because the terminal path sets FAILED silently first
  (`transport_failed`, line 971) — logging gap worth closing later.

## The fix (deployed on final base 32147)

One line in `music_playback.c`: guard with `now >= p->ipc_sent_ms`
(matches the 3 existing guarded call sites; audit found no other unguarded
music-path subtraction). Regression test in `tests/music/test_playback.c`
single-mp3 branch (crafted `now`): verified FAIL on unfixed code
(`now=1 sent=135405148` reproduces instantly), PASS on fixed.

## Stop behavior

Plain `["stop"]` is rejected (`unsupported_or_unavailable_music_control`) —
stop must go through playlistcontrol with context. Noted for the control
contract; the terminal/revoke path itself is traced and sound.
