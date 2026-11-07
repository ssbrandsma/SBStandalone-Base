# Read-only Fab4 qualification

Only 192.168.1.244 was contacted, using existing strict-known-host SSH. Firmware 7.7.3 r16676 and boot 9162ba64-822a-46e1-8ab5-2ea3c6a66740 are unchanged. Jive 3192/start 55166, watchdog 639/start 923 (actual FD1), Base 3933/start 17600372, proxy 3282/start 56496 and web 3312/start 56733 retain the known baseline. Native preflight confirms both Jive allocations in the 20-slot IPC set, positive countdown/feeder rules and exact listener owners. No session supervisor is running.

Current installed file hashes/modes/ownership match the restored WP4a.3b snapshot with no differences. That is file/receipt consistency, not live Base proof. Native results: Base LEVEL 3/open errno 2/zero bytes hashed; proxy and web LEVEL 1 with expected SHA, size, parent and matching kernel mapping/device/inode. Pin rehash on proxy/web succeeds without any file replacement; Base cannot create an initial pin.

Proc stat/status/cmdline/maps/mountinfo/fd and exe readlink and upper branch/whiteout observations were retained. The shell stat command is unavailable. The known Base command line still includes its original startup scan flag; this investigation sent no new scan request. Jive/watchdog and service identities are identical before and after the diagnostic.

TinySC remains freshly STOPPED_VERIFIED/count zero, Now Playing inactive/stopped, and a read-only original-protocol browse query returns 11 tracks with playback actions disabled. USB counters before/after remain six writes and 29 sectors written on sda and sda1. USB mount identity remains /media/sda1, mount 20, 8:1, vfat. No USB file was modified. The 8 MiB RAM library mount remains present with its three existing DB files.

A private /dev RAM diagnostic helper was hash-verified before execution and removed with its empty directory afterward; cleanup is recorded. No active executable, persistent installed file, service, watchdog, decoder, factory partition or other device was changed. Physical screen interaction was not independently rechecked.

Qualification result: **failed for current Base**. No ready deployment generation was prepared and WP4a.3b activation must not resume without a new trustworthy baseline.
