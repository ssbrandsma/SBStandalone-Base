# WP6a firmware integration and TinySC paths

2026-11-02. Evidence is the **previously captured installed Fab4 7.7.3 r16676 firmware**, under ignored `.local/fab4-music-research/device`, not an old forum description. No SSH/device session was opened for WP6a. The original [evidence ledger](../docs/research/FAB4_LOCAL_MUSIC_EVIDENCE.md), [profile](../docs/research/FAB4_LOCAL_MUSIC_PROFILE.json) and new [source hashes](STANDALONE_MUSIC_WP6A_SOURCES.json) establish provenance. Paths below omit the workstation capture prefix.

## Existing responsibilities

| Component | Confirmed installed behavior | Reuse in WP6a |
|---|---|---|
| Linux kernel | Block/partition devices, USB/MMC events, mounted filesystem state; captured kernel 2.6.26.8-rt16 | Authoritative proc mount state and native descriptors |
| udev `local.rules:17-18` | Calls `/etc/udev/scripts/mount.sh` for block add/remove | Preserved; no new rule or automounter |
| `mount.sh` | Blacklists loop/ram/mtdblock; tries pmount or mount/fstab; then auto-mounts `/media/$(basename DEVNAME)`; vol_id detects filesystem | Observe its results, never invoke it |
| `mount.sh` filesystem branch | NTFS uses `/usr/bin/ntfs-3g`; VFAT uses rw, fmask/dmask 0022, codepage437, iocharset iso8859-1, utf8=1, shortname=mixed | Preserve existing filesystem/mount policy |
| `mount.sh` removal | umounts matching entries from `/proc/mounts`, removes an empty automount directory; restarts samba on add/remove | No replacement/eject orchestration |
| udev `60-persistent-storage.rules` | Imports vol_id UUID, USB/SCSI/MMC hardware identifiers and partition suffixes; creates by-uuid/by-id links | Conservative native identity evidence |
| MediaManager Meta | Registers existing mount/menu/eject services, calls mmStartupCheck, adds addUeventListener callback to udevEventHandler | Independent listener pattern only |
| MediaManager Applet | Checks `/proc/mounts`, shows USB/SD menu, waits up to 60s for mounting; eject handlers then umount, retries/forced lazy unmount, warning UX | Stock implementation remains unchanged |
| SqueezeCenter Applet/Meta | Chooses storage, writes prefs, starts/restarts/rescans TinySC and presents server actions | Never called by new integration |
| TinySC init/runtime | Init script launches Perl server/resizer; server performs library/scanner work under selected `.Squeezebox` data policy | Source reference only, no dependency |

MediaManager has `mmRegisterOnMountHandler`, but inspection finds registration/storage of the table and **no active execution of that table**. Its `addMountedDevice` creates a menu item; `_mountingDriveTimer` calls addMountedDevice and eject warning, not an on-mount callback dispatcher. A comment mentioning scanner startup is not executable evidence. `mmRegisterOnEjectHandler` really executes before stock unmount, but does not provide an asynchronous barrier; registering a callback there would not make Base safe eject complete. Neither hook is used by WP6a.

## TinySC startup/rescan routes found

