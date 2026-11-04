# WP4a.2b implementation

Status: host validated; hardware preflight blocked deployment. Existing earlier
work-package changes were retained. No unrelated reset, commit, release or push.

| Source | Change |
|---|---|
| `native/sblifecycle/tinysc.{h,c}` | Detection, stock stop worker, journal, flock/latch, freshness, release |
| `native/sblifecycle/control.c` | Root service/detect/release/version CLI |
| `native/sbbase/src/main.c` | Independent opt-in, startup gate, notifications, lifecycle HTTP status |
| `native/sbbase/src/music_runtime.inc` | Admission before root/database/scan; sticky conflict revocation |
| `native/sbbase/src/music_playback_glue.inc` | Playback/resource and stream-start checks |
| `native/sbmaintenance/helper.c` | Production maintenance CLI, strict Fab4 profile |
| `native/sbmaintenance/watchdog_probe.inc` | Read-only feeder/configuration/PID/IPC validation |
| `native/sbmaintenance/coordinator.inc` | Detached restart, handoff, child health/deadlines/recovery |
| `native/sbmaintenance/quiesce.inc` | Verified Jive/Base/decoder stop and bounded recovery escalation |
| `native/sbmaintenance/internal_files.inc` | Whitelisted atomic copy, original restore, SHA and durability checks |
| `native/common/process_identity.h` | Existing helper identity parser shared |
| `native/common/process_spawn.h` | Bounded inherited-FD cleanup, including high descriptors |
| `native/common/descriptor_walk.h` | Existing WP6a descriptor walk shared |
| `native/common/sha256.h` | Existing storage helper SHA-256 shared unchanged |
| `applet/LifecycleHeartbeat.lua` | Advancing actual UI challenge witness and rollback stop marker |
| `applet/StandaloneBaseApplet.lua` | Heartbeat, quarantine-aware conflict/guardian handling |
| `packaging/fab4-reliability/patch_tinysc_lifecycle.py` | Pinned firmware patches and minimal installed-Base/recovery variants |
| `packaging/fab4-reliability/restart-actions.sh` | Fixed preflight/stop/start/health/recovery phases |
| `scripts/fab4-lifecycle.py` | Target-restricted backup, candidate, receipt gate, install, activate, restart, reboot, rollback |

Copy operations allow eleven exact overlay targets and the fixed private backup
tree. They reject symlinks, cross-device traversal, unsafe ownership/modes,
oversize input and unexpected current SHA. Bounds: 8 KiB buffer, 2 MiB per file,
4 MiB tar payload. Same-directory atomic rename and file/parent fsync are used;
no global sync, factory block write or USB path.

The installer validates original hashes/modes and a source-bound host receipt.
It uploads the production helper to RAM and verifies watchdog/stack before
creating the persistent recovery payload. Backup/source hashes and file/directory
flushes precede replacement. Policy is last. Interrupted-deployment restoration
includes attempted copies that failed after rename. Unknown revisions are refused.

Base-only activation leaves the existing applet supervisor as sole launcher.
Restart/reboot attempts are recorded durably locally before dispatch; lost SSH
acknowledgement cannot authorize a repeat. Reboot requires restart plus postchecks.

CMake/manual ARM scripts link the lifecycle module and Threads. Separate controller
and maintenance builds support Baby/Fab4; mutating maintenance requires the qualified
Fab4 profile. Production binaries exclude test overrides. Packaging includes
controller/heartbeat; firmware overlay activation remains explicit.

No new runtime network service, decoder, image subsystem or external dependency.
Lupa/Lua 5.1 is development-only. WP6a path and storage hash regressions pass after
sharing their helpers. The Music runtime ARM binary is a host build artifact only;
it was neither selected for the candidate nor uploaded to the Touch.
