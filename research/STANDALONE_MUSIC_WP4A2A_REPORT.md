# WP4a.2a — blocked before hardware modification

2026-11-03. **Incomplete and not hardware-qualified.** Only read-only device investigation, local prototypes, host tests and read-only backup/verification were performed. No guard was installed, service stopped/restarted, reboot requested, USB mount changed, music scanned, audio started or ownership/decoder experiment run.

The earlier reset is now established as a watchdog reset. The current boot reports `Reset status: Watchdog timeout`. Persistent crashlog.gp6m17 records watchdog628 finding monitored PID14559 absent, errno3, followed by repair.sh and watchdog error3. Why that Jive process disappeared remains unconfirmed; an SSH child-lifecycle problem is a hypothesis.

## Failed boot safety gate

Factory `/linuxrc` runs before the persistent union overlay. Its line57 selects factory reset when the persistent storage mount fails. Line68 erases writable storage, line113 constructs the overlay, and line138 starts init. Therefore an overlay-only TinySC policy can be erased by a required persistent-state failure and factory startup can return. An overlay replacement of `/linuxrc` cannot change the earlier factory script already executing.

The assignment requires rejecting qualification if unreadable persistent state can permit startup. Factory-partition changes are prohibited. No earlier boot barrier can be claimed under those constraints. Phases F–H are blocked; the conditional authorization for one reboot was not exercised.

A separate audit limitation exists: USB completed writes increased5→6 and reported sectors28→29 during the read-only investigation. Attribution is unknown. Later fixed-path snapshots were stable. USB cannot be described as untouched.

## Delivered work

- Six-file intact-overlay deny prototype in `packaging/fab4-reliability/`: Lua Meta/Applet, shell service and three direct Perl entry stubs. No Base, bridge, RAM marker or optional policy read is required. This does not solve loss of the entire overlay.
- `qualification.json` marks installation unqualified. The host tool refuses installation before SSH and has no force/restart/reboot option.
- Host backup, verification and file-recovery tool with known-host SSH, explicit completion markers, allowlisted paths, hashes, metadata, atomic replacement, preserve-guard default and explicit USB warning before original TinySC restoration.
-17 host tests passed, including actual local atomic/interruption cases,64 partial-install restoration combinations, Lua5.1/shell/Perl barriers and the captured factory-reset predicate. These are not hardware restart proof.
- Fresh13-file device backup and verification: no differences. No target restoration write was performed. Full service/reboot recovery remains unqualified.

## Final status

The same boot UUID, Jive3192, watchdog639, proxy3282, Base3297 and web3312 remain. Final Now Playing is inactive/stopped. TinySC/server/resizer/scanner were not observed in snapshots; transient-start absence was not instrumented as a reboot test. Qobuz Connect was absent initially and was not installed.

| Requirement | Result |
| --- | --- |
| Full fault-model startup protection | Failed design gate: factory fallback removes overlay guards |
| Reboot/USB/missing-RAM protection | Local intact-overlay tests only; hardware unqualified |
| Watchdog-safe restart | Design only; not executed |
| Controlled reboot with USB | Not performed |
| No transient TinySC startup | Not hardware-qualified |
| No unexpected USB writes | Not established; one write/sector observed |
| Existing applications | Files unchanged; live menu regression not performed |
| Rollback | Local file-level tests/live read-only verification passed; full hardware recovery unqualified |
| No factory changes or Music tests | Respected |

**WP4a.2 playback qualification cannot safely resume.** Future scope must address stock factory-reset-on-storage-failure, or explicitly exclude it from the fault model, and explain the USB write before hardware qualification. No factory/bootloader change was attempted. Stop after this work package.

[Startup](STANDALONE_MUSIC_WP4A2A_TINYSC_STARTUP.md), [watchdog](STANDALONE_MUSIC_WP4A2A_WATCHDOG.md), [implementation](STANDALONE_MUSIC_WP4A2A_IMPLEMENTATION.md), [tests](STANDALONE_MUSIC_WP4A2A_TEST_RESULTS.md), [USB](STANDALONE_MUSIC_WP4A2A_USB_AUDIT.md), [rollback](STANDALONE_MUSIC_WP4A2A_ROLLBACK.md), [evidence](STANDALONE_MUSIC_WP4A2A_EVIDENCE.md).

Additional read-only integrity check: all91 regular files in the prior Base/Radio/Spotify/config/firmware-reference backup matched current bytes, with91 current regular files and no differences. This establishes installation-content continuity, not live UI/playback/reboot regression. Qobuz was not installed. Evidence: `.local/wp4a2a/existing-app-integrity.json`.

## Continuation ? 2026-11-04

The user explicitly retained the FULL failure requirement, including overlay erasure after storage mount failure, and instructed: no hardware changes. The known /linuxrc gate therefore remains failed. The guard, maintenance helper and all test fixtures remain LOCAL ONLY. No target install, service restart, reboot, scan, decoder experiment or audio was performed.

Current read-only snapshot: same boot UUID9162ba64-822a-46e1-8ab5-2ea3c6a66740, uptime37640.51s, Jive3192/watchdog639/Base3297/web3312/proxy3282. Now Playing inactive/stopped. USB counters remain6 completed writes/29 sectors, unchanged from the previous late snapshots. The earlier one-sector write is still unattributed; it is not explained or retroactively treated as harmless.

New local work: native/sbmaintenance/helper.c and an explicit ARM build script. Production/test builds compile with -Werror.19 real Linux host tests passed in0.342s in a temporary networkless Docker container. Tests cover detached process survival, descriptor/environment isolation, PID handoff/restoration, concurrent requests, stale identities, unexpected exits, emergency PID recovery, file safety and bounded O_NOATIME metadata auditing. A private SysV IPC set verifies that snapshots do not change semaphore values. No watchdog device or decoder is used.

Both Baby and Fab4 static production builds passed ELF/dependency checks (46004bytes each). Under Docker's existing ARM binfmt emulation, both read20 semaphore slots correctly and refused production mutations on the non-Fab4 host. This is ARM ABI/emulation evidence, not actual target behavior. Production binaries contain no SB_TEST path overrides. The full restart coordinator, UI/IPC health timing, target recovery and controlled restart/reboot remain NOT qualified.

WSL again returned Wsl/Service/E_UNEXPECTED. It was not restarted; existing Docker images supplied the isolated Linux toolchain, without network downloads or changes to other running workloads. The previous17 guard/recovery tests are historical results, not claimed rerun in this continuation.
