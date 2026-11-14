# WP4a.6 — Jive transport controls (DRAFT)

## Play (PASS)

5/5 plays from My Music → Tracks via UI (Fame, Culture Club, ABBA,
FYC, ABBA-2nd): spinner → Now Playing (correct title/artist/VU) →
strm-s → decode 1/1 → progress advances. Epochs 15–19, owner gens
39/41/43/45/47. No duplicate streams from repeated UI.

## Pause (FAIL, narrowed)

- Fence side works: pause press → `lastAction=pause` fresh →
  `playerPause` → Bayeux `/slim/request ["pause",1]` POST to base
  :9000 (`cometdSeq` 1→2), `pauseHeld` set, `pauseEpochs` tracked,
  fencing intact (music still owned, no `stopInternal` murder).
- Decoder side dead: zero `strm-p` in 770 KB+ base log; decode stays
  1/1; UI time advances. No `STMs`-pause/`STMp` events.
- Base log has exactly ONE "pause" line in 5 h: my own malformed
  wget probe. 46 COMET RX, all `/meta/*` (music-pump requests are
  silent by design, so arrival is unobservable in logs).
- Code audit eliminates: parser (pause ∈ supported → OK), queue
  context (`music_allocate` snapshots live context → gate passes),
  transport state (STMs → PLAYING steady-state; `q` proves stream
  alive), `single_mp3` (0 live — multi-play works).
- Remaining candidates: (a) Jive `SocketHttp` POST never arrives;
  (b) post-flap generation mismatch in `sb_owner_controls`/`id_equal`
  silently rejects ALL owned mid-play commands (`p`/`u`/`q`).
  NOTE (b) predicts user-stop/switch also dead — consistent with
  tonight's unproven switch (below).
- Decisive instrument staged: build-10 `fence.cometd` captures the
  POST error + base reply (`{"count":0}` vs
  `unsupported_or_unavailable_music_control` vs transport error).

## Resume / Stop (UNPROVEN)

Never observed a `strm-u` or user-driven `strm-q` (only post-drain
`q`, which bypasses ownership). Touch Now Playing has NO stop
button; viable stop paths: next-track switch (unproven), standby
(suspends, doesn't stop), radio takeover (blocked while owned).
If (b) above holds, stop also fails → transport deadlock until
track end. Morning: test + fix together with pause.

## Switch (mid-play track change): UNPROVEN

0/10 track-row taps acted while audio played (music or radio) vs
4/4 tracks eventually playing from idle. Portal taps are lossy
(~60%, lag 2–15 s, bottom-row dead zone), so tap loss cannot be
excluded — but the idle/playing split is stark and matches theory
(b): switch needs `control('q')` first, same gate as pause.
Morning: owner physical-remote switch test (reliable input) OR
base-side proof. If (b) confirmed, one base fix heals pause,
stop, and switch together (native redeploy with LEVEL 1).

## End of track (PASS 5/5)

Drain → STMc/STMs → post-drain `strm-q` → STOPINTERNAL → decode
idle → RELEASE-IDLE → owner=0 → flag clear. UI returns to stop.
No auto-advance, no radio restart, no stuck state. See STATE_MACHINE.

## Files

`native/sbbase/src/music_playback_glue.inc` (pause branch),
`native/sbbase/src/music_playback.c` (`sb_playback_control`,
`control`), `native/sbbase/src/slim_controller.c`
(`sb_transport_owned_command`, `sb_transport_command`),
`applet/PlaybackFenceV6.lua` (`playerPause`, cometd transport).
