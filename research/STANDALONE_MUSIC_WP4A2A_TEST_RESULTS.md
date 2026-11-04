# WP4a.2a executed tests and limits

Final functional run:17 unittest methods passed in4.448s, WSL root host, Python3, existing Lupa Lua5.1 and Perl. Command: `sudo sh .local/wp4a2a/host_tests.sh`. Subcases include eleven runtime/USB/reinitialization conditions, seven shell actions, three Perl entries and64 partial guard-restoration combinations.

Executed locally:

- Lua with IO/process launch disabled: absent RAM/Base/policy context cannot start payloads/write prefs. USB boot/later/reinsert contexts are injected, not real firmware events.
- Injected Lua require failure; independent shell/Perl barriers refuse launch.
- Missing/unreadable host gate file fails before transport construction.
- Installation refused for all considered partial/full artifact states.
- Captured /linuxrc predicate alone evaluated with controlled variables: failed mount selects factory reset; no destructive/mount/init command executed. This proves the BLOCKER, not success.
- Hash/corrupt-backup/foreign-content checks, guard-preserving default and USB rollback acknowledgment.
- Actual local atomic replacement/metadata restoration; interruption before rename preserves original, lost acknowledgment after rename leaves a complete recoverable version.

Actual target: read-only architecture/process/config/log capture,13-file backup and verification with no differences. Known-host SSH and command markers worked. No service start/stop, reboot, scan or playback.

NOT EXECUTED: watchdog restart state machine tests (normal restart, crash, stale PID, helper overlap, feeder failure, interrupted recovery, duplicate restart, unexpected exit), target atomic restoration, controlled restart/reboot, transient-start instrumentation, post-reboot menus, guard overhead or USB-event hardware tests. Full watchdog procedure remains design-only.

No native source changed, so ARM/QEMU were not rerun. Earlier96-test/ARM results are not claimed as current proof. Tests run directly, not as an always-on CTest dependency on private firmware. Without an actual /linuxrc capture, its source-specific test explicitly skips.

Phases F–H are BLOCKED.17 passing host methods do not override the failed boot gate or unexplained USB write.

Additional read-only integrity check: all91 regular files in the prior Base/Radio/Spotify/config/firmware-reference backup matched current bytes, with91 current regular files and no differences. This establishes installation-content continuity, not live UI/playback/reboot regression. Qobuz was not installed. Evidence: `.local/wp4a2a/existing-app-integrity.json`.

## Continuation ? 2026-11-04

The user explicitly retained the FULL failure requirement, including overlay erasure after storage mount failure, and instructed: no hardware changes. The known /linuxrc gate therefore remains failed. The guard, maintenance helper and all test fixtures remain LOCAL ONLY. No target install, service restart, reboot, scan, decoder experiment or audio was performed.

Current read-only snapshot: same boot UUID9162ba64-822a-46e1-8ab5-2ea3c6a66740, uptime37640.51s, Jive3192/watchdog639/Base3297/web3312/proxy3282. Now Playing inactive/stopped. USB counters remain6 completed writes/29 sectors, unchanged from the previous late snapshots. The earlier one-sector write is still unattributed; it is not explained or retroactively treated as harmless.

New local work: native/sbmaintenance/helper.c and an explicit ARM build script. Production/test builds compile with -Werror.19 real Linux host tests passed in0.342s in a temporary networkless Docker container. Tests cover detached process survival, descriptor/environment isolation, PID handoff/restoration, concurrent requests, stale identities, unexpected exits, emergency PID recovery, file safety and bounded O_NOATIME metadata auditing. A private SysV IPC set verifies that snapshots do not change semaphore values. No watchdog device or decoder is used.

Both Baby and Fab4 static production builds passed ELF/dependency checks (46004bytes each). Under Docker's existing ARM binfmt emulation, both read20 semaphore slots correctly and refused production mutations on the non-Fab4 host. This is ARM ABI/emulation evidence, not actual target behavior. Production binaries contain no SB_TEST path overrides. The full restart coordinator, UI/IPC health timing, target recovery and controlled restart/reboot remain NOT qualified.

WSL again returned Wsl/Service/E_UNEXPECTED. It was not restarted; existing Docker images supplied the isolated Linux toolchain, without network downloads or changes to other running workloads. The previous17 guard/recovery tests are historical results, not claimed rerun in this continuation.
