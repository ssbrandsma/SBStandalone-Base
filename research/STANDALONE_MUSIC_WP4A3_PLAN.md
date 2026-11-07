# WP4a.3 controlled physical playback: execution plan

Owner authorized the next phase on 2026-11-05. Only Fab4 192.168.1.244 is
authorized; .245 is the Radio and is excluded. No new release/default activation,
USB writes, watchdog disabling or unqualified restart is part of the plan.

The current source and WP4, WP4a.1, WP4a.2, WP4a.2b and WP4a.2d reports were
reviewed. Current native browse PID 3933 has an intact 11-track RAM library.
Jive 3192, watchdog 639, proxy 3282, web 3312 and boot UUID remain unchanged.
TinySC is freshly STOPPED_VERIFIED/count zero; Now Playing is inactive/stopped.

Playback entry gates still require concrete production Lua bindings, independent
decoder stop evidence, native resource IPC on the existing web listener and
qualified producer fencing. Process absence or metadata stopped is not ownership
proof. Spotify's asynchronous production stop/restart-fence driver is explicitly
missing; handoffs cannot be advertised merely because the host contracts pass.

First resolve the restart-preflight mismatch using read-only observations, then
host negative tests and a fresh static ARM build. Sixteen real-device semaphore
snapshots show two active registrations owned by Jive, both with positive
countdowns, in the stock 20-semaphore set. The old `ui == 1` predicate rejects
this healthy profile. Firmware watchdog decrements countdown semaphores, so their
GETPID identifies the last writer, not registration ownership. Allocation slots
are the appropriate registration evidence. A corrected preflight must require
the two observed registrations and retain process/configuration/descriptor and
expiry checks. Post-start challenge-bound UI evidence remains mandatory and
separate; a preflight pass alone does not qualify a restart or prove decoder idle.

Only after qualification: prepare exact reversible native/Lua/resource bindings,
preserve the completed browse database after ending its session, establish all
required ownership proofs, then attempt one MP3 with owner audible confirmation.
Stop/release must qualify before queue/handoff tests. FLAC, additional outputs,
Radio/Spotify transitions and failure cases follow only where their entry gates
are actually implemented and passed. Keep unsupported paths unavailable.

Evidence starts in `.local/wp4a3`: hardware identity/runtime/storage/watchdog
captures, `semaphore-samples.json` and `semaphore-actors.txt`. The temporary
read-only helper was hash verified, invoked only for `sem`, then removed. No
service, decoder, watchdog device or IPC value was changed by those observations.
