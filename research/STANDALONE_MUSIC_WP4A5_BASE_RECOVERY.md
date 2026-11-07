# WP4a.5 — Base recovery: safe Base-only replacement

## Transaction (all rounds)

Guards (boot id, FIXED Jive/watchdog/proxy/web PIDs+starts, TinySC
STOPPED_VERIFIED, nowplaying idle, artifact hashes) → pre-launch music web →
stop old base → launch versioned base → LEVEL 1 (`/proc/<pid>/exe` hash) →
ready/revision/health → 45 s supervision → ownership admission → 60 s soak →
commit. Any failure auto-rolls back (tier-1: same-or-new binary, Rumors
no-scan; tier-2: supervisor core, never needed).

## Round history (this session)

- Round 5 (prior): actually SUCCEEDED (LEVEL 1 + READY + rev21) but was rolled
  back by a harness hex typo checking port 8774 instead of 8766. The phantom
  "web cannot bind" investigation that followed is fully retracted.
- Round 6: rolled back pre-stop by a stale `sbbase-core` cmdline assert; the
  rollback itself exposed and then fixed a real bug (blind `^BASE` kill could
  murder an intact old base sharing the artifact — now snapshots old-intact
  first and kills only the web in that case).
- Round 6b: COMMITTED (base 13052, LEVEL 1, 0-flap soak). Attempt-1 load failed
  pre-audio (see AUDIO).
- Round 7: COMMITTED (base 12920, instrumented). Gate F success + audible.
- Round 8b: COMMITTED (base 32147, timer fix). Final state. Playback→playback
  needed the pre-check port assert dropped (old production web legitimately
  holds :8766; CUTOVER-02 sweep + re-assert retains coverage).

## LEVEL 1 evidence

`CUTOVER-05-level1` receipts record `/proc/<pid>/exe` sha256 = staged artifact
sha for 13052 (`ca359ccf…`), 12920 (`182a4719…`), 32147 (`da86f374…`).
Versioned immutable stage files + ROOT symlinks; add-only (old links intact).

## Rollback record

5 rollback landings this session (rounds 5/6/8-attempt + 2 operator), all
tier-1, all guard-verified. Rollback truncates stage logs first (no stale
READY match — added this session) and never rescans, restarts Jive, touches
the watchdog, or starts TinySC.
