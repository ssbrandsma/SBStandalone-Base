# WP4a.2a provenance and evidence

Hardware collection used explicit192.168.1.244, existing known-host SSH and private authentication. Command completion markers/bounds replaced reliance on Dropbear status. No service was started/stopped, reboot requested, device guard installed, USB intentionally written, watchdog device opened, or Music run.

Captured `/linuxrc:57,68,113,138` proves the pre-overlay reset/erase boundary. Kernel boot evidence reports Watchdog timeout. The persistent previous crashlog identifies the missing watched PID and repair error3. Raw sources/logs remain private ignored evidence rather than public log dumps.

| Evidence under `.local/wp4a2a/` | Purpose |
| --- | --- |
| firmware-before.tar/.json | Exact firmware files, hashes, metadata |
| tinysc-leaves-before.tar | Original direct entry scripts |
| linuxrc.bin, boot-chain.txt | Factory boot decision/init selection |
| kernel.config, watchdog-process.txt | Driver/NOWAYOUT, feeder FD/process/IPC |
| previous-crashlog.txt | Persistent previous watchdog failure |
| system/startup/routes/overlay.txt | Read-only inventory |
| usb-audit-before/after.txt | Fixed metadata paths/counters/boot/process state |
| recovery-snapshot/files.json |13 originals, hashes/owners/groups/modes/links |
| host-tests.log |17 local methods; not hardware qualification |
| final-state-extra.txt | Inactive/stopped metadata, original watchdog FD |

Initial recursive grep and /proc/*/comm probes were unsupported on this firmware; empty output was NOT negative evidence. Exact archives and /proc/PID/status/cmdline/FD replaced them.

Pinned upstream protocol context: [LMS-Community squeezeplay cca1bba](https://github.com/LMS-Community/squeezeplay/tree/cca1bba4abeb3fef2b7c4a48a4e4da8fc255666d). It is not asserted identical to installed2014 code. Actual binary/config/log evidence takes precedence. No guessed protocol was deployed as a restart implementation.

The [machine ledger](STANDALONE_MUSIC_WP4A2A_EVIDENCE.json) hashes captured evidence/artifacts and records the blocked status. Hashes establish captured bytes, not qualification. Passwords/cookies/private music contents are not published.

Additional read-only integrity check: all91 regular files in the prior Base/Radio/Spotify/config/firmware-reference backup matched current bytes, with91 current regular files and no differences. This establishes installation-content continuity, not live UI/playback/reboot regression. Qobuz was not installed. Evidence: `.local/wp4a2a/existing-app-integrity.json`.

## Continuation ? 2026-11-04

The user explicitly retained the FULL failure requirement, including overlay erasure after storage mount failure, and instructed: no hardware changes. The known /linuxrc gate therefore remains failed. The guard, maintenance helper and all test fixtures remain LOCAL ONLY. No target install, service restart, reboot, scan, decoder experiment or audio was performed.

Current read-only snapshot: same boot UUID9162ba64-822a-46e1-8ab5-2ea3c6a66740, uptime37640.51s, Jive3192/watchdog639/Base3297/web3312/proxy3282. Now Playing inactive/stopped. USB counters remain6 completed writes/29 sectors, unchanged from the previous late snapshots. The earlier one-sector write is still unattributed; it is not explained or retroactively treated as harmless.
