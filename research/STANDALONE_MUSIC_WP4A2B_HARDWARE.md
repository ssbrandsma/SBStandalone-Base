# WP4a.2b actual Fab4 observations

Date: 2026-11-04. Device: owner-designated Touch 192.168.1.244 only.
Firmware 7.7.3 r16676; Logitech Fab4; kernel 2.6.26.8-rt16-332-g5849bfa.

## Before modification

SSH used the existing strict known-host entry. Read-only backups contain sixteen
original files with SHA-256, owner, mode and mtime; seven authorized paths were
absent. Original Base is 0.6.7. The candidate minimally patches that installed
applet rather than replacing it wholesale with the repository's 0.7.0 applet.

Boot UUID `9162ba64-822a-46e1-8ab5-2ea3c6a66740`.
Jive PID 3192/start 55166; watchdog PID 639/start 923 with writable `/dev/watchdog`
FD 1. Decoder children 3212/3213; proxy 3282, Base 3297, webserver 3312.
Now Playing inactive/stopped. No active Spotify/ogg bridge/Qobuz process observed.
TinySC absent; it was not deliberately started.

USB `/dev/sda1`, major/minor 8:1, mounted FAT32 at `/media/sda1`. Root is the
expected writable unionfs; `/dev` tmpfs and `/var` ramfs. Relevant target directory
chains were ordinary root-owned directories, not symlinks. Free overlay space
7088 KiB. No USB boot extension, log redirection directory or rcS.local was found.
Qualified backups, recovery payload, host tests and ARM builds existed locally
before the installation attempt. No production Music runtime was selected.

## Single installation attempt

1. Rechecked firmware, same boot, idle playback, storage, audio application
   processes, original file hashes/metadata and source-bound host receipt.
2. Created private `/dev/standalonebase-wp4a2b`, uploaded only the qualified
   production maintenance helper, verified its read-back SHA-256, mode 0500.
3. Ran native `restart-preflight`, a read-only operation.
4. It returned code 2: `missing/stale Jive UI or feeder keepalive evidence`.
5. Stopped device-changing work. Removed the private RAM helper/directory.
6. Used read-only SSH to verify original file state and process identities.

The persistent recovery directory was never created. No overlay target or opt-in
file was installed. No Base activation, TinySC stop, Jive stop/restart, watchdog
handoff, recovery launch or reboot was attempted. No alternate preflight or
weakened check was tried. See the watchdog report for predicate limits.

## After refusal

Original sixteen file hashes, UID/GID and modes match the snapshot. Expected
absent paths remain absent. Boot UUID is unchanged; uptime advanced from
63076.05 to 63181.03 seconds across the final read-only snapshots. Jive/watchdog
PID/start identities and original three Base services remain unchanged. The RAM
bootstrap and persistent candidate/policy paths are absent.

USB write counters remained six completed writes / 29 sectors for sda and sda1,
matching the earlier read-only observations. This is block-level evidence, not
a full USB file-content hash audit. Our code did not create, write, delete,
remount or format anything on USB. Original Now Playing remains inactive/stopped.

| Qualification phase | Actual outcome |
|---|---|
| G1 minimal overlay install | Blocked before persistent staging/replacement |
| G2 TinySC stop/inactive path with new Base | Not activated; pre-existing absence observed only |
| G3 controlled Jive restart | Not attempted |
| G4 controlled reboot | Not attempted; prerequisite G3 unmet |
| H Radio/Spotify menus | Not exercised after restart; no restart occurred |
| H Qobuz | Not installed, not exercised |

Original services and metadata preservation are verified; actual menu usability,
new lifecycle health, restart survival and reboot survival are not claimed.
Hardware performance measurements dependent on successful activation are unavailable.
