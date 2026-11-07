# WP4a.4 — Explicit scanning and incremental reconciliation
Qualification date: 2026-11-06. Target: owner-designated Fab4 at 192.168.1.244 only.
This package stops at database/scanning. No audio playback, application handoff,
Jive/watchdog restart, reboot, remount, firmware change or active Base replacement.
Raw local receipts: `.local/wp4a4/`; source/artifact SHA-256 receipts are
`source-provenance.json`, `artifacts.json`, and `staging.json`.

## Reuse and execution

WP2's native coordinator, helper process, metadata DTOs, artwork references and
WP1 worker are reused. WP6a supplies validated descriptor-relative roots and
generation checks. MP3 (ID3 variants), FLAC and Ogg Vorbis are indexed; no decoder,
artwork rasterizer, internet dependency, LMS, TinySC or Perl is added.
No automatic insertion/startup scan was introduced.

The isolated administrator process is `nice(10)`. It uses one metadata child at
a time, batches of at most 16 records, 10 ms yields and 25% work-duty pacing.
Sleeping is a pacing mechanism, not an exact CPU quota. Database requests are
serialized with bounded asynchronous browse work. A busy initial admission can
be retried for up to five seconds before any scan has been accepted.

Bounds retained from WP2: 1,024-byte storage paths including terminator, 16
directory levels, 16 retained errors, 16 pending records, one helper, 256 KiB
metadata buffer and 768 KiB metadata read budget. Helper address-space/CPU/stack/
descriptor rlimits remain active. No entire audio file is read for metadata.
The scanner reports its descriptor bound; actual coordinator FD samples include
database/observer overhead and are recorded separately in performance evidence.
Errors beyond the bounded detail ring still increment counters and invalidate
successful reconciliation. The isolated job has a one-hour cooperative deadline.

## Identity, unchanged files and deletion

Track identity is scoped by compound media identity and relative path. Size and
mtime/fingerprint evidence avoids reparsing unchanged files. Where a filesystem
has qualified inode semantics, a confirmed unique rename preserves track ID.
FAT/NTFS inode identity is not assumed: an uncertain rename is remove+add after
successful traversal, rather than merging unrelated tracks.

`TEMP scan_seen(id PRIMARY KEY)` replaces persistent per-track epoch updates.
Lookups overlay current-scan membership for hardlink/rename handling. Unchanged
tracks do not mutate track rows, metadata or content revision. A constant number
of private-RAM administrative statements still advance/finish scan bookkeeping.
SQL counters distinguish a main-table DELETE that reads TEMP from a TEMP write.

Only a complete, error-free, current-generation traversal may reconcile missing
tracks. Empty folders are pruned bottom-up after successful changed scans, with
a bounded 16-pass walk; occupied ancestors/root remain. Incomplete/cancelled scans
never sweep missing tracks. The isolated persistent workflow publishes only a
fully successful closed working copy: partial additions remain private and are
discarded on failure, preserving the last published library.

Symlinks are not followed; escaped/looped directories, changed files/directories,
permissions, excessive paths/depth and stale generations fail conservatively.
Internal caches, own snapshot directories and filesystem metadata are ignored.
The physical scope is the entire `/media/sda1`, not only the old 11-track album.

## Progress/cancellation

Existing scan states/counters report visited entries, directories, recognized
audio, additions, changes, unchanged tracks, removals, errors and elapsed time.
No percentage is invented without a denominator. `scan-detached` owns the fixed
0700 RAM directory `/dev/sbmusic-library-job`; a root-owned empty 0600 `cancel`
file requests cancellation. `result.json`, `scan.log` and `exit` are finite job
receipts, not a new daemon, network endpoint or per-file notification stream.
SIGHUP is ignored before detach; TERM/INT request cooperative cancellation.
