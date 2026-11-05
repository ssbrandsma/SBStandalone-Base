# WP4a.2d Jive and native browse verification

**Current result, 2026-11-05:** owner confirmed that Artists -> Fleetwood Mac ->
Rumours -> 11 tracks is visible after both native corrections. The confirmation
prompt also asked for Albums or Tracks. Genres and Music Folder content pass
real-device protocol checks but were not individually confirmed on the display.
The earlier failures/pending observations below are retained as history.

The original installed SlimMenus/SlimBrowser/DB and SqueezeCenter sources were
captured in .local/wp4a2d/firmware before modification. Stock My Music consumes
native menu/menustatus and browselibrary responses; no replacement UI or custom
Music applet was installed. The running Jive VM was not restarted or hot-reloaded.
New native data can be supplied to these existing framework handlers.

Native roots: Artists, Albums, Tracks, Genres, Music Folder. Tracks is an added
direct node; the prior four nodes remain. Paging retains original Jive 200-row
wire requests assembled through bounded 50-row WP1 pages. String IDs, revision
pinning, menu merging, subscriptions and reconnect behavior remain tested.

Actual example command through /cometd /slim/request:

```json
{"request":["00:04:20:22:92:c1",["browselibrary","items",0,200,"mode:artists","menu:1"]]}
```

