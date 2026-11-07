# Rollback qualification

**No valid WP4a.3d rollback generation is available or claimed.** This is the
decisive pre-stop refusal condition. No process replacement or file deployment
occurred, so no rollback was required or attempted.

The old live image cannot be opened; it cannot be saved as trusted process bytes
or resumed after exit. The restored installed image is independently hashable,
but that does not prove live bytes or establish no-scan startup compatibility.

Actual known browse source `2b666ba` and current `music_runtime.inc` both require
`--music-scan`, refuse an existing database, open with create enabled and run the
scanner. The existing wrapper's one-shot browse marker is already consumed; a
normal subsequent launch falls back to `sbbase-core`, which has no Music library.
Clearing that marker alone would not make the existing DB reopen successfully.

WP4a.3b's preserved recovery wrapper renames the old database and reconstructs the
album by scanning. WP4a.3d forbids this. The existing full coordinator also restarts
Jive, forbidden here. Neither is reused or represented as a compatible rollback.
All old staging, backups, receipts and RAM databases were left in place.

A future qualified rollback within this assignment requires an independently
verified browse-only artifact that reopens the exact schema/source/generation
read-only, plus a demonstrated single-attempt Base-only supervisor path. A newly
patched older build is not known-good merely by retaining its revision name: its
new source bytes, build, behavior and recovery failure cases must be qualified.
Candidate and rollback must both preserve library checksum, listener exclusivity,
Jive/watchdog, TinySC inhibition and stock browsing without any scan.

Do not stop PID3933 using the generic native `terminate` command as a shortcut.
Do not replace its active path or call `launch-restart`. Do not relax LEVEL 1
requirements for the replacement or substitute a filename/argv match as byte proof.
