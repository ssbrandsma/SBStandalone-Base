# WP4a.2d storage and SQLite

The only selected music source is:

`/media/sda1/mp3/Fleetwood Mac - Rumors-1977-FAF`

Existing USB mount /dev/sda1, major/minor 8:1, FAT32, mount ID 20. No mount options
were changed. WP6a validates the mounted root and a descriptor-relative child
scope, rejects unsafe symlinks and requires no-atime access. Source identity and
mount generation are session scoped as appropriate to existing WP6a evidence;
no path/label alone is treated as persistent identity. Native scan uses only this
validated child. No entire-stick walk, insertion scan or executable from USB.

TinySC must be verified stopped under the native lease before any root/database
adoption, scan or browse activation. Current actual lifecycle is enabled,
STOPPED_VERIFIED/fresh/operational, zero identified or uncertain processes. Stock
service stop is reused if needed, never killall perl. TinySC was absent, so no
intentional start or physical active-to-stopped test was performed. Host tests
cover active shutdown, races, children, stale PIDs and stop failures. Unexpected
appearance revokes Music admission; no automatic scan/playback resume.

Disposable DB: `/dev/standalone-music-test/library.db`. Dedicated tmpfs mount
ID 21, nosuid,nodev,noexec, size=8192k. Root is owned by root, mode 0700. Maximum
8 MiB applies to this filesystem, not whole process memory. Actual file 122880
bytes (120 KiB); final filesystem usage 128 KiB, 8064 KiB available. There is no
internal-flash/USB database fallback or .Squeezebox cache use. Software binaries
and explicit arbitration policy are installed on internal overlay as authorized.

The first mount omitted mode=0700 and inherited tmpfs default 1777. Native storage
admission refused before DB creation. Evidence first-activation-refusal.txt shows
DATABASE_ABSENT, no scan and healthy core fallback. After verifying same empty
mount and private ownership, chmod 700 corrected setup before the first actual
scan. The session script now supplies mode=0700 on new mounts. It did not loosen
validation, overwrite an existing DB, truncate content or change USB options.

The one scan: state COMPLETED, 15 visited, 11 added MP3 tracks, errors 0, 8014 ms.
The four other entries are the existing NFO/sidecar images, not audio tracks.
WP1 owns the single target SQLite connection/worker; WP2 writes through its broker.
Approved DELETE journaling is retained. The new explicit INTEGRITY request runs
PRAGMA integrity_check on that same worker after scanning, before browse publication.
Result ok; schema 3, completed library revision 5, eleven distinct track IDs.

Independent verification copied the committed, journal-free DB after the scan
and opened only the HOST copy with mode=ro. Its integrity_check is ok and tracks
count/distinct count are both 11. No second target database connection was opened.
The schema is native library_meta/media/tracks/contributors/etc., not a Logitech
TinySC database. Live library was retained unchanged for browsing.

Metadata: Fleetwood Mac, Rumours, genre rock; track numbers 1-11, actual titles and
duration fields. Year is zero/unknown in these native records; no 1977 value was
invented from the folder name. Host format tests cover more encodings/formats;
this physical corpus qualifies only these eleven MP3s.

No repeated music-tree scan. Existing WP6a bounded mount revalidation remains at
30-second idle intervals, alongside the separate TinySC process monitor; neither
automatically scans inserted media. Physical removal/reinsert was not tested.
USB completed-write counters remained six writes/29 sectors during observation.
That is block-level evidence, not a complete USB content hash. No USB write,
remount, format, deletion, thumbnail/cache or metadata file operation was issued.

Follow-up 2026-11-05: the native Music binary was replaced with the expired-Comet-
client correction. The old session/database was preserved after shutdown and a
new bounded session started. Current hashes, PID, scan measurements, RAM usage,
activation evidence and the additional rollback prerequisite are recorded in
[the dated Jive follow-up](STANDALONE_MUSIC_WP4A2D_JIVE.md#reconnect-correction-deployed-2026-11-05).
Earlier measurements/hashes here describe the original 2026-11-04 session.

Latest follow-up: stock SlimBrowser useContextMenu parsing is corrected and
deployed (PID 3933; scan 4561 ms; three preserved/current 120 KiB RAM DBs). See
[the second dated Jive follow-up](STANDALONE_MUSIC_WP4A2D_JIVE.md#stock-slimbrowser-parameter-correction-2026-11-05)
for current hashes, validation, previous-session preservation and rollback.
Physical category visibility passed the owner's first follow-up; filled lists
and navigation await the next owner check.

Final owner observation: filled Artists -> Fleetwood Mac -> Rumours -> 11 tracks
is confirmed on the physical Touch after the two fixes. See the Jive report for
the exact confirmation scope; audio remains disabled.
