# Standalone Music Phase 0 Detailed Gap Matrix

Read alongside the [report](STANDALONE_MUSIC_PHASE0_REPORT.md), [protocol matrix](STANDALONE_MUSIC_PHASE0_PROTOCOL.md) and [evidence index](STANDALONE_MUSIC_PHASE0_EVIDENCE.md). Each evidence ID resolves to exact path/symbol/line/hash. Complete is scoped to existing behavior, not the entire LMS feature. No implementation has occurred.

| Capability | Existing Base implementation | TinySC reference | Gap | Proposed approach | Complexity | Priority |
|---|---|---|---|---|---|---|
| Discovery/server attachment | Complete for existing local product (B01/B15, protocol.c) | L08 and firmware discovery | Music model/capabilities must be truthful | Extend existing component | Low | MVP |
| SlimProto framing | Complete bounded framing (protocol.c:103) | L08 client_readable | No semantic controller follows from framing | Reuse unchanged | Low | MVP |
| HELO capabilities | Partial MAC registration (B01 handle_slim) | L08 _hello_handler:974 | Codec/model/reconnect negotiation | Extend existing component | Medium | MVP |
| strm-s local start | Missing (B01 only keepalive/serv) | F01 _strm:852 | Codec, GET, address/port/threshold fields | Extend existing component | High | MVP |
| Pause/resume/stop | Partial external command forwarding (B01/B02) | F01 strm p/u/q/f | Native confirmed control path for local owner | Extend existing component | Medium | MVP |
| STAT/RESP/DSCO handling | Missing semantic handling (B01) | L08 _stat/_http_response/_disco | State, errors, elapsed, drain, duplicate events | Extend existing component | High | MVP |
| Local gain/volume | Partial UI/app-local behavior (R01/S01/Q01); Base audg missing | F01 _audg:1145 | Preserve local curve and avoid stale owner gain | Extend existing component | Medium | MVP |
| Time seek | Missing local file seeking (B01/B02) | L07 _timeToOffset/getSeekData | MP3/FLAC time/frame offsets and initialization | Extend existing component | High | Phase 2 |
| Gapless/prebuffer | Missing (B01) | F01 transition flags, L08 STMd/STMs | Separate input-ready/audible state and metadata boundary | Further investigation required | High | Phase 2 |
| HTTP network audio | Complete existing proxy role (B04/HLS) | LMS networking | Not equivalent to file server | Reuse unchanged | Low | MVP |
| Authorized local file route | Missing indexed-audio route; generic file serve Partial (B10) | L07 File open, HTTP stream | Resource authorization, old-kernel I/O and cancellation | Extend existing component | Medium | MVP |
| Byte Range/HEAD | Partial proxy forwarding and Mongoose primitive (B04/B10) | L07 offsets | Indexed file policy, 64-bit sizes, removal tests | Extend existing component | Medium | MVP |
| Metadata value model | Complete current bounded snapshot role (B03) | LMS track/schema fields L10 | Library richer/long/multi-valued data separate | Extend existing component | Low | MVP |
| ICY StreamTitle | Complete Radio role (R01:321), not general Base indexer | L08 META | Preserve existing live metadata | Reuse unchanged | Low | MVP |
| ID3 parsing | Partial streaming HLS v2.3/v2.4 text/APIC (B05) | Audio::Scan/scanner L04 | File tags, album-artist/disc/track, properties, v1/v2.2, duration | Extend existing component | Medium | MVP |
| FLAC/Ogg local metadata | Missing Base file scanner (B14/B15) | L04 and metadata/native firmware inventory | Native bounded parser choice/Unicode/properties | New native component | Medium | MVP |
| AAC/ALAC metadata/container | Unverified for new local path; no scanner (B14/B15) | Firmware private decoder presence not playback proof | Metadata plus capability/container acceptance | Further investigation required | High | Optional |
| SQLite library | Missing dependency/schema/query implementation (B14/B15) | L10 schema/migrations, installed SQL | Small native portable schema/indexes/versioning | New native component | Medium | MVP |
| Incremental discovery | Missing (B01/B14) | L04 find/rescan/new/changed/deleted | Fingerprints/epochs/cancellation and safe sweep | New native component | High | MVP |
| Watch/schedule/throttle | Missing shared music scheduler (B01); bounded proxy/image work exists | L05 async traversal, L06 AutoRescan | Priority, inotify overflow, cooperative budgets | New native component | Medium | MVP |
| Symlink/rename identity | Missing portable music policy (B13/B14) | L05 dependency behavior partly unverified | Safe root confinement, stable IDs, loop tests | New native component | Medium | MVP |
| Corrupt/offline DB recovery | Missing music DB (B14) | L09/L10 separate DB/cache behavior | No destructive sweep/rebuild on removal; preserve user data | New native component | High | MVP |
| Bayeux transport | Partial LMS subset; reusable sockets/replay (B01) | F03 CometD, LMS network modules | Additional typed handlers/subscriptions | Extend existing component | Medium | MVP |
| JSON-RPC endpoint | Missing /jsonrpc.js (B01 handle_http) | TinySC JSONRPC initialized despite noweb | Envelope adapter to same dispatcher/server | Extend existing component | Medium | MVP |
| CLI text protocol | Missing Base 9090 listener (B01 main) | LMS CLI plugin | Not needed for stock Jive | Further investigation required | Low | Optional |
| My Music nodes | Missing; alarm menu Partial (B01) | L02 myMusicMenu/L01 getJiveMenu | Node IDs/actions/notification merge | Extend existing component | Medium | MVP |
| Stock browse widgets | Complete on firmware, not Base backend (F02/F03) | Same installed Jive | Protocol compatibility, no replacement UI | Reuse existing Jive Lua | Low | MVP |
| Artists/albums/tracks/genres | Missing query/menu handlers (B01/B02) | L01 _artists/_albums/_tracks/_genres | Filters/joins/order/count/pagination/actions | Extend existing component | High | MVP |
| Music Folder | Missing (B01/B02) | L01 _bmf:1677 | Directory IDs and safe hierarchical views | Extend existing component | Medium | MVP |
| Search/sorting | Missing local search (B01/B02) | L01 _search/searchItems/_albums | Unicode normalization, indexed query, input actions | Extend existing component | Medium | Phase 2 |
| Multi-track queue | Missing; command queue Partial (B02:953) | L03 playlistcontrol; Commands.pm:1746 | Stable entries/order/index/atomic edits/revisions | New native component | High | MVP |
| Repeat/shuffle | Missing local semantics (B01) | L03 repeat/shuffle dispatch | Modes, reproducible order and persistence | Extend existing component | Medium | Phase 2 |
| Saved M3U playlists | Missing (B14/B01) | L04 scanPlaylistFileHandle; L01 _playlists | Ordered refs/unresolved entries, path conversion/export | Extend existing component | Medium | Phase 2 |
| Queue/resume persistence | Missing; alarm/settings persistence reusable (B11/B12) | LMS current playlist/persistent state | Coarse checkpoints, stale media, no boot autoplay | Extend existing component | Medium | Phase 2 |
| Now Playing model/status | Partial single external track (B02/B03) | F01/F03 and LMS status | Real queue status/time and native transport events | Extend existing component | Medium | MVP |
| Owner lease/generation | Complete metadata-session role (B02 np_api) | LMS per-player controller; existing app hooks R01/S01/Q01 | Physical yield/ack/reconnect separate from claim | Extend existing component | High | MVP |
| App takeover compatibility | Partial Radio external-library yield (loopback excluded); Qobuz disconnect; Spotify false controls (R01/S03/Q01) | F01 normal playback ownership | Acknowledged stop/hook restore and stale-event policy | Small Lua compatibility adapter | High | MVP |
| Artwork URL routing | Complete current remote routing (B08/B09) | F03 artwork fetch; LMS cover URLs L01 | Local cover alias/opaque ID; prevent remote-policy weakening | Extend existing component | Low | MVP |
| SVG/render/cache workers | Complete current remote role (B07) | TinySC gdresized, artwork cache | Trusted local sources/cache root/peak-RAM scheduling | Extend existing component | Medium | MVP |
| Embedded/sidecar album art | Partial HLS small APIC (B05/B06); local lifecycle Missing | L04 scanner and gdresized | Select/extract by bounded source offset; fingerprint invalidation | Extend existing component | Medium | MVP |
| OS USB/SD mounting | Unverified as Base API; firmware implementation present (F06) | Existing udev mount script | Reconcile authoritative mounts; no duplicate automounter | Reuse unchanged | Low | MVP |
| Storage menus/eject visuals | Partial Base board layer; firmware UI present (B13/F04) | MediaManager registration/eject | Replace TinySC actions, scan progress and safe acknowledgements | Small Lua compatibility adapter | Medium | MVP |
| TinySC permanently bypassed | Partial initial conflict guard only (B13/F05) | SqueezeCenter startup/insertion/start actions | All enable paths must avoid old backend | Small Lua compatibility adapter | High | MVP |
| Medium identity/removal | Missing Base music abstraction (B13/B14) | F04/F05/L09 one-drive policy | Stable medium IDs, generations, cancellation, no empty-dir writes | New native component | High | MVP |
| Baby portability | Partial existing dual-target builds (B15/B13) | Fab4 storage UX is board-specific | Verified Baby RAM/storage availability, core no board assumptions | Extend existing component | Medium | MVP |
| Performance acceptance | Unverified music workloads; previous passive profile only | Existing profile no TinySC scan/play | Controlled CPU/RAM/latency/I/O/underrun experiments | Further investigation required | High | MVP |

MVP rows can be delivered in successive packages; they do not authorize implementation in this phase. Search/playlists/seek advanced modes must not be advertised until complete. Basic embedded/sidecar art may start as an original-image/placeholder path and use existing rendering infrastructure before later full search/cover polish. Priority labels describe release acceptance; portability/performance checks start at WP0 rather than being postponed to the final package.
