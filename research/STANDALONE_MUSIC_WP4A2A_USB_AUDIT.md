# WP4a.2a USB observations

Actual `/dev/sda1`, vfat, `/media/sda1`, rw,8:1; original fmask/dmask0022, codepage437, iso8859-1/utf8, shortname=mixed. No remount/configuration change, removal or deletion was requested. Existing `.Squeezebox/cache` was retained.

| Snapshot | sda completed writes | sda sectors written | sda1 counters |
| --- | ---: | ---: | --- |
| Initial Phase A | 5 | 28 | 5 / 28 |
| Later fixed-path before | 6 | 29 | 6 / 29 |
| Final fixed-path after | 6 | 29 | 6 / 29 |

Parent/partition describe the same IO; do not sum them. One additional completed write/sector was observed during read-only investigation. Cause/content are unknown. Filesystem/background activity is possible but not established. No guard/restart/reboot occurred, so attribution to a deployed candidate is inappropriate.

Metadata inspection used three fixed `ls -d` operands: media root, `.Squeezebox`, `cache`. It did not enumerate arbitrary contents. Owner/group0:0, size4096 and displayed mtime/ctime/atime remain Nov3 20:44 at minute resolution. This does not prove unchanged entries/content. Full before/after inventories and content hashes were not collected. Previous-task cache emptiness was not re-proven.

The late pair shows no further write completions/sectors at available resolution. The whole phase does NOT support a no-observed-writes claim; USB is NOT proven read-only. No forced flush, repair, filesystem reconfiguration or write test was performed.

Raw evidence: `.local/wp4a2a/system.txt`, `usb-audit-before.txt`, `usb-audit-after.txt`. No across-install/restart/reboot audit exists because those actions were blocked. Unexpected-write attribution remains a prerequisite.

## Continuation ? 2026-11-04

The user explicitly retained the FULL failure requirement, including overlay erasure after storage mount failure, and instructed: no hardware changes. The known /linuxrc gate therefore remains failed. The guard, maintenance helper and all test fixtures remain LOCAL ONLY. No target install, service restart, reboot, scan, decoder experiment or audio was performed.

Current read-only snapshot: same boot UUID9162ba64-822a-46e1-8ab5-2ea3c6a66740, uptime37640.51s, Jive3192/watchdog639/Base3297/web3312/proxy3282. Now Playing inactive/stopped. USB counters remain6 completed writes/29 sectors, unchanged from the previous late snapshots. The earlier one-sector write is still unattributed; it is not explained or retroactively treated as harmless.

A new local C audit primitive uses bounded descriptor-relative O_NOATIME/O_NOFOLLOW directory reads and lstat metadata only. Host tests preserved directory/file atime, refused symlink escape and enforced512 entries per directory. It has not been installed/run on the Touch. No new target directory enumeration or across-restart/reboot audit was performed.
