# Watchdog-safe restart and receipts

Implemented locally and host-tested; physical coordinator qualification refused before Jive stop.

Maintenance roles include exact fixed paths for sbbase, sbbase-core, sbbase-music, sbproxy, sbwebserver, sbwebserver-music and sbmusic-session. Duplicate aliases are refused. A session requires its actual Base and web children with consistent start identities. LISTEN socket inodes for existing TCP 3483/9000/8765/80 and UDP 3483 must belong to the corresponding processes. `verify-exe` hashes the running executable inode, with PID/start/executable/parent rechecks; replacing its on-disk path cannot hide a live old revision.

The Fab4 7.7.3 r16676 profile retains two Jive allocation registrations, positive countdowns, allocation GETPID ownership, stable feeder identity and actual watchdog-device evidence. Countdown GETPID is not treated as allocation ownership. Independent UI nonce/advancing heartbeat and post-start service health remain required. No raw Jive stop, watchdog disabling, TinySC start or convenience reboot is permitted.

`scripts/fab4-music-qualification.py` reuses strict-host lifecycle transport, snapshot validation, native checked copier and the bounded coordinator. It produces a distinct WP4A3B plan and source-bound qualification receipt; legacy receipts cannot qualify this payload. Before/after/recovery running-inode receipts bind the intentionally retained proxy/web versions and new session topology. Three installed older Radio/Spotify files are explicitly replaced with the already tested fence-capable sources and restored by recovery.

Recovery is staged durably before first replacement. Per-member tar streaming avoids retaining a second full archive on the small overlay while preserving timestamps. Space accounting takes no credit for old live inodes, reserves all new destinations, atomic rollback temporary space and another 512 KiB. Installation itself never restarts Jive. The separate restart uses only the qualified native coordinator and is journaled once. A refusal blocks activation.

## Actual qualification refusal

The installed/recovery file checks, native watchdog profile, listener ownership and current role checks passed. The old live Base executable could not be opened through `/proc/3933/exe` after atomic replacement on the real old kernel/UnionFS (`ENOENT`). Its symlink/mappings and PID/start remained present. Read-only shell tracing identifies failure at the first `verify-exe` in check-running-before.sh. This is distinct from watchdog failure: feeder 639/start 923 and both Jive registrations remained valid.

Exactly one native coordinator was dispatched and returned REFUSED before Jive stop or handoff. No retry, reboot or raw process termination occurred. A local generic free-space check had earlier prevented dispatch; its phase-specific 1428 KiB post-install rollback reservation was boundary-tested and requalified, without changing the payload or weakening watchdog/identity proofs.

The hardware assumption behind live executable reads therefore remains unqualified for this filesystem. A future bounded assignment must establish and test provenance before replacement with a retained descriptor/mapping identity; do not accept an unreadable live image by matching its filename.
