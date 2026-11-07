# WP4a.4 — Physical attempt and remaining qualification

2026-11-06. Strict-known-host SSH connected only to owner-designated Fab4
192.168.1.244, firmware 7.7.3 r16676, kernel 2.6.26.8-rt16. Native probe validated
vfat `/media/sda1`, UUID `1643-62E9`, compound hardware identity
`usb:18a5:0245:12440000000002DF:partition:1`.

## Completed physical observations

- Backed up original 122,880-byte schema-3 RAM library; SHA-256
  `7ef9c34cf2e63fe53904af50eb8ff21d3c02f259fa25b342af671f08f314d006`.
- New static production Base `--music-open-check` reopened that existing library
  read-only: 11 tracks, one album/artist, revision5, no scan/listeners/ownership.
- Staged byte-verified production scan-only tool/helper in private RAM. Live
  tool PID27730/start24412815 executable SHA-256 matched the staged artifact:
  `ea1f65199a06d6aa5c787a1811ba495adbfe8970244e54fbd2ad9f51e57a8dc1`.
- Created only the dedicated own USB directory `.standalone-music`.
- Dispatched the original full-root attempt and one owner-authorized replacement
  attempt after diagnosis, both against the entire USB root.

## Cancelled attempt — no successful full-scan claim

The observer cancelled the job after a runtime guard exception. Final native
state was CANCELLED (5), 1,088 visited entries, 11 recognized tracks, zero parser
errors and 14,199 ms elapsed. Exit2. No SQLite snapshot was published; the new
USB directory remained empty. Private working data was discarded. No persistent
track/album/artist/genre counts or completed-scan CPU/RSS are available.
The observer's initial exception handler did not save the failing check, so its
exact cause cannot be established retrospectively.

Read-only diagnosis reproduced intermittent `http.client.IncompleteRead` using
Python HTTPConnection over a Paramiko channel even with no scanner running.
A bounded direct HTTP/1.0 channel read then completed 20/20 idle lifecycle samples,
all STOPPED_VERIFIED/fresh/count0. This supports an observation-path fault but
does not prove which initial guard failed. The observer now records exceptions
and uses the validated bounded read method; no device executable changed.

The cancelled interval changed sda1 diskstats from 6 writes/29 sectors to
11 writes/41 sectors: +5 operations/+12 sectors (6,144 bytes), consistent with
new-directory metadata; block counters do not prove exact file attribution.
No database payload was published. First tool sample RSS was 244 pages (976 KiB),
not a peak or completed-scan usage measurement. A complete full scan, persistent
reopen, unchanged verification and their CPU/RSS/write measurements remain open.

The next read-only diagnosis confirmed the same Base3933/Jive3192/watchdog639/
proxy3282/web3312 start times, healthOK, inactive Now Playing and fresh stopped
TinySC. The active Base was never replaced. Physical My Music still serves the
original 11-track catalogue, not a full USB index. No playback/reboot/remount/
Jive or watchdog restart occurred. Actual UI responsiveness is owner-observed
only; health requests do not prove touchscreen or Radio/Spotify audio operation.

## Explicitly authorized replacement attempt

The owner authorized one new attempt and confirmed menu/My Music navigation
remained smooth. The replacement's guards all passed (`health_failed=false`).
Native traversal reached1,376entries and273recognized tracks, then FAILED(6)
with one metadata error after165,826ms (whole observation169.381s):

`mp3/Dr Dre - The Chronic/Lyrical Gangbang.mp3`: Audio signature unsupported.

A separately byte-verified, fixed-path, WP6a-root-validated O_NOATIME inspector
read only the first65,536bytes of this5,912,576-byte file. That block contains
zero bytes throughout and no MPEG sync. This is not a claim that the entire
file is zero, nor permission to modify/delete it. The parser's bounded signature
rejection is supported by the actual bytes; no MP3 decoding was attempted.

The conservative publication policy deliberately refuses the whole failed scan,
so273is a recognized-work count, not a committed library count. Albums/artists/
genres, final DB size and successful reopen are not measured. The own USB DB
directory is still empty. No unchanged scan was dispatched because no committed
new library exists. The replacement wrote zero USB sectors/operations (11/41
before and after). First tool sample RSS321pages=1,284KiB is not peak usage;
completed-job CPU/high-water counters are absent on the failure path. Source
files remain untouched. Do not bypass the error just to claim acceptance.

Both attempts and job receipts are preserved locally. Native source/build hashes
are unchanged. A final read-only audit verifies baseline RAM SHA and service
start times. Further scans are not authorized automatically. A policy decision
is needed on handling a new non-audio/unsupported file without weakening deletion
safety, followed by explicit authorization for another physical attempt. No real
power-cut/unplug test was performed.

## Final cleanup

All known finished private-RAM staging executables and job receipts were removed
after local collection (`cleanup.json`). No USB content was removed; the dedicated
USB directory remains empty. `hardware-final-audit.json` verifies unchanged original
RAM database SHA-256, unchanged service starts, stopped TinySC and inactive audio.
The owner confirmed smooth menu/My Music navigation. No native playback was run.
