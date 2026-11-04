# WP4a.2b recovery and rollback

Status: native file rollback is host tested. Full operational device rollback is
implemented but unqualified. The actual Touch needs no rollback: no persistent
candidate or overlay replacement was installed. Original state was verified and
the temporary RAM helper removed after preflight refusal.

## Backups and candidate

Owner originals are in `.local/wp4a2b/hardware/snapshot`: `snapshot.json`,
`originals.tar`, and `files/`. The manifest records SHA-256, bytes, UID/GID, mode
and mtime. Sixteen files are backed up, seven paths recorded absent. Candidate
`plan.json` binds the snapshot and all replacement/recovery payload hashes.
Do not substitute originals from the older denial-prototype snapshot.

Before replacement, the installer requires known current revisions, enough space,
root/same-device descriptor-safe paths, native preflight, and a complete verified
durable recovery payload in `/mnt/storage/standalonebase/wp4a2b-backup`.
Original sources retain original mtimes. No backup or cache is put on USB.

## Interrupted installation

Policy is installed last. Each attempted target is recorded before copying, so
a post-rename failure is included in rollback. Known original/new/recovery
revisions may be restored in reverse order using the native copier. Unknown
revisions or unsafe paths stop restoration rather than overwrite owner changes.
Originally absent paths are removed only through the exact target whitelist.
There is no Jive restart, reboot or intentional TinySC start in this file-only
failure path. RAM cleanup uses two fixed paths and never recursive deletion.

Tests exercise every prefix of the eleven-file plan, failures after each rename,
wrong expected hash/absence, symlinks, forbidden factory targets and unknown owner
changes. These prove native file restoration/planning in a host sandbox, not live
firmware timing or recovery on this old unionfs.

## Owner operation after a future qualified installation

The available command is:

```text
python scripts/fab4-lifecycle.py rollback --snapshot <snapshot-directory> --candidate <candidate-directory>
```

Use the original designated device and strict known-host SSH; password is read
from `SB_DEVICE_PASSWORD`, never written to reports. This is an operation for an
installed matching candidate, not a command to run on the current unchanged Touch.

The procedure verifies current files, idle audio and ordinary watchdog/Jive
supervision; uploads a verified private RAM helper; restores original Base binary
before its sole existing applet supervisor can relaunch it; removes opt-in; stops
only the exact verified Base incarnation; explicitly releases quarantine after
the exclusive lease and native Music processes are absent; restores originals
and removes newly installed files. The controller is retained until release.
Heartbeat stop is signalled in RAM, so the timer can end without another Jive
restart. The already loaded applet uses unchanged service arguments and original
Base can be relaunched by its existing supervisor.

Original hashes/metadata and normal Jive/watchdog identities are checked. The
original watchdog PID remains Jive throughout this Base-only rollback. User
configuration, Radio/Spotify files, USB content, watchdog configuration and
factory partitions are preserved. Normal TinySC startup is permitted after release;
rollback never deliberately starts it.

## Restart failure recovery is different

The detached coordinator may restore known-good Base and a heartbeat-enabled
original applet while retaining TinySC quarantine. This is bounded recovery,
not full owner rollback. RECOVERED still fails qualification; no reboot follows.
FAILED_HELD retains explicit maintenance supervision pending verified healthy
UI handoff. Do not terminate that holder, edit the watched PID blindly, start a
second feeder, or invoke the old unbounded hold prototype. No such state was
entered on the actual Touch in this task.
