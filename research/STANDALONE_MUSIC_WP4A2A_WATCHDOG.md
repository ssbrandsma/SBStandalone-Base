# WP4a.2a watchdog evidence and restart design

## Actual firmware

- `/dev/watchdog`, misc10:130, mxc_wdt driver. Kernel WATCHDOG, MXC_WATCHDOG and NOWAYOUT are enabled. Boot reports initial timeout60s. Current timeout was not queried by ioctl; this task never opened/reconfigured the node.
- `/usr/sbin/watchdog` PID639 owns FD1 to the device. VmRSS1748KiB, VmLck1808KiB. It was not stopped/replaced or joined by another hardware feeder.
- watchdog.conf checks the SqueezePlay PID file and SysV semaphore key1234. Startup delay20s, interval5s, realtime priority1, repair.sh on failure.
- Pinned upstream platform_linux.c145-201 describes SEM_UNDO allocation/countdown refresh; framework startup/loop sets grace and refreshes counters. Installed daemon imports semget/semctl/semop; current set has20 slots. Upstream is reference, not asserted byte-identical to the2014 firmware.
- Official `stop` terminates the PID and removes files; `stopwdog` adds a one-hour sleep PID AFTER stop. Ordinary restart calls stop/start. PID existence alone does not prove executable/start identity or healthy UI.

## Previous reset

Persistent crashlog records missing monitored PID14559, errno3, repair.sh error3. The next boot reports Watchdog timeout. This establishes failure/reset mechanism, not why Jive disappeared. SSH child lifetime or startup failure remains a hypothesis. repair.sh copies logs to a persistent crashlog and lets watchdog recovery proceed. No failure was injected here.

## Proposed procedure — not implemented or qualified on hardware

1. Lock maintenance; validate model, boot UUID, exact watchdog/config/FD, Jive/helper executable and start-time identities, inactive playback and guard hashes.
2. Establish a detached, bounded maintenance PID holder BEFORE stopping Jive. Verify independence from SSH and atomically switch the expected PID file to it. Keep the original hardware daemon; do not add a device writer/change timeout.
3. TERM verified Jive, boundedly confirm exit/IPC release, then stop its verified old helpers. Otherwise original Base rejects pre-existing listeners during initialization.
4. Start known-good Jive with official environment and detached stdio/session. Keep the holder published while verifying actual new identity, real UI responsiveness, helpers, IPC/loopback and unchanged boot UUID/monotonic uptime within a finite deadline.
5. Atomically publish healthy Jive, then terminate/verify holder exit and release maintenance.

Failure must preserve the holder/guard and expose a bounded maintenance lease to SSH recovery. At most one bounded known-good recovery; no indefinite restart loop or automatic second reboot. Duplicate requests, stale/reused PID, feeder loss, startup timeout, unexpected exit or changed boot UUID abort qualification.

The earlier boot-policy gate failed. Restart/crash/stale-PID/feeder-failure/interruption tests and timings are NOT EXECUTED. No reusable restart command is exposed. Historical fixed-PID scripts must not be reused. A safe procedure cannot be inferred from host guard tests.

## Continuation ? 2026-11-04

The user explicitly retained the FULL failure requirement, including overlay erasure after storage mount failure, and instructed: no hardware changes. The known /linuxrc gate therefore remains failed. The guard, maintenance helper and all test fixtures remain LOCAL ONLY. No target install, service restart, reboot, scan, decoder experiment or audio was performed.

Current read-only snapshot: same boot UUID9162ba64-822a-46e1-8ab5-2ea3c6a66740, uptime37640.51s, Jive3192/watchdog639/Base3297/web3312/proxy3282. Now Playing inactive/stopped. USB counters remain6 completed writes/29 sectors, unchanged from the previous late snapshots. The earlier one-sector write is still unattributed; it is not explained or retroactively treated as harmless.

New local work: native/sbmaintenance/helper.c and an explicit ARM build script. Production/test builds compile with -Werror.19 real Linux host tests passed in0.342s in a temporary networkless Docker container. Tests cover detached process survival, descriptor/environment isolation, PID handoff/restoration, concurrent requests, stale identities, unexpected exits, emergency PID recovery, file safety and bounded O_NOATIME metadata auditing. A private SysV IPC set verifies that snapshots do not change semaphore values. No watchdog device or decoder is used.

Both Baby and Fab4 static production builds passed ELF/dependency checks (46004bytes each). Under Docker's existing ARM binfmt emulation, both read20 semaphore slots correctly and refused production mutations on the non-Fab4 host. This is ARM ABI/emulation evidence, not actual target behavior. Production binaries contain no SB_TEST path overrides. The full restart coordinator, UI/IPC health timing, target recovery and controlled restart/reboot remain NOT qualified.

WSL again returned Wsl/Service/E_UNEXPECTED. It was not restarted; existing Docker images supplied the isolated Linux toolchain, without network downloads or changes to other running workloads. The previous17 guard/recovery tests are historical results, not claimed rerun in this continuation.
