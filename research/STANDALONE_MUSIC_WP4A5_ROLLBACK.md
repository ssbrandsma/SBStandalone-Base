# WP4a.5 — Rollback: tiers, proof, and failure coverage

## Tiers (as executed)

- Tier 0 — old-intact: failure before the old base stopped → kill only the
  pre-launched web, re-verify old base + pidfile, return it. Hardened this
  session: intactness is snapshotted BEFORE any kill (old and new share the
  artifact path, so a blind kill would murder a healthy old base — found live
  in round 6, fixed, verified in round 8's pre-stop abort).
- Tier 1 — same/new binary, Rumors library, no-scan reopen: the workhorse.
  5/5 landings this session, every one guard-verified (LEVEL-exempt browse
  tier, TinySC, nowplaying idle, FIXED processes, listeners).
- Tier 2 — supervisor core without music: honest last resort, never needed.

## What rollback never does

No full scan, no database recreate, no Jive restart, no watchdog change, no
TinySC start, no reboot. Schema downgrade never arose (no migration this WP);
the pre-existing snapshot remains the fallback.

## Failure injection (real, not simulated)

Exercised live this session: failed pre-guard (round 6 cmdline, round 8 port),
pre-stop abort (old-intact path ×2), post-commit operator rollback (×2),
supervisor-racer sweep (every round), stale-log truncation (added after
round-5 residue analysis), wrong-generation/LEVEL mismatch asserts (all armed,
none tripped wrongly). SSH stayed up throughout; every script is re-runnable
and idempotent (add-only staging, receipts appended).

## Gaps (honest)

Legacy scoped-scan tier-2-music is unsupported (legacy binary predates
`--music-open`; documented in staging receipts). Mid-cutover SSH loss was not
fault-injected (session held); the design (pidfile fence, tier sweep loops)
covers it by construction. Rollback evidence:
`.local/wp4a5/rollback/manifest.json`, `cutover*/receipts.json`,
`gatef-attempt1/rollback-tier1.log`.

## End state

No rollback is pending: the device runs the fixed playback base (32147,
LEVEL 1) by owner choice. Tier-1 remains one `--rollback-only` away.
