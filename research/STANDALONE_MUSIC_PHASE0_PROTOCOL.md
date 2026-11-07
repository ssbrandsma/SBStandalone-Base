# Standalone Music Phase 0 Protocol Compatibility Matrix

Source-derived analysis, not runtime traffic. References resolve in the [evidence index](STANDALONE_MUSIC_PHASE0_EVIDENCE.md). Complete means complete for the stated bounded operation, not full LMS compatibility. Unknown commands returning an empty success are missing, not implemented. Single local player is the MVP scope.

## 1. SlimProto wire coverage

| Direction/message | Required semantics | Existing Base | Reference | Proposed phase |
|---|---|---|---|---|
| Player -> HELO | MAC/device identity, reconnect/capabilities/codecs | Partial: MAC and connected flag only | B01 handle_slim:1567; L08 _hello_handler:974 | MVP: parse bounded capabilities, truthful model |
| Base -> serv | Server handoff | Complete for validated literal IPv4 switch | B01 switch_server:960 | Reuse; ownership reconnect tests |
| Base -> strm-t; player -> STAT/STMt | Timestamp/heartbeat and elapsed feedback | Partial: heartbeat emitted, STAT not interpreted | B01 keepalive:1550; F01 _strm:852; L08 _stat_handler:681 | MVP |
| Base -> strm-s | Codec, thresholds, stream GET/header, port/address, flags | Missing local start | F01 _strm:852 | MVP MP3 mode m, FLAC f; validate actual HELO |
| Base -> strm-p/u/q/f | Pause/unpause/quit/flush with correct state/epoch | Missing; current controls are external-owner messages | B01 request_responses:1043; F01 _strm:852 | MVP |
| Base -> audg | Gain and mute consistent with local volume curve | Missing native transmitter | F01 _audg:1145; S01 setRemoteVolume:48 | MVP retain firmware local volume, native gain if required |
| Base -> aude/cont/codc | Output enable, continuation/codec configuration where firmware requires | Missing semantic controller | F01 subscriptions and stream handling | Implement only required negotiated subset; optional codecs later |
| Player -> STAT STMc/STMe/STMh | Connection/header/start progress | Missing | L08 _stat_handler:681; F01 | MVP status state machine |
| Player -> STAT STMl/STMr/STMp | Full/resume/pause confirmation, buffer recovery | Missing | L08:729-731; F01 | MVP, no timed sync claim |
| Player -> STAT STMs | Actual track-start event | Missing | L08:732; F01:381 | MVP: commit audible track/position once |
| Player -> STAT STMd | Decoder-ready for next stream | Missing | L08:731; F01:320 | MVP avoid confusing with audible end; later prebuffer |
| Player -> STAT STMu/STMf/STMn | Output drain/close/unsupported, contextual errors | Missing | L08:724-733; F01 | MVP deterministic terminal/error handling |
| Player -> DSCO | Input disconnect reason; normal EOF vs error | Missing interpretation | L08 _disco_handler:581 | MVP; not automatic next on every socket close |
| Player -> RESP | HTTP status/header validation | Missing interpretation | L08 _http_response_handler:555 | MVP detect file HTTP failures |
| Player -> META | ICY payloads | Missing Base interpretation; Radio handles it elsewhere | L08 _http_metadata_handler:922; R01:321 | Preserve external app path; local queue not dependent |
| Base -> transition/prebuffer flags | Gapless/crossfade and buffering next track | Missing | F01 _strm and native decoder reference | Phase 2, codec-pair tests |

Framing exists in `native/sbbase/src/protocol.c:103` and server-frame construction; this does not implement message semantics. Map events by owner generation and stream epoch, deduplicate repeated terminal events and handle player disconnect/reconnect without advancing twice. Seek is a separate time-to-byte/frame plus decoder initialization contract, not a new use of strm-t or HTTP Range alone.

## 2. HTTP and command/subscription inventory

