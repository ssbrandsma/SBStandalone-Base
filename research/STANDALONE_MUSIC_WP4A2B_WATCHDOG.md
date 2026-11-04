# WP4a.2b watchdog coordination

Status: native coordinator passes host tests; Fab4 preflight refused installation.
No controlled restart or reboot was attempted. This is not hardware qualification.

## Preserved supervision

The factory watchdog remains the sole process opening/feeding `/dev/watchdog`.
Its interval 5, startup delay 20, repair script, PID path and SysV key 1234 are
unchanged. The helper never opens the watchdog node, changes countdowns, allocates
keepalive slots or starts another feeder. The previous watchdog reset is confirmed;
why the earlier Jive vanished remains unresolved. SSH lifetime is only a hypothesis.

## Coordinator

`launch-restart` starts a detached setsid coordinator with SIGHUP ignored and all
inherited SSH descriptors closed. A restart lock serializes maintenance. Preflight
checks the exact firmware/configuration, Jive PID/start time, one watchdog process
and its actual writable hardware descriptor, and the known 20-slot IPC layout
with nonexpired allocated countdowns.

After preflight and fixed action-script checks, compare-and-replace transfers the
watched PID from verified Jive to the live coordinator. The extended stock
`maintenance-stop` preserves this PID marker. Unmodified stock stop after transfer
would kill the wrong actor and is forbidden. Old Jive, decoder children and three
Base helpers are identified before stopping and checked by start times. Duplicate
or foreign helpers refuse work.

The coordinator directly parents replacement Jive and records actual exit/signal
in RAM `jive.exit`. Jive receives the qualified factory environment and working
directory in its own session. Stdout/stderr go to `/dev/null`; no new logger is
introduced. Existing firmware logs remain available.

Health requires an advancing challenge-bound Lua UI witness, matching live Jive,
current IPC evidence, three unique fresh Base helpers, TinySC absence and current
Base lifecycle health. A successful shell exit or PID presence cannot prove
health. Only verified health transfers supervision back to Jive. Temporary
challenge/identity files are removed; passive status/failure evidence remains.

Phases have monotonic deadlines. Hung action groups are terminated/reaped.
Recovery is attempted once: restore a genuinely advancing original UI if possible,
otherwise known-good Base files and one replacement Jive. Recovery success still
fails qualification. Failed recovery leaves FAILED_HELD: an explicit live
maintenance actor remains supervised pending verified owner recovery. It does not
fabricate UI or refresh IPC. Older `hold` primitives are not the restart procedure.

## Actual refusal

The production helper ran from temporary RAM and returned:

`restart preflight refused: missing/stale Jive UI or feeder keepalive evidence`

Earlier checks, including unique feeder/descriptor and IPC layout, passed. This
last condition combines `ui != 1` with Jive/feeder identity revalidation; the output
does not distinguish the failing predicate. Read-only follow-up found original
Jive 3192/start 55166 and watchdog 639/start 923 with unchanged boot UUID.
No failing semaphore value/GETPID snapshot was retained. The exact mismatch is
unresolved. GETPID is last-writer evidence, not permanent ownership identity.
Further investigation must inspect real firmware writer/thread semantics and
sampling. Host fixtures do not prove them on the 2014 kernel.

The gate was not bypassed or weakened. Installation stopped before overlay
replacement, and the RAM helper was removed. No second attempt was made.
