# WP4a.2c preflight

Actual deployment target: **192.168.1.244**, verified Fab4, after owner correction
and explicit continuation. .245 was only read diagnostically and is a Radio/Baby.
No host key was accepted automatically; established RejectPolicy SSH and trusted
known-host entries were used. No experimental maintenance helper was invoked.

| Identity/state | Verified observation |
|---|---|
| Hostname | SqueezeboxTouch |
| Board | Logitech Fab4 Board |
| CPU | ARMv6 compatible; architecture 6TEJ |
| Firmware | 7.7.3 r16676 |
| Kernel | 2.6.26.8-rt16-332-g5849bfa, armv6l |
| Board UUID | 810384b1-35c1-a0f4-d1b6-4e6b128e9d0c |
| Boot UUID | 9162ba64-822a-46e1-8ab5-2ea3c6a66740 |
| Original applet/native release | 0.6.7; native /health independently confirms |
| Applet directory | /usr/share/jive/applets/StandaloneBase |
| Configuration | /mnt/storage/standalonebase/config.json, preserved |
| Writable root | unionfs dirs=/mnt/storage=rw:/=ro |
| Internal free space before deployment | 7088 KiB |
| RAM | 125848 KiB total; observation MemFree 15580-15628 KiB plus cache |
| USB | /dev/sda1, FAT32, /media/sda1, existing mount unchanged |
| Audio | Now Playing inactive/stopped; no active Spotify/bridge/Qobuz process |
| TinySC | No server/scanner/resizer process observed |
| Arbitration | Persistent opt-in and RAM latch absent |

Jive 3192/start 55166; watchdog 639/start 923 with writable /dev/watchdog FD1;
proxy 3282/start 56496, Base 3297/start 56611, webserver 3312/start 56733. The
watchdog configuration/binary were archived and unchanged. Process identity and
descriptor evidence are observations, not qualification of the experimental
watchdog preflight/handoff. That helper was neither installed nor executed.

Current source and all eight WP4a.2b reports were reviewed. Sibling repositories
were found at C:/Projects/squeezebox, C:/Projects/SBSpotifyConnect and
C:/Projects/QoBuz. No sibling app was changed. Current Lua dependency inventory
shows only three file differences from installed data, with no required native
service replacement for this on-disk scope. Future loaded-version compatibility
is not proven by this comparison.

Preflight logs contain old crashlog resubmission and a network-thread timeout
before deployment. These were retained as existing warnings, not attributed to
this update. Firmware logs can print credentials; captured runtime evidence is
redacted for credential log lines. No credential is included in these reports.

Verified full audit archive before replacement: .local/wp4a2c/touch/before-audit.tar
(69 file/symlink records, 3,920,896 bytes). Separate applet backup preserves exact
nineteen original selected files, owner/mode/mtime and original absence of the
heartbeat module. No selected path/parent symlink was overwritten.
