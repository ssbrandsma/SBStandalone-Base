# WP4a.6 — Playback state machine (DRAFT)

## Observed live lifecycle (build 8, 5/5 identical)

```
IDLE (owner=0, decode 0/0, flag down)
  → tap track → CONNECT (music header) → observer latch (flag up)
  → strm-s (epoch N, owner gen M) → STMc/STMs → PLAYING (decode 1/1)
  → ~6 s: Jive FIN (pbStream=nil) → transient FLAG-OFF (race)
  → grant sync → re-latch (owner=4, flag up) [build 9 holds through]
  → drain → STMc/STMs → post-drain strm-q → STOPINTERNAL
  → decode 0/0 → RELEASE-IDLE (>3 s) → owner=0 → flag clear
  → IDLE
```

Event→transition map verified per epoch: latch on music CONNECT,
grant on owner==4 snapshot, hold across FIN, release on sustained
idle, clear on owner-loss+idle. No transition inferred from UI or
HTTP alone; every step has ring/log/decode triple evidence.

## Transient states

- Grant race (every play, build 8): `FLAG-OFF reason=disconnect`
  between FIN and first snapshot sync; self-heals <1 tick; no
  spurious release (fail-safe: `maybeRelease` only fires when
  granted+inactive+quiet+aged). Hardened in build 9 (decode-busy
  hold) — morning ring should show zero transients.
- Pause-hold: `pauseHeld` + `pauseEpochs` set on forwarded pause;
  held across disconnects; cleared on resume or stop-while-paused
  (host-verified K8/L2; live resume untested — no strm-p).
- Standby: decode suspended (`decS=1/decA=0`), grant HELD, flag up;
  audio resumes on next `strm-s` (verified: post-standby play →
  `decA=1`, VU dances). Standby is suspend, not stop/release.

## Fence introspection (stable contract)

- `/tmp/standalonebase/fence.version`: `version=6 build=N installed_at=…`
- `/tmp/standalonebase/fence.status`: full snapshot each tick +
  transition (build 10 adds `radioOwnsPb/radioActive/radioPaused/
  radioPending/radioTimer`).
- `/tmp/standalonebase/fence.streams`: 20-line ring (CONNECT,
  DISCONNECT, STOPINTERNAL+traceback, FLAG-OFF+reason, RELEASE-IDLE;
  build 10 adds RSTOP/RPAUSE/RBEGIN/RSCHED/PPAUSE).
- `/tmp/standalonebase/fence.cometd` (build 9+, temporary): last
  pause-POST id/err/reply — the pause root-cause instrument.

## Failure states (observed)

- Pause-no-op: fence paused-epoch vs decoder playing (divergent
  state; UI shows pause icon? No — UI kept playing: consistent
  with decoder, fence epoch stale until resume/stop clears it).
- Radio action death: pause action consumed-without-effect (cause
  pending build-10 ring).
- No stuck-grant, no stuck-flag, no duplicate-stream, no leaked
  HTTP capability observed all night (5 plays + 1 radio session).