| Surface | Base coverage | Exact implementation/evidence | Required extension |
|---|---|---|---|
| POST /cometd, Bayeux handshake/connect/reconnect/disconnect | Partial LMS/Bayeux subset; working persistent request/event sockets | B01 handle_http:1265, stream_start:251 | Reuse framing/lifecycle; typed bounded music dispatch |
| /slim/request channel | Partial; reply-channel envelopes | B01 request_responses:1043 | Add validated library/control commands |
| /slim/subscribe and unsubscribe | Partial; selected status/firmware handling, not arbitrary LMS pubsub | B01 register_playerstatus:404, unregister_playerstatus:542 | Music/status subscriptions with cancellation/revision |
| serverstatus | Partial single player; hard-coded Baby model | B01 request_responses | Truthful target/capabilities; no multi-player promise |
| firmwareupgrade/jiveapplets/date/connect | Complete for existing bounded product flows | B01 firmware/catalog/switch handlers | Preserve unchanged behavior |
| menu/menustatus | Partial alarm menus, no My Music | B01 request_responses; alarms module | Merge My Music nodes, preserve alarms; add/remove notifications |
| status | Partial current external-owner snapshot | B02 np_lms_query_json:1873, np_playerstatus_json:607 | Queue length/index/items, real mode/time/repeat/shuffle/seek capabilities |
| songinfo / contextmenu / trackinfo items | Partial current track only, metadata/text rows | B02 np_lms_query_json | Resolve indexed track IDs and contextual actions |
| play/pause/stop | Partial external-owner command enqueue | B01:1177 onwards; B02 np_queue_command:953 | Route local owner to native controller, others retain polling |
| playlist index | Partial next/previous heuristic, not general absolute index | B01:1201 | Parse +/- relative and absolute index correctly |
| playlistcontrol load/add/insert/delete | Missing | No matching branch in B01; L03:593 | Native selection -> queue transaction |
| playlist clear/move/delete/repeat/shuffle | Missing local semantics | L03 dispatch:556-588 | Queue edits MVP basics; advanced modes Phase 2 |
| time seek / mixer volume | Missing local native command semantics | B01 command dispatch; F01 gain/stream | Honest flags; exact seek Phase 2 |
| browselibrary items modes artists/albums/tracks/genres/bmf/search/playlists | Missing | L01 handlers vs B01 dispatch | Native query and Jive menu serializers |
| artists/albums/genres/titles/musicfolder/search/playlists raw queries | Missing | L03 init:466, query dispatch 484-629 | Shared DB query core, serializers as required by clients |
| info total / rescan / scan status | Missing music behavior | L03 + L04; B01 | Bounded Base scan API; rescan compatibility only when UI needs it |
| /jsonrpc.js | Missing HTTP route | B01 handle_http rejects non-Comet routes | Adapter to same typed dispatcher, no duplicate backend |
| Text CLI port 9090 | Missing | B01 main listeners | Not required for stock Jive MVP; explicit optional scope |
| /api/nowplaying claim/update/heartbeat/release/commands/ack | Complete for existing lease/snapshot/control-queue role | B02 np_api:1010, lease120s/limit32 created B01:1655 | Preserve API; add acknowledged physical takeover internally |
| /music/stream/<token> (proposed) | Missing indexed music endpoint | B10 existing file-serving primitive | Extend existing web server; HEAD/GET/206/416 and cancellation |
| /music/<id>/cover... LMS-style art | Missing local album route | L01 builds music/<id>/cover; B07/B08 existing image path | Compatible URL rewrite or alias to same image worker |
| /hls-meta/<id>, /art/<revision> on proxy | Complete bounded transient HLS role | B06 hls_metadata_http:220 | Reuse utilities, not session registry as persistent library |
| /api/image/render, proxy /artwork/... | Complete for current remote artwork role, partial for local covers | B04/B07 | Trusted local source, fingerprinted IDs/cache roots |

Existing subscriptions publish playerstatus/server changes and firmware state. Required music notifications: menu nodes when media/library availability changes; queue revision/index/mode/time/metadata/artwork on playerstatus; library revision on successful scan publication. Reuse firmware Jive expectations first; a new private scan-progress channel is a proposed extension and must not be represented as an existing LMS contract. Coalesce progress, never flood one message per scanned file.

## 3. Fourteen Jive operations

For browse rows below F02 SlimBrowser `_actionHandler:1778`, `_server:userRequest:817/852` and F03 `userRequest` are the common outgoing path. SlimMenus receives server menu nodes; NowPlaying uses Player/LocalPlayer status and actions. LMS uses L03 Request dispatch, L01 BrowseLibrary and `Slim/Control/XMLBrowser.pm` to turn internal actions into wire menus. This XMLBrowser source is additionally indexed as L11. All paging uses zero-based offset/quantity, count of filtered total, stable sort and bounded item_loop. IDs in examples are illustrative.

