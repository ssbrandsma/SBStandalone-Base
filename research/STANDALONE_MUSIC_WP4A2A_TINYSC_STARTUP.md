# WP4a.2a TinySC launch chain and protection boundary

Sources were copied read-only from the current Fab4. Exact hashes are in the evidence ledger. No captured destructive firmware function was executed.

## Boot and media

1. Kernel command line selects factory `init=/linuxrc` on cramfs.
2. `/linuxrc:26-27` mounts writable storage. Its line57 reset condition includes mount failure, a factory-reset marker or boot parameter. Line68 erases storage, before overlay construction113-135 and chroot/init138.
3. Inittab invokes rcS. It starts udev mounting, RAM/syslog, watchdog59-60, network, optional rcS.local/media boot scripts, samba and SqueezePlay129. Current optional overrides were absent; no USB code ran.
4. WP6a's captured udev rules/mount.sh retain normal USB/SD mounting. MediaManager configures startup discovery/active uevents and media actions. Mounting was not replaced.
5. SqueezePlay extends Lua paths from media if present. SqueezeCenter Meta registers services26 and calls squeezecenterStartupCheck from configureApplet37-63.

## Entry points

| Entry | Installed source | Consequence |
| --- | --- | --- |
| Boot/Jive restart with media | SqueezeCenterApplet squeezecenterStartupCheck66-131 | Selects drive, writes prefs, restartServer(true) |
| Media switching | switchServerToDifferentMedia173; _writeSCPrefsFile795 | Writes selected storage/prefs, restarts |
| Media/advanced Start | startSqueezeCenter459; _startServerWindow555 | Chooses media, starts service |
| Wipe/rescan | _wipeRescan485; _wipeTimer524 | Destructive stock operation and later start; never invoked here |
| Common executor | _squeezecenterAction274-345; restartServer670 | Calls init action and guardian |
| Shell start | init/squeezecenter15-31 | Launches slimserver.pl then resizer |
| Resizer | init34-44 | Launches gdresized.pl |
| Restart/rescan | init96-114 | Stop/sleep/start; rescan delegates restart |
| Direct executable/interpreter | slimserver.pl, gdresized.pl, scanner.pl | Bypasses init-only protection |

Stop actions use PID files and TERM. SqueezeboxFab4 SCGuardianTimer812 checks/notifies; no automatic restart was found. The orphan SqueezeCenter udev handler does not prove an active insertion-start dispatcher. MediaManager insertion menus still expose Start/switch actions. A live server can initiate scanner/resizer children, so a final process list alone is insufficient.

## Prototype and failed boundary

The Meta/Applet pair replaces startup/prefs/media/wipe/menu routes. The shell rejects launch/restart/rescan/resizer/unknown actions; Perl BEGIN stubs reject direct entry before any original payload. Runtime marker, Base or bridge failures cannot unlock these installed files.

All barriers require their overlay files to remain present. Factory mount failure can erase the entire overlay before they execute. That known required failure invalidates qualification. Partial-file restoration was host-tested, but not every partially installed subset covers every direct entry; installation is refused. No target guard or reboot was attempted.

## Continuation ? 2026-11-04

The user explicitly retained the FULL failure requirement, including overlay erasure after storage mount failure, and instructed: no hardware changes. The known /linuxrc gate therefore remains failed. The guard, maintenance helper and all test fixtures remain LOCAL ONLY. No target install, service restart, reboot, scan, decoder experiment or audio was performed.

Current read-only snapshot: same boot UUID9162ba64-822a-46e1-8ab5-2ea3c6a66740, uptime37640.51s, Jive3192/watchdog639/Base3297/web3312/proxy3282. Now Playing inactive/stopped. USB counters remain6 completed writes/29 sectors, unchanged from the previous late snapshots. The earlier one-sector write is still unattributed; it is not explained or retroactively treated as harmless.