| Path | Installed source anchors | Consequence |
|---|---|---|
| Jive applet configuration with mounted media | SqueezeCenterMeta `configureApplet` -> `squeezecenterStartupCheck`; Applet lines 66-131 | Selects prior/fallback USB/SD, may write prefs, unconditionally restartServer(true) when any mounted drive exists |
| Stock media submenu Start | Meta registers `startSqueezeCenter` with MediaManager; Applet startSqueezeCenter -> _startServerWindow -> _squeezecenterAction(start) | User selecting stock Start can launch TinySC |
| Advanced Squeezebox Server Start | Meta advancedSettings menu -> settingsShow -> startServer callback -> same start window | Another explicit start path |
| Switching selected media | mmSqueezeCenterMenu -> switchServerToDifferentMedia -> _writeSCPrefsFile -> restartServer | Device selection can restart TinySC |
| Wipe/rescan | settings menu -> _confirmWipeRescan -> _wipeRescan/_wipeTimer -> _startServerWindow | Stock destructive wipe followed by start; never reused |
| Action execution | _squeezecenterAction lines 274-346 | Non-stop actions call `/etc/init.d/squeezecenter <action>`; SCGuardianTimer notified separately |
| Shell/init entry points | `/etc/init.d/squeezecenter` start/restart/rescan | start launches slimserver.pl and gdresized.pl; restart stops/sleeps/starts; rescan delegates to restart in this installed script |
| TinySC's own scan/remote commands | Installed LMS scanner/control references from Phase 0 | Reachable only if stock server started; no new integration route |

SqueezeCenter's own `udevEventHandler` still contains insertion/removal calls to `_mountingDrive`/`_deviceRemoval`, but its Meta does not register that handler/listener, and the active MediaManager listener uses MediaManager's registered service. The called mounting methods are not defined in the captured SqueezeCenter applet. A separate insertion-triggered TinySC start cannot be claimed from its comments or this orphan handler. Active insertion shows stock media UX; its Start/device-selection actions above remain a risk. The commented block around Applet lines 786-793 is not an active restart route.

`SqueezeboxFab4:SCGuardianTimer` watches whether TinySC dies after start/restart and displays a warning. Its captured code does **not** automatically restart TinySC. The squeezeplay init script itself starts Jive, which in turn configures applets; it also extends Lua paths from media. Captured evidence does not include complete rcS/rc symlink/startup configuration, so shell/boot/third-party startup routes cannot be exhaustively excluded. No claim that all firmware startup paths are blocked is made.

## New adapter and exclusion boundary

Native [music_storage_adapter.h](../native/sbmusic/music_storage_adapter.h) consumes bounded uevent hints, exposes immutable current model snapshots/wakeup fd/deadlines, reconciles actual OS mounts and validates on a storage worker. Startup discovery also catches already mounted media. Disk removal revokes matching partitions immediately. Available media has a bounded reconciliation backstop; delayed mounts have a finite retry window. No filesystem query is put on the existing Base control loop.

[MusicStorage.lua](../applet/MusicStorage.lua) is a small **unregistered, opt-in adapter seam** with an injected native bridge and `addUeventListener` service wrapper. It requires enabled=true and platform=fab4 before registering anything. It forwards block add/remove/change hints, extracts only a device basename, uses available exactly representable kernel SEQNUM values to refuse stale events, and provides bridge state. It never calls MediaManager's menu/start/device-selection/eject actions, any SqueezeCenter service, shell, filesystem, playback or database. It adds no music UI and does not fork MediaManager. Host Lua tests inject bridge/listener stubs, not firmware services.

There is intentionally **no enabled Lua-to-C IPC endpoint or sbbase runtime wiring** in WP6a: the product gate remains off and the storage library is not linked into sbbase. The native adapter and Lua ingress are executable/tested components for the future existing private control connection; the bridge implementation/scheduler registration must be connected in a separately enabled integration. Its contract must preserve ordering, immediate removal revocation, wakeup pumping, retry deadlines and source/generation checks. Do not activate the Lua listener alone without that implementation. Legacy events lacking SEQNUM rely on ordered ingress and authoritative reconciliation; they do not prove ordering of historical kernel events.

Within this new integration, TinySC startup paths are excluded by never exposing/calling those actions. This does **not** suppress the stock applet's independent startup check or stock menus. Before a future product opt-in, WP6b must install a narrow, explicitly reviewed override of the SqueezeCenter startup/menu routes while leaving udev mounting intact, qualify applet configure order, and implement eject quiescence. No global disabling/deletion, firmware preference/service changes, remount, formatting, media writes or device deployment occurred here. Baby does not load this module or register a listener; storage constructors return unavailable without allocations/polling.
