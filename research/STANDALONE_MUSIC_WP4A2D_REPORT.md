# WP4a.2d report

**Current outcome, 2026-11-05:** native My Music is visible and the owner has
confirmed filled Artists -> Fleetwood Mac -> Rumours -> 11 tracks on the Touch.
The reconnect and stock browse-parameter corrections are deployed. All five
categories return expected contents through the real-device protocol; the
confirmation prompt also included Albums or Tracks. Physical Genres/Folder
contents and exhaustive back/reopen responsiveness were not individually
reported. Audio remains disabled. The original-session results and investigation
chronology below are historical; see the dated Jive follow-ups for current state.

2026-11-04. **Native scanning/browsing is installed, activated and verified by
protocol on the real Fab4. Physical UI verification is separate.**

Owner correction overrides the attachment: use **192.168.1.244**, not .245.
Only .244 was accessed in this package. Board Logitech Fab4, hostname
SqueezeboxTouch, firmware 7.7.3 r16676, kernel 2.6.26.8-rt16-332-g5849bfa.

The existing Base applet supervisor replaced only its native Base child. Jive,
watchdog, proxy and webserver retained their original PID/start identities. No
second active Base, Jive restart/reboot, watchdog handoff or experimental restart
coordinator was used. The old WP4a.2b Jive restart gate is not fixed or bypassed;
it is unnecessary for this native-child-only activation path.

Base 0.7.0 first ran without Music and verified TinySC STOPPED_VERIFIED/fresh under
its exclusive lease and service guard. Only then was the disposable RAM volume
prepared and the production runtime explicitly started with --music-browse-only.
No physical owner bridge, playback instance, stream resource, native music
webserver or session launcher was activated. Native product defaults remain OFF.

The first browse process refused the tmpfs root's default 1777 permissions before
creating a DB or scanning. Its one-shot wrapper fell back to ordinary Base 0.7.0.
After verifying the empty volume, unchanged mount and healthy arbitration, the
owned RAM directory was corrected to 0700 and the pre-scan attempt was explicitly
rearmed. This was not a failed scan retry: there was no first DB or scan. The one
actual scan visited 15 files, indexed 11 MP3 tracks with 11 distinct IDs, zero
errors, in 8014 ms. Integrity passed in the native WP1 worker and independently
on a read-only host copy. Schema 3 database: 122880 bytes; DELETE journal.

Native WP3b responses provide Artists, Albums, Tracks, Genres and Music Folder.
Fleetwood Mac -> Rumours -> 11 tracks; genre rock; correct album track order,
stable string IDs and offset/past-end behavior were verified. All track leaves
are action=none/itemNoAction. A negative playlistcontrol request returns false
with music_playback_disabled. No play/decoder/ownership request was sent.

The first physical check failed: the owner sees only Switch Library in My Music. Native protocol browsing passes, but Jive category visibility and navigation remain unresolved. See the Jive report for the reconnect investigation.

The RAM library remains active for the requested browsing test; it was not
deleted/recreated during browsing. It is not a permanent USB/flash library.
After reboot the RAM session marker disappears and the wrapper starts ordinary
Base with arbitration, without an automatic scan. An unexpected Base exit also
does not automatically retry Music or rebuild the existing DB.

Observed Base RSS 2468 KiB, Jive RSS 33236 KiB, helper peak 144 KiB as reported by
the target kernel. Instrumented HTTP median 246.06 ms/max 585.89 ms includes SSH
channel setup. USB counters stayed six writes/29 sectors. Existing audited
Radio/Spotify settings, metadata, configuration, proxy/web binaries and watchdog
files were unchanged. Qobuz was absent and not installed. No audio regression is
claimed. No factory partition, bootloader or USB music file was modified.

Local final suite: 104/104, plus nine copy/rollback tests and three exact-session
file tests. Static Fab4 builds and ARM self-tests passed. A prior test run's two
failures and their resolution/limits are recorded in TEST_RESULTS.

Remaining: physical UI observation if not yet supplied; offline/reinsert and
large real library tests are not hardware qualified. Playback still requires
separate real producer/decoder ownership and resource-serving qualification.
Do not enable audio or restart qualification automatically. Stop at WP4a.2d.

- [Deployment and hashes](STANDALONE_MUSIC_WP4A2D_DEPLOYMENT.md)
- [Storage/database](STANDALONE_MUSIC_WP4A2D_STORAGE.md)
- [Jive/protocol](STANDALONE_MUSIC_WP4A2D_JIVE.md)
- [Tests](STANDALONE_MUSIC_WP4A2D_TEST_RESULTS.md)
- [Hardware performance](STANDALONE_MUSIC_WP4A2D_PERFORMANCE.md)
- [Rollback](STANDALONE_MUSIC_WP4A2D_ROLLBACK.md)

Follow-up 2026-11-05: deployed a native expired-Comet-client reconnect correction.
Stock Jive now re-handshakes and its _menuSink processes seven menu items. The
previous completed DB was preserved after ending the old session; a new bounded
scan indexed 11 tracks in 4575 ms with integrity ok. Jive/watchdog were unchanged.
19 relevant host tests and static Fab4/QEMU self-tests passed. Physical screen
confirmation is still pending; see the dated follow-up in the Jive report.

Latest state: the owner saw the categories after the reconnect correction, but
the lists were empty. A second native fix accepts stock SlimBrowser's boolean
useContextMenu flag. It is deployed as PID 3933, SHA 682cff02825f5f9b6e56f9787eea4931cc8d82a77bb7a488dfcbd1bca03ad90c.
Real-device requests matching Jive now return all expected contents and the
artist/album/track path. New scan 4561 ms/11 tracks/zero errors/integrity ok.
Filled-list touchscreen confirmation remains pending. No audio was enabled.
