# Fresh read-only hardware evidence

Only owner-designated Fab4 192.168.1.244 was contacted, with strict known-host SSH.
Firmware/profile validation passed: Fab4 board, 7.7.3 r16676, writable UnionFS.
Boot remains `9162ba64-822a-46e1-8ab5-2ea3c6a66740`.

| Role | PID | Kernel start ticks | Parent | RSS pages (sample) |
| --- | ---: | ---: | ---: | ---: |
| Jive | 3192 | 55166 | 1 | 8437 |
| Base | 3933 | 17600372 | 1 | 619 |
| Proxy | 3282 | 56496 | 1 | 224 |
| Web | 3312 | 56733 | 1 | 113 |
| Watchdog | 639 | 923 | 1 | 437 |

These process/start identities match before/after. Base FD55 is the active
`/dev/standalone-music-test/library.db`. Feeder FD1 is `/dev/watchdog`; no other
process was made a feeder. A transient zombie child named watchdog was also present
in the initial inventory, parent 639, with no open device FD; do not mistake that
for a second live feeder. No watchdog action was taken.

The host-only SQLite copy passes integrity_check, user_version 3. Media ID1 has
source BLOB `3dc256682c8d3c420100000000000000`, generation 1, online 1 and scan root
`mp3/Fleetwood Mac - Rumors-1977-FAF`. The 122880-byte active file SHA before/after
matches the historical SHA. It was not opened through SQLite on the device.
All three existing RAM DB files remain. No new scan or DB migration was requested.

Stock CometD reports one player and category counts 1 artist, 1 album, 11 tracks,
1 genre and 11 folder entries. Tracks have `action:none`. Health reports Base
0.7.0 OK. TinySC status is fresh STOPPED_VERIFIED/count zero, actor PID3933/start
17600372. Now Playing is inactive/stopped. No play/add/queue/handoff commands sent.

USB remains vfat `/media/sda1`; the library remains on the existing 8 MiB tmpfs.
Both sda/sda1 counters remain 6 completed writes and 29 written sectors. Overlay
free 3516 KiB; library tmpfs free 7808 KiB. No mount/configuration/volume changes.

Observed browse request elapsed times through SSH TCP forwarding, one sample each:
artists 292.42 ms, albums 278.72 ms, tracks 254.30 ms, genres 169.66 ms and folder
330.63 ms. These include host/SSH/protocol overhead, not device CPU time. No restart
latency, replacement peak RSS or CPU percentage was measured.

Limitations: physical screen navigation was not independently rechecked in this
phase. `/proc/sysvipc/sem`, feeder FD/configuration and process evidence were read;
fresh semaphore allocation/countdown values were not measured using the native
probe. Thus full watchdog/Jive readiness is **not qualified**. Source and historical
evidence do not substitute for these required live gates. Web/proxy installation
and identities remain unchanged; Radio/Spotify UI or audio were not tested.

## Subsequent owner UI confirmation

The owner confirmed that the Touch is idle and My Music ? Artists ? Fleetwood Mac ? Rumours opens with 11 tracks. This is physical UI evidence supplied by the owner, not an automated heartbeat or fresh watchdog countdown proof. No playback was started. It resolves the pending manual UI question but does not qualify the missing supervisor adapter, no-scan rollback or recovery coordinator. Evidence: `.local/wp4a3d/owner-ui-confirmation.json`.
