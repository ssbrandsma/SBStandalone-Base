# WP3b protocol and source trace

## Evidence provenance

This is an implementation of the observed original source contracts, not a live firmware traffic capture. Collected firmware reference files are below `.local/fab4-music-research/device/usr/share/jive/` and `usr/squeezecenter/Slim/`. The collected subset lacks SlimMenus. Its reference is the [pinned upstream SlimMenusApplet.lua](https://raw.githubusercontent.com/LMS-Community/squeezeplay/cca1bba4abeb3fef2b7c4a48a4e4da8fc255666d/src/squeezeplay/share/applets/SlimMenus/SlimMenusApplet.lua), SHA-256 `6e2db5b9ab17c14fe2702bda9407f226d3a49f1438241a242b4bd54a3b9e63a8`. It is explicitly not an installed-device capture.

| Source | Contract used |
| --- | --- |
| SlimBrowser/DB.lua:48,106–118,178 | 200-item wire blocks; count/timestamp cache reset; zero-based offsets |
| SlimBrowserApplet.lua:126–133,1818,1967–2020 | itemNoAction styling; action=none stops fallback; item/base go resolution |
| SlimMenusApplet.lua:79,414,462–494,628–635,1253–1265,1310 | local myMusic remap; menu item_loop; menustatus arrays; add/remove; subscribe/reconnect |
| Menu/BrowseLibrary.pm:506–570,604 | stable My Music child IDs, weights and actions |
| Menu/BrowseLibrary.pm:1013,1166,1288,1487,1677 | artists, genres, albums, tracks and music-folder navigation |
| Control/Jive.pm:2337 | removal/addition notification shape |
| Control/XMLBrowser.pm:1138,1614 | inactive leaves; commonParams/itemsParams action representation |
| Control/Queries.pm:1462 | info total songs/albums/artists output keys |

Other inspected references: LocalPlayer, NowPlaying, MediaManager, SqueezeCenter and Control/Commands. These informed scope/compatibility; none was modified. Exact source hashes are in the evidence ledger.

## Commands and responses

Requests retain the existing `/cometd` Bayeux envelope: `/slim/request`, caller `id`, optional `clientId`, and `data.request=[player, command]`, with `data.response` as the reply channel. Reply acknowledgement and data preserve the request ID and requested response channel. Original handshake/connect/legacy channels stay in the existing dispatcher.

```json
{"channel":"/slim/request","id":"2","clientId":"example","data":{"request":["02:00:00:00:00:01",["browselibrary","items",0,200,"mode:artists","menu:1"]],"response":"/example/reply"}}
```

Supported modes are `artists`, `albums`, `tracks`, `genres`, `bmf`. Applicable filters are `artist_id`, `album_id`, `genre_id`, `folder_id`, plus `library_revision`. `menu:1` and bounded `subscribe` are parsed. Offsets are zero-based; quantity 0..200. Unknown/duplicate tags, unsupported combinations and out-of-range integers fail explicitly. `info total songs|albums|artists [?]` returns `_songs`, `_albums` or `_artists`.

Every page includes total `count`, `offset`, `item_loop`, `window.text`, `base.actions`, `playlist_timestamp`, `library_revision`, and `media_generation`. Up to four 50-row WP1 snapshots compose a 200-row wire block, pinned to the first revision. A change between pages returns an error instead of mixing revisions. Past-end and empty results retain truthful counts and empty loops. Sorting and tie-break IDs use the existing WP1 SQL policy, with source/generation scoping and no path-derived external IDs.

Artist rows carry string `artist_id` in commonParams; base go opens albums. Album rows open tracks. Genre rows open filtered artists; filters propagate through navigation. Folder rows supply individual folder go actions so mixed folder/track lists work. Track leaves expose bounded title/artist/album/disc/tracknum/year/duration and string track_id, with `action:"none"` and `style:"itemNoAction"`. There are no Play/Add/fallback actions or fabricated artwork URLs. Names are UTF-8 escaped using existing Now Playing helpers; full metadata stays in SQLite. Row/navigation IDs are strings to avoid Lua double precision loss. Revision/timestamp fields are numeric; enormous >2^53 revisions are not qualified.

## Menus and subscriptions

Four children under `myMusic`: `myMusicArtists` weight10, `myMusicAlbums`20, `myMusicGenres`30, `myMusicMusicFolder`70. SlimMenus supplies its local parent. Existing Alarm Clock and Sleep nodes remain, giving six fixture menu entries when available and two when disabled/offline.

Menustatus notifications preserve `['menustatus', items, 'add'|'remove', player]`; removal contains only the four music IDs. Each live Comet stream holds one menustatus and one current browse subscription; duplicate registration replaces it. Exact unsubscribe cancels only that path/client. Reconnect transfers logical subscriptions, cancels old work and queries fresh data. Visibility changes trigger menu updates; revision changes trigger one current-view refresh, coalesced while scans are active. No per-track notification flood or fabricated percentage.

## Limits and gaps

Only bounded slim-only native batches are intercepted (up to32 commands); supported legacy commands in those batches retain their original dispatcher. Arbitrary mixtures of meta operations and native browse commands in one HTTP batch are not qualified. No search, playlists, genres info-total variant, localized strings, artwork, local playback or public activation route. Hardware UI use has not been verified. [Actual host fixture requests and complete responses](STANDALONE_MUSIC_WP3B_EXAMPLES.json) include menu, all five browse modes, info and offline rejection; these are executable native observations, not hand-written goldens.
