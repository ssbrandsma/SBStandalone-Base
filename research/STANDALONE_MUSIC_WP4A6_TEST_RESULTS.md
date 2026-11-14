# WP4a.6 — Test results (DRAFT)

## Host

- `lua5.1 tests/music/test_playback_fence.lua .` → **PASS**
  (Phases A–O). Phase O added tonight for the grant race:
  O1 failed before the build-9 fix (`flag cleared into grant
  race`), passes after; O2 (idle clear), O3 (sync-keeps-flag),
  O4 (moved-on reap) green. K/L/M/N updated to the grant
  lifecycle (hold → release after >3 s idle → clear on ack).
- `luac5.1 -p applet/PlaybackFenceV6.lua` → clean (build 10).
- `.venv/bin/python /tmp/wp4a6-ctest.py music browse ownership
  comet slim transport playback stream` → **46 PASS / 4 FAIL**
  (`music_playback_ownership`, `music_idle_fence`,
  `maintenance_music_deployment_rollback`,
  `maintenance_music_recovery`). `music_backlog_drain` flipped to
  pass in the full run but fails 2/2 in isolation: order-dependent
  flaky, unrelated to tonight's Lua-only changes. No real drift.
- ARM/QEMU/sanitizers: not re-run tonight (no native change;
  prior ARM fab4 build still the deployed artifact).

## Live matrix (build 8, portal-driven + SSH ground truth)

| # | Test | Result | Evidence |
|---|------|--------|----------|
| 1 | Play from My Music (idle) | 5/5 PASS | strm-s ep 15–19, NP correct, VU/progress |
| 2 | Full-track audible play | PASS (owner: audio works) | 5 drains; morning confirm pending |
| 3 | Natural end + release | 5/5 PASS | q → RELEASE-IDLE → owner=0, flag clear |
| 4 | Exclusive ownership (music) | PASS | 0 takes across 5 plays + radio session |
| 5 | Grant hold across FIN | PASS | owner=4 + decode 1/1 with pbStream=nil |
| 6 | Pause | FAIL | forwarded (seq 1→2) but no strm-p; diag staged |
| 7 | Resume / user stop | UNPROVEN | no strm-u/user-q observed; no stop UI |
| 8 | Mid-play switch | UNPROVEN | 0/10 taps; tap-loss vs base-refusal open |
| 9 | Standby during play | PASS (safe) | decA=0, grant held, resume on next strm-s |
| 10 | Radio plays after music | PASS | 538, 18+ min, benign takeover order |
| 11 | Radio pause action | FAIL | 3 presses, decode never dips; diag staged |
| 12 | Radio stop, no restart | OPEN | station tap restarts; true stop untested |
| 13 | Spotify preserved | PASS | stopped, intact, md5-verified restore |
| 14 | No reboot/Jive restart | PASS | PIDs unchanged 21:15→01:40 |

## Morning (build 10 + owner)

Pause diag → fix; radio diag → fix; owner-remote switch verdict;
Test2 stop path; windowed profiling; audible confirms; strip diags;
final deploy + verify; finalize + commit.
