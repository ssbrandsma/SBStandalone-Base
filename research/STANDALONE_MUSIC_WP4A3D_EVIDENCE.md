# Evidence index and provenance

New private observations are under `.local/wp4a3d/`; prior phases were preserved.
The working tree already contained uncommitted WP4a.3b/c changes; they were not
reset, committed, pushed or deployed by this phase. Source HEAD is `2b666ba`.

| File | Meaning |
| --- | --- |
| observe_baseline.py | Exact local read-only collection script; fixed .244 strict SSH transport |
| before.txt / after.txt | Boot, diskstats, mountinfo, TCP/UDP tables, IPC-set listing, watchdog configuration, capacity and DB listing |
| processes-before.txt / processes-after.txt | Actual matching process stat/cmdline/exe/FD inventory; contains NULs from cmdline |
| process-summary.json | Host-derived stable process/start/RSS observations |
| base-proc.txt | Base stat/maps/status and failed direct live-executable read |
| health.json | Current Base health, TinySC lifecycle and inactive Now Playing |
| browse.json | Original-protocol server/player and all five browse responses with timings |
| library.db / database.json | Private host copy and read-only SQLite schema/integrity/source/generation evidence |
| installed.json | Fresh installed artifact SHA-256 and sizes, not live executable proof |
| sbbase / StandaloneBaseApplet.lua / StandaloneBaseMeta.lua | Freshly read installed launcher and applet sources |
| observation.json | Successful bounded observation summary and explicit unmeasured gates |
| regression.txt | Exact existing host regression output |
| regression-root.txt | Five privilege-dependent maintenance groups rerun under WSL root; 5/5 pass |
| readiness.json | Explicit incomplete/refused admission, no authorized transition dispatched |
| provenance.json | Host source/evidence hashes and collection timestamp, when present |

Source review covered all nine prior reports requested in §3, actual Base runtime
and CLI/startup, deployed applet launcher/supervisor/meta, `music_session.c`,
maintenance helper/coordinator/watchdog/identity/internal-file policies,
`music_generation.py`, `fab4-lifecycle.py`, qualification/rollback tooling and
WP1's read-only library support. Reopening capability in WP1 does not imply that
Base's current startup exposes it.

No immutable candidate, independently qualified rollback receipt, native recovery
coordinator, installation transcript, new process identity or replacement-level
performance evidence exists. Do not infer those deliverables from prior-phase
fixture results. WP4a.3d remains incomplete and playback admission remains closed.
