# WP4a.4 — Rollback boundary
Qualification date: 2026-11-06. Target: owner-designated Fab4 at 192.168.1.244 only.
This package stops at database/scanning. No audio playback, application handoff,
Jive/watchdog restart, reboot, remount, firmware change or active Base replacement.
Raw local receipts: `.local/wp4a4/`; source/artifact SHA-256 receipts are
`source-provenance.json`, `artifacts.json`, and `staging.json`.

The active Base PID 3933 was not replaced. Jive/watchdog/proxy/webserver and the
original RAM database remain the baseline. Original DB bytes are saved locally
as `.local/wp4a4/original-ram-library.db` (122,880 bytes), SHA-256:

`7ef9c34cf2e63fe53904af50eb8ff21d3c02f259fa25b342af671f08f314d006`

The new production Base executable has a non-serving `--music-open-check` mode
that opens this existing schema-3 RAM file read-only with explicit legacy source
ID `3dc256682c8d3c420100000000000000`. This mode returns before lifecycle policy,
listeners, scanner or ownership initialization. It also validates persistent
store open using the same production worker path. Physical results are in the
hardware report. It does not qualify replacement of the running LEVEL 3 Base.

Persistent rollback is copy-based: retain exported snapshot bytes and checksum
receipts, validate with native `integrity/open`, and use explicit `restore` only
when two valid committed generations exist. Do not copy a raw SQLite payload
over a snapshot envelope or make an old runtime adopt a different media identity.
The legacy runtime cannot directly consume snapshot envelopes; it must use the
preserved RAM baseline or a separately qualified compatible runtime.

Only exact, finished, private-RAM helper/job paths may be cleaned after receipts
are collected. Keep USB snapshots and host evidence. No rollback requires Jive,
watchdog, factory boot code, TinySC content, audio files or partition changes.

**Still unqualified:** automatic Base-only supervised replacement and rollback
of active LEVEL 3 executable identity. No such transition was attempted. A later
package must qualify it before the complete persistent catalogue can serve the
physical My Music UI. Native playback remains out of scope.

## Final cleanup

All known finished private-RAM staging executables and job receipts were removed
after local collection (`cleanup.json`). No USB content was removed; the dedicated
USB directory remains empty. `hardware-final-audit.json` verifies unchanged original
RAM database SHA-256, unchanged service starts, stopped TinySC and inactive audio.
The owner confirmed smooth menu/My Music navigation. No native playback was run.