| Operation | Outgoing command / LMS handler | Expected response/menu semantics | State and notifications | Base |
|---|---|---|---|---|
| 1 My Music | menu/menustatus -> L02 myMusicMenu:2337 -> L01 getJiveMenu:604 | Nodes id/node/text/weight/actions.go; My Music parent | Server attachment, menu add/remove updates | Partial alarm menu only |
| 2 Artists | browselibrary items 0 50 mode:artists menu:1 -> L01 _artists:1013 -> artists query | Artist rows, common artist_id, go albums | Library revision; page invalidation after scan | Missing |
| 3 Albums | mode:albums [artist_id] -> _albums:1288 -> albums query | Album rows, cover/name2, common album_id, go tracks | Stable filtered total/sort | Missing |
| 4 Album tracks | mode:tracks album_id:N -> _tracks:1487 -> titles | Ordered disc/track rows, track_id, play/add actions | Track metadata/duration/cover, page revision | Missing |
| 5 Genres | mode:genres -> _genres:1166; chosen genre -> artists/albums filters | Genre rows propagate genre_id | Library revision, no audio claim | Missing |
| 6 Music Folder | mode:bmf -> _bmf -> musicfolder query | Directory/file items, folder_id, hierarchical go/play/add | Online medium generation, removed folder errors | Missing |
| 7 Search | mode:search -> _search:858/_globalSearchMenu:870/searchItems:884; search term input | Input action substitutes text, categorized results and totals | Cancel/stale result generation; no raw SQL from clients | Missing |
| 8 Playlists | mode:playlists -> _playlists:1739; playlist tracks | Saved playlist rows, playlist_id, ordered members | Missing files retained/unavailable, no automatic play | Missing |
| 9 Play album | playlistcontrol cmd:load album_id:N -> Commands::playlistcontrolCommand | Acknowledged queue replace; play action nextWindow nowPlaying | Owner yield, queue revision, buffering then actual start | Missing |
| 10 Play track | playlistcontrol cmd:load track_id:N, or playlist-load selection with chosen index | Queue selection and track start, not a text-row response | Same plus current entry ID | Missing |
| 11 Add to queue | playlistcontrol cmd:add track_id/album_id; insert for play-next | Ack; add action preserves browse window | Queue revision/count, owner policy; no unwanted start | Missing |
| 12 Now Playing | status 0 50 tags:... subscribe:N, songinfo/contextmenu | mode/time/duration, playlist_loop/index/count and metadata/cover; menu-specific form as needed | /slim/playerstatus updates; confirmed position | Partial one current external track |
| 13 Next/previous | playlist index +1/-1 -> playlistJumpCommand | Ack + new index and subsequent playback confirmation | Epoch increment, stop/prebuffer policy; stale event rejection | Partial external command queue |
| 14 Repeat/shuffle | playlist repeat/shuffle 0/1/2 or ? -> respective Commands handlers | Query returns mode, change updates state | Deterministic modes, queue revision/status; preserve original order | Missing |

Base should not advertise incomplete menus/actions: MVP can show Artists/Albums/Genres/Folders while Search/Playlists remain absent until implemented. Preserve stock visual styles and interaction semantics, not every unsupported historical submenu. Raw CLI query loops and Jive menu item_loop are separate serializers over one query core.

## 4. Representative source-derived envelopes

These are reduced, reconstructed fixtures with invented IDs/text and omitted unrelated fields, **not captured firmware traffic or byte-identical LMS output**. Shape is derived from L01 getJiveMenu/_albums/_tracks, L11 XMLBrowser action serialization and F02 action resolution. Additional optional fields/notification envelopes must be pinned by WP0 fixtures.

My Music node supplied through the menu response/notification:

```json
{"id":"myMusicAlbums","node":"myMusic","text":"Albums","weight":20,"actions":{"go":{"cmd":["browselibrary","items"],"params":{"menu":1,"mode":"albums"}}}}
```

Album page request on the existing CometD HTTP connection:

```json
[{"channel":"/slim/request","clientId":"example-client","id":"7","data":{"request":["00:11:22:33:44:55",["browselibrary","items",0,50,"mode:albums","menu:1"]],"response":"/example/albums"}}]
```

Reduced reply (ack and data are distinct messages, and may be delivered through the Comet stream):

