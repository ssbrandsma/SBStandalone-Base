# WP4a.2d Jive and native browse verification

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

Physical navigation confirmation is pending; protocol success is not visual verification.

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
