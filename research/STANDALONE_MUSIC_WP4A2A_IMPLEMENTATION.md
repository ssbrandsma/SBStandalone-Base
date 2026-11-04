# WP4a.2a local implementation and deployment gate

`packaging/fab4-reliability/` contains an opt-in intact-overlay prototype: SqueezeCenter Meta/Applet, service wrapper and three direct Perl-entry deny stubs. The normal package builder uses explicit file lists and does not include it. No Base/Radio/Spotify/Qobuz runtime source or startup behavior was changed.

Installed-file content is the inhibition policy: no RAM marker, Base/bridge/scan status or optional policy read can reach original launch code. Shell launch/unknown actions exit77; stop actions check numeric PID/process identity. Perl BEGIN exits77 before original server code. Lua replaces startup/prefs/media methods. No listener, polling service, artwork or audio code was added. The design does not survive firmware erasure of its entire overlay and is unqualified.

`qualification.json` is a HOST deployment gate, not runtime enablement. It records qualified=false and hardware_installation_allowed=false. The host CLI refuses installation before SSH, with no force option. No qualified installer/restart/reboot command exists behind this blocked prototype.

## Recovery tool

`scripts/fab4-reliability.py` supports read-only backup/verify and guarded file restoration. It requires an explicit host, known SSH key and Fab4/7.7.3 writable-overlay profile. Credentials come from existing keys or SB_DEVICE_PASSWORD. Explicit completion markers handle missing Dropbear exit status.

Thirteen allowlisted TinySC/app/native/config files are backed up with SHA256, uid/gid/mode and symlink status. Current targets are regular files. Corrupt backups, unsafe metadata/paths/links and foreign current content are refused. Temporary sibling upload/read-back, metadata restoration and atomic rename preserve complete versions across interruptions.

Default restoration retains recognized existing TinySC guards. Original TinySC restoration is explicit; attached USB causes refusal before writes without an additional acknowledgment, and a warning is printed. It never silently removes protection or starts services. Missing-directory/factory-reset reinstall and full service recovery are not qualified.

Live execution was backup/verify only:13 files, differences=[]; no target restoration/install writes. Root-host atomic/failure tests passed. No PID holder, device writer, coordinator or Music component was installed.

Development dependencies are existing host Paramiko and Lupa/Lua5.1, sh and Perl for tests. No new device library or permanent process. Perl stubs use the existing firmware interpreter; this is not a new Music runtime dependency.

## Continuation ? 2026-11-04

The user explicitly retained the FULL failure requirement, including overlay erasure after storage mount failure, and instructed: no hardware changes. The known /linuxrc gate therefore remains failed. The guard, maintenance helper and all test fixtures remain LOCAL ONLY. No target install, service restart, reboot, scan, decoder experiment or audio was performed.

Current read-only snapshot: same boot UUID9162ba64-822a-46e1-8ab5-2ea3c6a66740, uptime37640.51s, Jive3192/watchdog639/Base3297/web3312/proxy3282. Now Playing inactive/stopped. USB counters remain6 completed writes/29 sectors, unchanged from the previous late snapshots. The earlier one-sector write is still unattributed; it is not explained or retroactively treated as harmless.

New local work: native/sbmaintenance/helper.c and an explicit ARM build script. Production/test builds compile with -Werror.19 real Linux host tests passed in0.342s in a temporary networkless Docker container. Tests cover detached process survival, descriptor/environment isolation, PID handoff/restoration, concurrent requests, stale identities, unexpected exits, emergency PID recovery, file safety and bounded O_NOATIME metadata auditing. A private SysV IPC set verifies that snapshots do not change semaphore values. No watchdog device or decoder is used.

Both Baby and Fab4 static production builds passed ELF/dependency checks (46004bytes each). Under Docker's existing ARM binfmt emulation, both read20 semaphore slots correctly and refused production mutations on the non-Fab4 host. This is ARM ABI/emulation evidence, not actual target behavior. Production binaries contain no SB_TEST path overrides. The full restart coordinator, UI/IPC health timing, target recovery and controlled restart/reboot remain NOT qualified.

WSL again returned Wsl/Service/E_UNEXPECTED. It was not restarted; existing Docker images supplied the isolated Linux toolchain, without network downloads or changes to other running workloads. The previous17 guard/recovery tests are historical results, not claimed rerun in this continuation.