```json
[{"channel":"/slim/request","id":"7","successful":true},{"channel":"/example/albums","data":{"count":1,"offset":0,"window":{"text":"Albums"},"base":{"actions":{"go":{"cmd":["browselibrary","items"],"params":{"mode":"tracks","menu":1},"itemsParams":"commonParams"},"play":{"cmd":["playlistcontrol"],"params":{"cmd":"load"},"itemsParams":"commonParams","nextWindow":"nowPlaying"},"add":{"cmd":["playlistcontrol"],"params":{"cmd":"add"},"itemsParams":"commonParams"}}},"item_loop":[{"id":"12","text":"Example Album","commonParams":{"album_id":"12"},"icon":"music/34/cover"}]}}]
```

Selecting the album propagates its parameter, yielding a command like:

```json
["browselibrary","items",0,50,"mode:tracks","album_id:12","menu:1"]
```

A representative track item can carry explicit actions rather than inherited base actions:

```json
{"text":"Example Track","id":"34","actions":{"go":{"cmd":["playlistcontrol"],"params":{"cmd":"load","track_id":"34"},"nextWindow":"nowPlaying"},"add":{"cmd":["playlistcontrol"],"params":{"cmd":"add","track_id":"34"}}}}
```

Other command arrays from the source dispatch/menu vocabulary:

```json
[
 ["browselibrary","items",0,50,"mode:artists","menu:1"],
 ["browselibrary","items",0,50,"mode:genres","menu:1"],
 ["browselibrary","items",0,50,"mode:bmf","folder_id:8","menu:1"],
 ["browselibrary","items",0,50,"mode:search","search:example","menu:1"],
 ["browselibrary","items",0,50,"mode:playlists","menu:1"],
 ["playlistcontrol","cmd:load","album_id:12"],
 ["playlistcontrol","cmd:add","track_id:34"],
 ["playlist","index","+1"],
 ["playlist","repeat","1"],
 ["playlist","shuffle","0"],
 ["status",0,50,"tags:adKl","subscribe:1"]
]
```

Search has multiple original node/input/category routes; this array illustrates the shared vocabulary and is not a claim that every search screen uses that exact route. A JSON-RPC envelope over the future existing-server route should dispatch the same arrays:

```json
{"id":1,"method":"slim.request","params":["00:11:22:33:44:55",["status",0,50]]}
```

Return a proper JSON-RPC result/error, not a CometD ack. Library query examples (`albums_loop`, `artists_loop`, `titles_loop`) and menu results (`item_loop`) must remain separate modes. For status, preserve actual numeric/boolean/string compatibility expected by installed Jive, including mode, time, duration, playlist_cur_index, playlist_tracks, playlist_loop and playlist repeat/shuffle fields; the existing one-track serializer is only a starting point. Do not manufacture captured-looking status timing or notification channels as evidence.

## 5. Playback contract and acceptance boundary

MVP source-to-output sequence: Jive playlistcontrol -> native queue/owner transaction -> codec-specific strm-s -> GET authorized existing-webserver resource -> unchanged encoded MP3/FLAC bytes -> SqueezePlay decoder -> shared PCM -> ALSA. STAT/RESP/DSCO drive confirmed state and terminal decisions. HTTP 200 alone is not playing; transfer EOF alone is not finished.

Byte Range must handle HEAD, single ranges, suffix/open-ended ranges, Content-Length/Content-Range and 416, plus 64-bit offsets and >2 GiB media files. Time seek must additionally find valid MP3 frames/Xing/VBRI or FLAC seek points and retain required initialization data. Reject unsupported seeks without corrupting state; initially advertise can_seek false. Gapless is Phase 2 because next-input readiness, current audible track and metadata transition are distinct. Queue persistence should use coarse/event checkpoints rather than writes every elapsed tick; resume after interruption remains paused until explicit play.

Protocol validation in later work: fragmented/coalesced frames, malformed/oversized JSON, command names containing substrings, invalid selectors, batched requests, stale epochs, slow clients, reconnect/replay, unsubscribe, queue edits during scan, duplicate EOF/STAT and unsupported codecs. Preserve alarm/installer/date/server-switch and all external-owner REST fixtures after every package.

The reduced album fixture uses commonParams because L11 _makeAction:1614 maps L01 commonVariables to that item parameter object. Generic XMLBrowser feeds can instead use itemsParams=params; these two contracts must not be accidentally conflated. Radio loopback handoff and Mongoose 32-bit/file-I/O limitations are described in the report.
