# WP4a.5 — Scanner: recoverable per-file failures

## Classification (as implemented, prior session)

Recoverable per-file errors (recorded, scan continues, no fabricated track):
unsupported audio signature, corrupt MP3 header, invalid ID3, truncated file,
unsupported codec, missing metadata, file-local parser failure in a healthy
directory traversal. The known file
`Dr Dre - The Chronic/Lyrical Gangbang.mp3` (≥64 KiB leading zeroes) records as
an unsupported-signature skip; the file itself was never modified.

Fatal scan errors (abort, preserve previous snapshot): USB removed/remounted,
generation change, root invalidation, incomplete traversal, unrecoverable SQLite
or I/O failure, cancellation, scanner-wide helper failure. No blanket exception
swallowing: descriptor/generation/filesystem failures still fail loudly.

## Full scan result (Gate B, prior session, verified against live snapshot)

- Entries traversed: 1376. Tracks indexed: 273. Albums: 54. Artists: 16.
- Duration ~166 s. One recoverable diagnostic (the Dr Dre file).
- This session verified the live USB snapshot
  `/media/sda1/.standalone-music/library.0.snapshot` is byte-identical (md5
  `972e35a7d97a053950b8552082166451`) to the host copy, and that its carved
  SQLite holds 273 tracks, all `codec='mp3'`, revision 21, track 46 =
  `99 Luftballons _ Nena`. Snapshot container: 192-byte `SBMUSIC1` header +
  raw SQLite.

## Code references

`native/sbmusic/music_scanner.c` (codec gate: mp3/flac/ogg, per-file errors),
`music_library_scan.inc` (TEMP membership, complete-scan-only publication),
`music_snapshot.c` (checksummed slots). No scanner changes were needed or made
in this session.
