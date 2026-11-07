# WP4a.5 — Ownership: production idle admission

## Protocol (as exercised)

`SBOWN1 <op> music incarnation seq gen conn …` over authed HTTP
(`X-Music-Ownership` token; even `hello` 403s without it). Per-owner adapter
sequence is server-global: fresh clients must resync from `hello`'s `sequence`
(the Gate F harness initially 409d on this until resync was added).
`register` → `admit-idle` (masks 14/12, bridge proof) → `query` refresh.
Incarnations expire after 45 s without `query` (`MUSIC_OWNERSHIP … timed-out`,
`idle_admitted` cleared). Reconnect flaps clear `baseline_idle`; re-admit with
current gen/conn recovers (cutover soak tolerates ≤3 flaps; all commits saw 0).

## Device facts

- Every commit admitted (incarnations 1–4 across runs) with `idle_only`,
  `unavailable_mask=14`, `startup_native_absent_mask=12`.
- No active Radio/Spotify/Qobuz producer or grant at any admission; decoder
  quiescence came from the live SlimProto connection (STMt heartbeats).
- The stuck-grant case: after attempt 2, `release` with a flapped connection
  correctly 409d (generation check working as designed); the grant later
  expired via the 45 s adapter timeout. No fabricated ACKs anywhere.

## Cross-application note

Fencing is one-sided: Music fences itself, but Radio (in-Jive Lua, direct
HTTP decode path) does not honor Music ownership — it auto-resumed and
preempted the test stream after ~33 s of playback (see AUDIO/HARDWARE).
Out of scope for this WP by design; recorded as the top follow-up.
