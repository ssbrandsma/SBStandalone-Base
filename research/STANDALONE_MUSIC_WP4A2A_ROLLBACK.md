# WP4a.2a backup and recovery

No deployment occurred and no rollback was needed. Existing Jive/watchdog/services and disk TinySC policy remain unchanged. In-memory protection inherited from previous recovery remains in that Jive incarnation; future ordinary reboot can restore stock TinySC behavior.

## Actually executed: backup and verify

From repository root, credentials supplied privately through the existing SSH method:

```powershell
python scripts/fab4-reliability.py backup --host 192.168.1.244 --snapshot .local/wp4a2a/recovery-snapshot
python scripts/fab4-reliability.py verify --host 192.168.1.244 --snapshot .local/wp4a2a/recovery-snapshot
```

Backup requires a NEW host destination. Thirteen regular root-owned files were saved with SHA256/uid/gid/mode/symlink records. Archive SHA256: `65f1d7eb81ca3df522c2cdafaa7e2c80f8a2406178530a59376045003315d0d7`. Live verification returned no differences, device_modified=false. Backups are on the PC, not USB/device flash.

## Recovery interface — not exercised as target writes

Default preserve-inhibition file recovery:

```powershell
python scripts/fab4-reliability.py restore --host 192.168.1.244 --snapshot .local/wp4a2a/recovery-snapshot
```

It retains recognized installed TinySC guards and restores only recognized allowlisted original app/native/config files. It never starts Jive/TinySC or reboots. Foreign changes, corrupt backups and unsafe paths/links/metadata are refused. Missing-directory/factory-reset reinstall is not implemented.

Original TinySC restoration re-enables startup. With USB attached the following needs acknowledgment and prints a warning. This is descriptive recovery documentation, NOT a command performed/recommended for this blocked qualification:

```powershell
python scripts/fab4-reliability.py restore --host 192.168.1.244 --snapshot .local/wp4a2a/recovery-snapshot --restore-tinysc --ack-tinysc-with-usb
```

SSH file recovery is independent of Jive/Base, preserves complete originals until rename and recognizes original/guard versions after partial installation. Host tests covered real local atomic interruption and64 partial combinations. No remote write was attempted after the failed gate.

Full service failure, watchdog-reset, full reboot and unreadable-overlay recovery remain UNQUALIFIED. No safe restart command is exposed. Do not use old fixed-PID scripts or infer restart safety from successful file verification. No factory partition or `.Squeezebox` directory was changed.

Additional read-only integrity check: all91 regular files in the prior Base/Radio/Spotify/config/firmware-reference backup matched current bytes, with91 current regular files and no differences. This establishes installation-content continuity, not live UI/playback/reboot regression. Qobuz was not installed. Evidence: `.local/wp4a2a/existing-app-integrity.json`.

## Continuation ? 2026-11-04

The user explicitly retained the FULL failure requirement, including overlay erasure after storage mount failure, and instructed: no hardware changes. The known /linuxrc gate therefore remains failed. The guard, maintenance helper and all test fixtures remain LOCAL ONLY. No target install, service restart, reboot, scan, decoder experiment or audio was performed.

Current read-only snapshot: same boot UUID9162ba64-822a-46e1-8ab5-2ea3c6a66740, uptime37640.51s, Jive3192/watchdog639/Base3297/web3312/proxy3282. Now Playing inactive/stopped. USB counters remain6 completed writes/29 sectors, unchanged from the previous late snapshots. The earlier one-sector write is still unattributed; it is not explained or retroactively treated as harmless.

The new primitive can atomically hand off/restore the monitored PID and recover a dead recorded PID to an exact verified replacement, but this was tested only with host fixtures. The passive holder does not expire into a reset and requires explicit cleanup/recovery; it is not a new hardware feeder. No complete target restart/recovery command is exposed. Existing file-recovery commands remain the only public host-tool interface and retain their stated limitations.
