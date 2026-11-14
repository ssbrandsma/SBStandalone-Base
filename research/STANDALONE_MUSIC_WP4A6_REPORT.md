# WP4a.6 — Stable native MP3, exclusive ownership & Jive transport (DRAFT)

**Status: partial-complete, one morning live block remains.** Test1 (full-track
play) passed 5/5 with clean release. Pause root cause narrowed to two
candidates with a decisive hardware diagnostic staged. Radio plays; its
pause-action path fails (diagnostic staged). Mid-play track switch is
unproven (portal tap ambiguity). No native code changed; no restarts.

DRAFT: pause/radio/switch verdicts + profiling finalize after the morning
build-10 install (one ImageViewer tap) + owner audible confirms.

## Gate results

- Baseline preserved: pass — 273 tracks/rev21, LEVEL1 base 10907,
  web 10903, Jive 773, watchdog 640, TinySC stopped, USB untouched.
- Exclusive fencing (music side): pass — 5 plays, 0 radio/Spotify takes.
- Test1 full-track + natural end + release: pass 5/5 (epochs 15–19).
- Test2 pause: FAIL (forwarded, no strm-p) — narrowed, diag staged.
- Test2 resume/stop/switch: UNPROVEN (no working stop UI; taps ambiguous).
- Test3 radio regression: partial — plays 18+ min; pause-action fails.
- Profiling: deferred to morning (method + idle baseline ready).

## Final summary (mission items)

1. Radio auto-resume cause (WP4a.5): desiredStation + reconnect timers fired
   while music owned the decoder. Fenced in V6 (wraps + grant tracking).
   The 6 s "resume" seen tonight is NOT a timer: tapping the playing
   station RESTARTS it (stop+play); code audit proves no timer survives
   an explicit stop (reconnect cancelled, watchdog/audio gated).
2. Production changes: `applet/PlaybackFenceV6.lua` builds 8/9/10 (8 live,
   10 staged), `tests/music/test_playback_fence.lua` Phase O (grant-race),
   `scripts/wp4a6-fence-v6c.py` (alternate Spotify carrier, used + restored).
   No native changes. No music-file, DB, or firmware changes.
3. Ownership guarantees: one owner enforced at the SBOWN1 bridge; fence
   flag tracks the GRANT (owner==4), not the HTTP stream (which FINs in
   ~6 s while decode runs minutes); release after >3 s decoder idle;
   flag clears on owner-loss+idle; foreign-owner surrender (build 9+).
4. Play/Pause/Resume/Stop: play 5/5 from UI; pause forwarded but decoder
   unaffected (no strm-p ever emitted); resume/stop/switch unproven.
5. Complete MP3 audible: owner confirmed audio works (evening session);
   tonight's 5 tracks played to drain with VU/progress/FW events.
   Morning: one owner audible confirm on build 10.
6. Natural completion + release: 5/5 (strm-s → STMc/STMs → strm-q →
   RELEASE-IDLE → owner=0, flag clear, decode idle).
7. Radio still works: yes, plays (538, 18+ min). Stop paths degraded
   (pause-action dead; station tap restarts). Under diagnosis.
8. Spotify: preserved, stopped, never interfered. Applet file restored
   pristine (md5-verified) after carrier attempt. Daemon never started.
9. CPU/RAM: overnight deltas only (Jive +18 min CPU/2.7 h, base +10 min;
   load 1.2–1.8). Windowed profiling runs in the morning.
10. Host/ARM/QEMU/sanitizer: fence suite PASS (K/L/M/N/O), luac clean,
    ctest 46/50 (4 known fails; music_backlog_drain flaky, fails alone).
    ARM/QEMU/sanitizers: not re-run (Lua-only change; prior ARM fab4
    build still valid).
11. Generations: base sbbase-music.61 pid 10907, web pid 10903
    (unchanged all night, LEVEL 1 intact, no redeploy).
12. Rollback: receipt + backups intact; Spotify restored pristine;
    ImageViewer splice still staged (build 8→9→10 chain); device left
    in standby, fence idle, radio suspended (silent).
13. Blockers: (a) build-10 live install needs one ImageViewer tap
    (portal cannot reach it); (b) pause strm-p cause (diag decides);
    (c) radio pause-action cause (diag decides); (d) switch verdict
    needs reliable input (owner remote) or base-side proof.

## Morning block (est. 45 min)

1. Owner opens ImageViewer once (ask which menu path!) → verify build=10.
2. Play track → pause → read fence.cometd (POST err? base reply?) → fix.
3. Radio pause → read ring RPAUSE/RSTOP + radio state → fix.
4. Owner-remote switch test (mid-play track change) → verdict.
5. Windowed profiling (idle/play/pause/resume/stop) + audible confirms.
6. Strip diags → final build → verify → finalize reports → commit.

Evidence: `.local/wp4a6/tonight-20261110/` (status, ring, strm timeline,
receipt, radio applet export, screenshots). Staged deploy: build 10
`9e9f34ec64fa` on device, awaiting install.
