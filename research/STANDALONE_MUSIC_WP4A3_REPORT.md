# WP4a.3 playback entry gates: partial, no audio activated

2026-11-05. Owner authorized proceeding to physical local playback on Fab4
192.168.1.244. Only that device was accessed. The working browse session remains
active. This phase is **not complete**: no physical playback ownership, decoder
ACK, native music stream or audible MP3 has been qualified.

## Implemented and verified

The earlier restart-preflight refusal has an identified cause. Sixteen read-only
snapshots of the real stock 20-semaphore set consistently showed two active
registrations owned by Jive PID 3192, each with a positive countdown. The prior
helper required exactly one registration by counting countdown GETPID values.
The installed watchdog's firmware patch decrements the countdown semaphores;
their GETPID is last-writer evidence, not registration ownership.

`native/sbmaintenance/watchdog_probe.inc` now checks the allocation semaphore's
GETPID and requires exactly the two observed Jive registrations for the existing
Fab4 7.7.3 r16676 profile. Expired countdowns, malformed allocations, foreign
registration ownership, process changes and invalid feeder/device/configuration
still refuse work. Its read-only status reports `jive_registrations:2` explicitly.
`coordinator.inc` uses the same registration rule during replacement health.
Independent challenge-bound UI advancement remains mandatory after a restart;
the registration check is not proof of UI responsiveness or decoder silence.

Host fixtures now model two registrations per Jive process. New negative tests
cover one/three registrations, foreign allocation ownership and expiry of the
second countdown. Another test proves that a different countdown last writer
does not invalidate genuine registration ownership. Existing missing/stale UI
witness and recovery tests still refuse qualification as designed.

The new static helper was temporarily staged in a private RAM directory, its
read-back SHA-256 verified, and invoked only for inspection. The real device
returned:

```json
{"actor":"restart_preflight","read_only":true,"jive_pid":3192,"jive_start":"55166","feeder_pid":639,"feeder_start":"923","watchdog_fd":1,"semid":0,"slots":20,"jive_registrations":2}
```

The helper was removed afterwards. No persistent installation was performed.
No launch/stop/hold/coordinate/PID-swap command was used. The watchdog device was
not opened or fed, IPC values were not modified and no decoder command was sent.

## Actual activation blockers

1. The existing restart recipe predates the current browse-only runtime/wrapper.
   Its read-only `verify-stack 3192 55166` returned **2** on the actual device.
   `base_role()` recognizes the original `sbbase` executable, not the current
   `sbbase-music`/`sbbase-core` arrangement. The source-bound old installation
   receipt also predates subsequent code and device changes. A preflight pass
   must not bypass stack validation or authorize use of that stale candidate.
2. The production Lua ownership modules are not constructed by the active Jive.
   `OwnershipBinding`/`NativeMusicOwnership` provide opt-in components; Base applet
   startup does not assemble their concrete profiles. The loaded legacy Radio
   hooks cannot simply be declared released. A safe activation path must arrange
   registration before those hooks and retain exact hook/decoder evidence.
3. Spotify's `ownershipStop` requires a correlated asynchronous driver returning
   both producer stopped and restart fenced for the requested operation. The
   current implementation explicitly refuses when that driver is missing.
   Process disappearance and the ordinary service stop timeout cannot substitute
   for this acknowledgement. Qobuz is absent; no Qobuz install was attempted.
4. Playback additionally needs connected native resource IPC with the existing
   webserver and a bound physical ownership manager. The current browse-only
   session deliberately constructs neither. No second Base/web listener may be
   launched alongside it to work around these prerequisites.

The prior hardware task requires stopping device-changing activation if no safe
activation path exists. Accordingly the current device was preserved. Work on
the above bindings and a rebased, fully validated recovery candidate remains;
this report is not a claim that physical playback is ready or that the complete
next phase has been delivered.

## Validation

- Native host build passes. Three relevant CTest groups pass in 17.66 seconds:
  maintenance primitives, 14 probe cases and 15 coordinator cases. The fixture's
  orphan-decoder count assertion was updated from one to two live replacement
  registrations; the first run exposed that outdated expectation.
- The final additional probe run, including the explicit status field, passes.
- ASan/UBSan: 14 probe and 15 coordinator cases pass in separate private IPC
  namespaces. The sanitizer-only build disables Ubuntu's default fortify macro
  to avoid a pre-existing ignored-write-result diagnostic in the child failure
  path; production ARM builds retain their normal flags and `-Werror`.
- Baby and Fab4 static maintenance builds pass with `-Werror`; neither has an
  ELF interpreter or dynamic dependencies. QEMU verifies the version, read-only
  20-slot IPC snapshot and production mutation refusal on a non-Fab4 host.
- No new full 103/104-case regression, production playback build, physical
  restart, reboot, ownership handoff, MP3/FLAC playback or audible test is claimed.

Final helper SHA-256:

| Target | SHA-256 |
|---|---|
| Fab4 | `67c0f8a959edffe924c286a120750fe697483240f3f10e6a29349f2e15313c51` |
| Baby | `8d7460996b0e1d2e49532d544efb96190d3f30849e24c52c5f79086ed96a3f8d` |

No physical Baby/Radio was contacted. The production mutation profile remains
Fab4-only; a Baby cross-build does not authorize operating the Radio.

## Preserved hardware state

Boot UUID remains `9162ba64-822a-46e1-8ab5-2ea3c6a66740`; Jive 3192/start 55166
and watchdog 639/start 923 remain unchanged. Base Music PID 3933 is still active,
with Now Playing inactive/stopped and TinySC freshly STOPPED_VERIFIED/count zero.
The three previous/current 120 KiB RAM databases remain intact in the dedicated
8 MiB tmpfs (384 KiB filesystem usage). USB completed writes/sectors remain
6/29. No USB content, mount settings, app settings or output/volume settings
were changed. No hardware rollback was needed because no persistent change or
service transition was performed.

## Evidence and reproducibility

Private local evidence is in `.local/wp4a3`: identity/runtime/storage/watchdog
snapshots, semaphore-samples.json, semaphore-actors.txt, restart-preflight-final.json,
stack-gate.txt, final-state.txt, maintenance-tests-final.log, probe-asan-final.log,
coordinator-asan-final.log, qemu.log and Baby/Fab4 ELF/hash outputs.

The inspected target AppletManager and Lua integration sources are captured
locally; no unknown code from USB was loaded or executed. The failed attempt to
read an AppletInstaller source path established only that the guessed path is
absent, not that dynamic installation/reload is a qualified alternative.

Exact read-only hardware follow-up:

```powershell
python .local/wp4a3/preflight.py
```

It uses the established strict-known-host deployment transport and a temporary
password environment variable, never a stored credential in these reports.
Host test command:

```sh
ctest --test-dir /tmp/sb-menu-fix --output-on-failure \
  -R 'maintenance_probe|maintenance_coordinator|maintenance_primitives' -j 1
```

See [the execution plan](STANDALONE_MUSIC_WP4A3_PLAN.md). Recommended next concrete
implementation: assemble the production ownership/resource/startup bindings and
rebase the watchdog-safe restart/recovery candidate onto the current installed
stack. Only after those entry gates pass can the first controlled MP3 test occur.
