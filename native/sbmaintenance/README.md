# Unqualified maintenance primitives

These explicitly invoked primitives are not a complete or qualified restart
procedure. They are not installed or included in normal applet packages.
The user retained the full failure model on 2026-11-04 and prohibited hardware
changes; the factory-reset/overlay-loss gate therefore remains failed.

`helper.c` provides:

- Read-only process start-time/executable identity and existing SysV semaphore
  snapshots. It never opens `/dev/watchdog`, creates semaphore sets or modifies
  semaphore counts.
- Detached launch with closed SSH descriptors, a separate session and ignored
  SIGHUP. Jive launch uses a fixed executable/environment, without USB Lua paths.
- A passive maintenance PID holder; it does not feed the hardware device.
  On an interrupted transaction it deliberately waits for explicit recovery
  rather than expiring into a reset. A future coordinator must bound startup
  and recovery attempts, restore normal monitoring, and verify holder exit.
- Serialized, atomic PID-file comparison/replacement, requiring executable,
  start identity, owner, file type and permissions. Emergency adoption accepts
  only a dead recorded PID and an independently verified live replacement.
- TERM only for recognized process roles with the recorded start identity.
- Bounded metadata inventories of the media root and named TinySC directories,
  with descriptor-relative no-follow/no-atime reads. File contents are never
  opened. Inodes are diagnostic and not stable identities across FAT mounts.
  JSON filename bytes are escaped individually and are not Unicode-normalized.

Production mutations require the exact Fab4 firmware profile. Test path
overrides exist only in builds with SB_MAINTENANCE_TEST and are absent from the
production binary. The host process fixture contains no firmware/audio APIs
and must never be deployed.

`scripts/build-maintenance-arm.sh` builds static Baby/Fab4 ELF files but does
not deploy or activate them. Baby compilation does not authorize Baby use;
production mutation checks still require Fab4.

Native tests exercise real fork/session/PID-file operations and a private
container IPC namespace. ARM tests use Docker's existing binfmt emulation.
Neither is hardware watchdog, Jive UI, restart or reboot qualification.

The complete coordinator, target recovery, startup health/IPC timing and
hardware fault cases remain unqualified. Do not use individual primitives as
a substitute for the missing qualified restart procedure.
