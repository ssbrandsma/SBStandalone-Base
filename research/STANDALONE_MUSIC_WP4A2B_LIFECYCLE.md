# WP4a.2b TinySC lifecycle

Status (2026-11-04): implemented and host tested; not installed or hardware qualified.
See [hardware outcome](STANDALONE_MUSIC_WP4A2B_HARDWARE.md).

## Policy and configuration

The WP4a.2b owner policy supersedes the factory-reset-survival rule. Early boot
TinySC activity and limited firmware USB writes are accepted. Factory overlay
erasure may remove Base. No factory code, partition or bootloader changes are
needed or attempted. The older six-file denial prototype is not installed.

Arbitration is independent of Music. Production opt-in is the root-owned regular
file `/mnt/storage/standalonebase/tinysc-arbitration.enabled`, not group/world
writable. Base also supports explicit lifecycle CLI configuration. Persistent
opt-in enables arbitration when the legacy applet launches Base without new
arguments. Music remains disabled by default; arbitration does not enable it.

With policy OFF and no quarantine, the wrapper delegates stock service actions.
With policy ON, a private RAM latch precedes an exclusive runtime flock. Start
requests require a shared lease and no latch. Existing launch requests finish
before Base acquires the exclusive lease. Saved stock script recursive calls
reenter the public wrapper, covering delayed launches.

The latch survives Base exit/crash within this boot. This is documented quarantine,
not a claim that a stale PID proves an active server. Removing policy alone does
not release it: `sb-tinysc-control release` requires no exclusive owner and no
native Music process. Reboot clears RAM and permits early firmware startup.

## States and admission

`native/sblifecycle/tinysc.{c,h}` implements UNKNOWN, DETECTING, STOP_REQUESTED,
STOPPING, STOPPED_VERIFIED and FAILED. Suppressed service requests report why.
Running, initialized, operational, TinySC-stopped and Music-authorized are distinct.
The existing loopback listener exposes `GET /api/tinysc/lifecycle` with current
PID/start-time/boot identity, flags, freshness and bounded blocker records.

Detection recognizes qualified TinySC script paths, native children, descendants,
factory nice launchers and delayed stock service shells, never all Perl processes.
PID/start-time identities survive orphaning via a bounded boot-bound RAM journal,
written before stop requests. Corrupt journals, uncertain identities and bounds
violations fail closed.

The worker requests stock `stopscan`, `stopserver`, `stopresizer`. PID files must
match verified roles; unrelated live PIDs are refused. The old numeric-PID stock
service retains a small check-to-signal race; no atomic pidfd guarantee is claimed.
Two empty observations under the lease establish STOPPED_VERIFIED. Observations
expire after 1250 ms. Unexpected reappearance revokes admission, requests stop
and leaves a sticky conflict: no automatic scan/playback resumption.

Database/root adoption, scan, playback resource creation and SlimProto start all
check admission. Runtime invalidation preserves WP6a generation/revocation.

## Firmware integration and bounds

The pinned generator guards SqueezeCenter startup, media switching, udev,
wipe/rescan, timers, start windows, restart, preference writing and common actions.
Unlatched behavior remains stock. The saved script is root-owned 0400 and invoked
by the checked shell path. The obsolete Base TinySC-crash guardian is disarmed
under quarantine. Existing menu behavior is preserved in host fixtures.

One worker, 128 KiB requested stack; 128 identities, 4096 process rows, 16 ancestry
passes, bounded arguments/journal. Normal polling is 500 ms, stop verification
100 ms, default stop deadline 15 s. Condition waits and notification descriptors
avoid busy polling. Startup gates listener admission; ongoing detection/stop work
stays off the protocol loop. Stop subprocess groups have bounded cleanup. There
is no TinySC killall or forced kill.
