# WP4a.4 — Implemented lifecycle; hardware qualification incomplete

The native SQLite/scanner implementation is complete and locally qualified.
**The requested full physical USB catalogue has not been published.** The first
attempt was safely cancelled by an observer guard exception. One explicitly
owner-authorized replacement attempt completed traversal but failed on one
unsupported-signature file. Active LEVEL3 Base also blocks physical
My Music activation. This report does not claim full acceptance.

## Implemented

- Explicit create/open/scan/close and read-only production reopen without scan.
- Schema3 checks and transactional v1→v2→v3 migration, no downgrade/reset.
- Bounded native MP3/FLAC/Ogg scanner using existing WP1 worker and WP6a roots.
- TEMP scan membership eliminates unchanged track-row writes and content revision
  churn; unchanged content skips persistent snapshot publication entirely.
- Two closed checksummed USB snapshots, private tmpfs SQLite working copy,
  configurable1–64MiB DB budget/default32MiB, no internal-flash fallback.
- Explicit backup/restore/recover, bounded interrupted-file quarantine and
  complete-scan-only persistent publication/deletion reconciliation.
- Same-worker asynchronous browse refresh, media/offline/reinsert generations
  and revision notifications; production session/runtime opt-in store-open path.
- Static Baby/Fab4 artifacts and deterministic lifecycle/refresh/launcher tests.

## Source index / reuse

`native/sbmusic/music_library.[ch]` and `music_library_scan.inc` retain the WP1
connection/worker/schema and add migration validation, caps, TEMP membership,
counters and refresh. `music_snapshot.[ch]`, `music_snapshot_view.[ch]` and
`music_library_tool.c` implement bounded persistence/admin/refresh. Existing WP2
`music_scanner.[ch]`/metadata helper, WP6a storage/identity, WP3b browse dispatcher
and async broker are reused. `music_storage_linux.c` supports old udev USB serial
identity. Base `music_runtime.inc`/`main.c`, `music_browse.c` and `music_session.c`
add explicit open/store and non-serving diagnostics without playback ownership.
CMake and both ARM build scripts include the new artifacts/tests.

New tests: `tests/music/test_library_lifecycle.py`, `test_snapshot_view.py`,
`snapshot_view_driver.c`, `test_store_launcher.py`; existing scanner/library
fixtures add measurements. Dependencies remain existing vendored SQLite,
C/POSIX/pthreads, shared SHA-256 and native contracts. No decoder, artwork renderer,
metadata package, LMS/TinySC/Perl dependency, network port or default startup
activation is added. Earlier unrelated uncommitted phase work is preserved.

## Physical boundary and rollback

Only192.168.1.244 was contacted. The original122,880-byte11-track RAM DB is backed
up and new production code reopens it read-only without scan/listeners. The
first attempt visited1,088entries/read11tracks with zero parser errors in
14.199s, then cancelled. The authorized replacement visited1,376entries and
recognized273tracks in165.826s with one unsupported-signature error and no
publication. A validated no-atime read confirmed the offending file's first64KiB
are zero. The own USB directory remains empty.
Base/Jive/watchdog/proxy/web stayed running; TinySC is stopped and audio inactive.
The exact first guard fault was not recorded. Read-only diagnosis reproduced
truncated HTTP reads in the observation path; a corrected bounded HTTP/1.0 read
passed20/20 idle samples. The scanner/runtime binaries did not need modification.

Local tests, sanitizer groups, ARM builds and QEMU pass (see test limitations).
Hardware complete-index counts/timings/CPU/RAM, persistent reopen, unchanged scan
and full My Music browsing are still unverified. Physical My Music retains the
old11-track session. No safe supervised replacement of LEVEL3 Base is qualified;
do not bypass it. Backup/restore software is qualified locally; automatic active
Base rollback is not. No real power cut/unplug test or native audio was performed.

Next: explicitly decide safe handling of the unsupported-signature file, qualify
any policy change locally and authorize a further physical scan. Then qualify
Base-only transition/rollback before exposing the catalogue in
physical My Music. Stop at database/scanning; no playback/decoder phase begun.

## Required documentation

- [Database](STANDALONE_MUSIC_WP4A4_DATABASE.md)
- [Scanner](STANDALONE_MUSIC_WP4A4_SCANNER.md)
- [Storage](STANDALONE_MUSIC_WP4A4_STORAGE.md)
- [Recovery](STANDALONE_MUSIC_WP4A4_RECOVERY.md)
- [Test results](STANDALONE_MUSIC_WP4A4_TEST_RESULTS.md)
- [Performance](STANDALONE_MUSIC_WP4A4_PERFORMANCE.md)
- [Hardware](STANDALONE_MUSIC_WP4A4_HARDWARE.md)
- [Rollback](STANDALONE_MUSIC_WP4A4_ROLLBACK.md)

Raw receipts/source hashes/build logs/exports remain in `.local/wp4a4/`.
