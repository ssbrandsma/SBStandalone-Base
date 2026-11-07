# Base-only recovery design and unresolved integration

This is a design assessment, **not an implemented or qualified coordinator**.

## Actual current supervision

The previously captured deployed applet and freshly read on-disk copy use fixed
service paths under `/usr/share/jive/applets/StandaloneBase`. `launch()` executes
the service wrapper, waits one second and publishes its PID. `alive()` checks that
PID's command-line prefix against the canonical service path. The processes are
currently orphaned to PID 1; parentage does not itself identify their launcher.
Historical startup evidence and the applet source establish the Jive launcher.

`supervise()` polls every 15 seconds and schedules up to five launches with
increasing backoff. It also invokes the existing local-server reconnect logic.
Its `stop()` helper is local; `free()` cancels timers and stops Base, web and proxy.
The applet meta registers storage/image/artwork services, no Base maintenance RPC.
No supported live Lua reload or injected Lua control path was used.

Launching an immutable generation with its normal generation-path argv[0] would
fail the existing canonical-prefix liveness check. A native compatibility launcher
could retain the canonical argv[0] while executing immutable bytes, but argv is
then compatibility data only. Independent live FD/hash/maps proof remains
mandatory. That launcher, one-shot selection, races with scheduled timers and
bounded rollback have not been implemented/tested. Merely copying a new launcher
or changing a PID file is not a verified recovery transaction.

Base is not the watchdog's monitored Jive process. A Base-only design could leave
Jive and watchdog IPC intact in principle. This observation does not qualify the
missing supervisor adapter or prove future reconnection. The existing native
coordinator explicitly transfers the watched PID and launches Jive; do not reuse
its mutation command for this assignment.

## Required implementation before admission

1. Add strictly browse-only existing-DB reopening through the existing WP1 worker
   with read-only/create-disabled options, schema compatibility and WP6a source/
   generation checks. No scanner, migration, decoder/owner/resource construction.
   A check-only entry must validate this without starting competing listeners or
   acquiring TinySC ownership.
2. Qualify two source-bound immutable compatible artifacts, including a separately
   verified rollback. An old-source artifact with a new reopen patch is a new build
   requiring testing, not automatically a known-good rollback.
3. Provide a tested fixed-path supervisor adapter for the already running applet.
   Preserve Jive, web/proxy and watchdog; bound each candidate/rollback launch to
   one attempt and fence pending automatic service retries.
4. A detached Fab4-profile-only native actor must correlate the precise legacy
   PID/start/boot/parent, listener inodes, health, applet evidence and library owner.
   Call this maintenance target evidence, never executable LEVEL 1/2. No generic
   hash bypass or arbitrary path input may be introduced.
5. Pin authorization and source-bound receipt; lock concurrent operations; validate
   both artifacts and real Jive/feeder/countdown evidence before shutdown. Verify
   old PID/start death and release of listeners before a new launch. Do not infer
   exit from a PID-file change or escalate an uncooperative old process silently.
6. New process admission requires live `/proc/PID/exe` hashing plus independent
   mapping correlation, stable PID/start, unique listeners, health, HELO/CometD
   reconnection, 11-track browsing, TinySC absence and unchanged Jive/watchdog.
7. On primary failure, preserve logs and make one qualified rollback attempt with
   the same no-overlap and health conditions. No Jive restart or automatic retry.

Host tests must exercise real process/FD/socket and installed-supervisor semantics
as well as fault fixtures. QEMU cannot establish old UnionFS proc behavior. All
three gates remain unpassed; no serialized action plan is activation-qualified.
