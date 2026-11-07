# WP4a.5 — Full library published, first audible MP3 achieved

**Outcome: complete.** The full USB library (273 tracks) is published and browsable
through the original Jive My Music interface, the playback-capable Base runs with
LEVEL 1 identity, and one controlled MP3 played audibly through the Touch hardware
(owner-confirmed). The device was left on a fixed playback base with no stale state.

## Gate results

- Gate A (scanner recovery): pass (prior session; re-verified read-only here).
- Gate B (library publication): pass — 273 tracks / 54 albums / 16 artists, rev 21.
- Gate C (SQLite lifecycle): pass — reopen without scan, unchanged scan stable.
- Gate D (replacement readiness): pass — qualified cutover + rollback (this session
  fixed a harness hex typo that had falsely failed round 5).
- Gate E (playback-capable activation): COMMITTED — base 32147, LEVEL 1, ownership
  admitted, 60 s soak with 0 flaps.
- Gate F (first MP3): pass on attempt 2 — track 46 (99 Luftballons), strm-s sent,
  HTTP 200 served, ~33 s of playback, **audibly confirmed by the owner**.
  Attempt 1 failed before any audio (root-caused to a timer underflow, fixed).
- Gate G (post-test health): pass on the fixed base — idle, TinySC stopped,
  Jive/watchdog intact, USB untouched, 273-track browse with play actions.

## Final summary (mission items)

1. Full-library scan: 1376 entries traversed, 273 tracks indexed, 1 recoverable
   malformed file (Dr Dre zero-prefix MP3, recorded, file untouched).
2. Valid library: 273 tracks, 54 albums, 16 artists, revision 21.
3. Reopen without scan: OK, integrity OK, stable IDs; unchanged scan: no reparses.
4. Replacement: versioned immutable artifacts, `/proc/<pid>/exe` hash proof,
   actual LEVEL 1 on every committed base (13052, 12920, 32147).
5. Ownership/resource IPC: production active — idle admission, authenticated
   token bridge, SCM_RIGHTS resource channel, :8766 streaming with ACKs traced.
6. First MP3: native SlimProto strm-s, decoder fetched and played the real file.
7. Audible: YES, owner-confirmed live during the test.
8. CPU/RAM: base RSS ~1.9 MB, web RSS ~0.4 MB, load ~0.7–1.3; scan 166 s (hist.).
9. Stop/release: stream terminated (firmware STMs), teardown RESET+ACK traced;
   ownership release path observed (409 on stale connection is by design).
10. Remaining blockers: none for this WP. Known follow-ups: cross-application
    fencing is one-sided (Radio auto-resume preempted the test stream after ~33 s
    — audible goal already met); plain `["stop"]` is not a valid music control
    (use playlistcontrol stop).

## What this session changed

- Fixed harness port-hex typo (`2246`→`223E` for :8766) that had murdered a
  healthy round-5 cutover and spawned a phantom "web cannot bind" investigation.
- Added IPC trace logging (`MUSIC_IPC_SEND/RECV/TIMEOUT/DISCONNECT`) — permanent,
  capability-safe (no tokens logged).
- Fixed ACK-timeout unsigned underflow (`now >= sent` guard, matches 3 existing
  call sites) + regression test `music_playback_single_mp3` (verified fail→pass).
- Hardened cutover rollback (old-intact snapshot before kill; log truncation;
  playback→playback support).
- Evidence: `.local/wp4a5/` (cutover6/6b/7/8b, gatef, gatef-attempt1, gateg,
  gatef-final, staging*, rollback/manifest).

## Acceptance criteria

All 17 criteria met (see sections above). No factory firmware changes, no music
file modifications, no Jive restart/reboot, watchdog never weakened, TinySC
stopped throughout, exactly two controlled single-track attempts (first died
pre-audio; second is the counted success), no FLAC/gapless/handoff work.
