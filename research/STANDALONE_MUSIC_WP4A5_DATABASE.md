# WP4a.5 — Database: persistent SQLite lifecycle

## Storage design (unchanged)

Persistent checksummed USB snapshots (`SBMUSIC1` header + SQLite) with a private
RAM working database. Publication is complete-scan-only; a failed publication
preserves the previous valid snapshot. No internal-flash fallback; the music
subsystem writes only under the dedicated store (`.standalone-music`) and the
RAM working area. USB user files are never modified (verified: no mp3-tree
writes during the whole session).

## Lifecycle qualification (Gate C)

New creation, existing reopen, reopen-without-scan, explicit full scan,
incremental scan, clean shutdown, crash recovery, schema validation, stable
track IDs, media generation validation, revision notifications, atomic
publication, interrupted-publication recovery — implemented in prior sessions
and exercised by host tests (`music_library_*`, `music_stream*`) plus the
device reopen path used by every cutover (`--music-open`, no scan).

## This session's verification

- Every cutover (6b/7/8b) and rollback opened the store with `--music-open`
  and logged `MUSIC_LIBRARY bytes=270336 revision=21 journal=delete` — no scan.
- Unchanged-scan stability: revision 21 constant across all device activity;
  the store snapshot mtime (Nov 6 19:22) never moved.
- Ownership-side revision is a separate counter (owner generations), not the
  library revision — confirmed distinct in code and logs.
- Post-test: library still browses 273 tracks with identical IDs (track 46
  stable across all runs).