Full requests/responses are in .local/wp4a2d/protocol/*.json. They show:

| Browse path | Actual data |
|---|---|
| Artists | 1, Fleetwood Mac |
| Artist -> albums | 1, Rumours |
| Albums -> tracks | 11, numbered 1-11 |
| Tracks directly | 11 distinct string track IDs |
| Genres | 1, rock; filtered artist navigation passed |
| Music Folder | 11 scoped album audio entries |
| Offset 3/count 4 | Correct slice of the same eleven stable IDs |
| Past-end offset 50 | Truthful total 11, empty item_loop |

Track leaves are action=none/style=itemNoAction, without Play/Add/Insert actions.
The controlled negative playlistcontrol load probe returned successful:false,
error music_playback_disabled. No generic play, decoder or ownership request was
issued. Now Playing stayed stopped. The browse-only runtime does not construct
the playback instance, authenticate an ownership bridge or adopt stream resources.

MUSIC_BROWSE lines in scan-and-browse.log prove requests enter the production
WP3b dispatcher. The matching native SQLite schema/revision and eleven identical
track IDs independently establish data origin. No stock TinySC/Perl/LMS provider.

Physical navigation did not pass the first owner check: ?In My Music zie ik alleen switch library?. The categories are not yet visible in the running Jive UI. Protocol success is not visual verification.

Read-only follow-up confirmed the same boot, Base PID 2698, Jive PID 3192, watchdog PID 639 and fresh TinySC STOPPED_VERIFIED/count 0. The captured logs show no new SlimMenus menu subscription/request after the Base reconnection. Installed SlimMenus can retain its loaded-menu state when the same player/server reconnects (notify_playerCurrent early return; notify_serverConnected only refetches when waitingForPlayerMenuStatus). This is a candidate explanation, not a proven fix. The owner was asked to inspect Switch Library and select Standalone Base if offered. No restart, rescan, database recreation or device file change was performed during this diagnosis.

The owner was given this exact physical test: My Music -> Artists -> Fleetwood
Mac -> Rumours -> eleven tracks; return and open Albums, Tracks, Genres and Music
Folder; navigate back several levels and reopen My Music. No Play/Add. Confirm
titles/order and responsiveness. The stock _updateMyMusicTitle may label this
parent Standalone Base (server name), distinct from the Base service-status applet.
There is no new standalone application menu named Standalone Music.

The existing Virtual Touch GET /api/remote/status returned 401 Browser pairing
required. No pairing/protection bypass, new credentials, injected UI events or
frame capture was performed. Therefore protocol tests alone are not represented
as visual confirmation. Native 0.7.0 is active even though the newer Base Lua
applet files from WP4a.2c remain unactivated in the current VM.

Physical empty-library, offline/reinsert, arbitrary Unicode corpus and full
navigation remain unverified unless covered by the owner's observations. Those
contract cases passed host fixtures; they were not simulated with device test
endpoints. Physical playback remains out of scope and disabled.

## Follow-up, 2026-11-05

The owner again reported only Switch Library in My Music. Read-only SSH confirmed
the original boot and Music PID 2698, fresh native arbitration, no TinySC process
and no native restart. The Comet log shows Jive reconnecting with the pre-restart
client ID f4dd3cd7, without replaying its menu subscriptions. The captured installed
Comet.lua explains the distinction: _handshake resets existing subscriptions to
pending and resends them; _reconnect retains its client ID. Base currently accepts
that unknown pre-restart reconnect ID and responds with retry advice. Combined
with SlimMenus' loaded-menu cache, this explains a missing initial menu refresh;
the exact physical recovery remains to be verified.

A source fix should distinguish an expired logical Comet session from an ordinary
replacement event stream and request a handshake for expired sessions, while
preserving valid subscriptions during ordinary reconnects. It has not been
implemented/deployed or claimed qualified by this observation. No database was
recreated and no Jive/watchdog restart was attempted. The requested next owner
observation is the list displayed after tapping Switch Library, selecting
Standalone Base if offered. Native browsing API tests alone remain insufficient.

## Reconnect correction deployed, 2026-11-05

Selecting the already-selected Standalone Base library was insufficient. The
server now rejects /meta/reconnect when no logical event stream remains, returning
successful:false, error 402::Unknown client and advice.reconnect=handshake. A live
replacement stream keeps the existing subscription inheritance. This bounded
change uses stock Comet.lua's existing handshake/resubscribe behavior; no Jive
patch/hot reload or custom menu is involved.

19 relevant host tests passed in 45.93 seconds, including the new expired-client
case, live /meta/reconnect subscription replay, Comet, Now Playing, browse and
partial-write regressions. Host build used working WSL (Docker was unavailable),
BUILD_SBPROXY=OFF. Static Fab4 compilation with -Werror and QEMU self-test/version
passed. No new Baby, sanitizer or full-suite run is claimed for this correction.

The old browsing session was explicitly ended via Base-only SIGTERM with the
Music flag disabled. The existing supervisor started ordinary Base. After no
process held a database FD, the completed old database was copied to the host,
checked read-only (integrity ok; 11 tracks), and renamed on its existing private
RAM volume to library-before-menu-fix.db. It was not deleted or overwritten. Only
then was a new bounded session started for the same selected album. No active
browse session had its database replaced. Both DBs total 240 KiB, with tmpfs
usage 256 KiB out of 8192 KiB. The one-shot retry guard remains in force.

Only /usr/share/jive/applets/StandaloneBase/sbbase-music changed on persistent
storage in this follow-up. Before SHA-256:
013cf9f4bfa6443e5cbdb5658504e2e60120bd4ceb00ca5e75152a0d98c05dd1
After SHA-256:
ad4c0420ef35f5b4ce148e53f52c23052088b2cbe12a11dd448c7033ea5948d9
Mode remains 0755, root:root. New active Music PID 1839/start 17567376, Base
version 0.7.0; the boot UUID and Jive 3192/watchdog 639/proxy 3282/web 3312
PID/start identities remain unchanged. TinySC STOPPED_VERIFIED/fresh, count zero.
USB write counters remain 6 writes/29 sectors. No audio started.

New scan: 15 files, 11 MP3 tracks, zero errors, 4575 ms. Base scan CPU 842 ms,
reaped helper CPU 40 ms, helper reported peak RSS 144 KiB. Native same-worker
integrity passed; DB 122880 bytes/revision 5/DELETE journal. Original-session
performance values above remain historical and are not measurements of the fix.

Hardware logs now show advice is handshake, a new client ID f4db8b0b and actual
stock SlimMenus _menuSink(7), directive add, isCurrentServer:true. The menu-loading
popup is dismissed by the stock handler. Direct protocol verification also
returns all seven menu items and eleven non-playable tracks. This is actual Jive
handler evidence; category visibility and touch navigation still require the
owner's new observation. WP4a.2d remains incomplete until that check passes.

Exact commands, using the already-approved SSH password environment:
```powershell
python .local/wp4a2d/menu-fix/deploy.py
python .local/wp4a2d/menu-fix/verify.py
```
Build command: sh .local/wp4a2d/menu-fix/build.sh under WSL (paths in script).
Relevant test command: ctest --test-dir /tmp/sb-menu-fix --output-on-failure
-R 'comet|nowplaying|music_browse|music_dispatch|base_backpressure|base_partial' -j 1.
Evidence: .local/wp4a2d/menu-fix/{before-files.tar,before.json,previous-library.db,
sbbase-music-before,build.log,active.json,verified.json,live.txt,verify.log}.

Rollback must first restore this follow-up's prior Music binary from the checked
before-files.tar/sbbase-music-before backup while ordinary Base is active, then
use the original WP4a.2d rollback. The original rollback deliberately refuses the
new binary hash as an unknown revision; do not bypass that check. The original
files/backup are preserved and no rollback was executed after successful
activation. Both RAM DBs must be closed before cleanup; no forced unmount.

## Stock SlimBrowser parameter correction, 2026-11-05

The owner confirmed that the first correction made all five categories visible,
but their lists appeared empty. Captured installed SlimBrowserApplet.lua appends
useContextMenu:1 to every parameterized browse action. The strict native parser
rejected that presentation flag, returning count 0/item_loop [] with the explicit
unsupported_or_invalid_browse error. Replaying that exact request shape against
the old host build reproduced the failure before the parser change.

The parser now accepts only useContextMenu:0 or :1 as presentation preferences;
unknown tags and other values still fail. It does not grant Play/Add actions or
construct a playback instance. HTTP fixtures now use the stock parameter on all
normal browse requests, including filtered/paginated paths, and test invalid
values. A test diagnostic indentation mistake was fixed before the successful
reruns; it did not affect production code.

This second correction replaced only the same Music binary, 0755 root:root:
before ad4c0420ef35f5b4ce148e53f52c23052088b2cbe12a11dd448c7033ea5948d9
after  682cff02825f5f9b6e56f9787eea4931cc8d82a77bb7a488dfcbd1bca03ad90c
Active Music PID 3933/start 17600372, version 0.7.0. The prior session was ended
first, its closed database copied and independently checked read-only, then
retained as library-before-context-fix.db. Together with library-before-menu-fix.db
and the current library.db, three 120 KiB databases remain within the dedicated
8 MiB RAM volume (384 KiB filesystem usage). None was overwritten or removed
during active browsing. New scan: 15 files/11 tracks/0 errors, 4561 ms; Base CPU
835 ms, child CPU 20 ms; helper kernel-reported peak RSS 144 KiB. Native same-worker
integrity ok, DB 122880 bytes, DELETE journal/revision 5.

Static Fab4 -Werror build and QEMU self-test/version pass. The final relevant
host test log is .local/wp4a2d/context-fix/host-tests.log. Device requests with
useContextMenu:1 now return Artists=1, Albums=1, Tracks=11, Genres=1, bmf=11;
artist -> album -> 11 tracks also passes. Track leaves remain action none.
Same boot, Jive/watchdog/proxy/web identities, fresh TinySC stopped/count zero
and USB counters 6 writes/29 sectors are retained.

An initial read-only verification GET over an SSH direct-tcpip channel ended with
an incomplete HTTP body. Target-local wget immediately returned healthy Base
with the same PID; repeating the bounded verification passed all queries. No
process restart or database retry was used for that transport observation.

Evidence and exact commands: python .local/wp4a2d/context-fix/deploy.py and
python .local/wp4a2d/context-fix/verify.py, with SB_DEVICE_PASSWORD supplied through
the environment. Build script and before-files.tar/before.json, prior database,
active.json, verified.json, jive-shaped-pages.json and verification logs are in
.local/wp4a2d/context-fix/. Physical filled-list navigation is pending the owner's
new check. Source/API evidence is not substituted for that observation.

For the original full rollback, first stop the new test session through the
existing Base-only fallback and restore original Music SHA 013cf9f4... from
.local/wp4a2d/menu-fix/sbbase-music-before using the checked atomic-copy workflow.
Then original session.py rollback recognizes the expected file inventory.
Do not feed the newer hashes to the original revision guard or delete live DBs.
No full physical rollback was executed after the successful corrections.

Owner final response: "ja" to the filled Artists/album/11-tracks check and
secondary Albums-or-Tracks check. Raw observation is recorded in
.local/wp4a2d/physical-ui.json. This confirms the reported empty-list problem is
resolved on the physical Touch. No exhaustive visual route/timing claim is made.
