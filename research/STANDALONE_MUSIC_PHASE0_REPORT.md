# Standalone Music Phase 0 Report

Status: architecture proposal only, 2026-11-01. No production code, build configuration, device, database, service, playback or scan was changed. No new device session was needed. TinySC must remain disabled; no runtime dependency on LMS, Perl, its scanner or its database is proposed. This decision supersedes the earlier report's suggestion to evaluate retaining TinySC.

Companion documents: [gap matrix](STANDALONE_MUSIC_PHASE0_GAPS.md), [protocol and Jive contracts](STANDALONE_MUSIC_PHASE0_PROTOCOL.md), [diagrams](STANDALONE_MUSIC_PHASE0_DIAGRAMS.md), [roadmap](STANDALONE_MUSIC_PHASE0_ROADMAP.md), [evidence index](STANDALONE_MUSIC_PHASE0_EVIDENCE.md), [machine-readable source ledger](STANDALONE_MUSIC_PHASE0_SOURCES.json).

## 1. Recommendation and decisive findings

Extend the existing `sbbase` music-server backend with a portable SQLite library, bounded native scanner, library/menu queries, an actual track queue and a SlimProto transport controller. Extend the existing `sbwebserver` file-serving and image worker infrastructure for opaque local audio/artwork resources. Keep Jive SlimBrowser, SlimMenus and NowPlaying, SqueezePlay decode and ALSA. Reuse the existing operating-system automounter; provide a small Fab4 Lua storage/ownership adapter. Baby uses the same core with storage unavailable until a separately validated adapter exists.

The five largest gaps are: (1) persistent library/scanning/tag extraction, (2) complete My Music browse/action contracts, (3) native SlimProto playback/status/transition state machine, (4) a real multi-track queue with ownership handoff, and (5) removable-storage identity/removal/recovery. Local-file HTTP streaming is also missing as a product feature, but an existing HTTP server and file-serving primitive can carry it. These gaps are source-confirmed, not inferred from module names (B01-B15, R01-S03, Q01-Q02 in the evidence index).

Base already has valuable functionality. It does **not** currently have a general local music server hidden behind its Now Playing API. In particular, its command queue is a queue of control messages for external apps, not a playlist. Parsing a SlimProto frame is not interpreting `STAT`; sending the `strm-t` heartbeat is not starting a track. Spotify's audio server is a sibling Rust bridge, not `sbbase`. Qobuz uses `sbproxy` as an upstream HTTP client/proxy and directly starts the firmware decoder from Lua. These boundaries determine the implementation scope.

## 2. Evidence and limits

Current Base checkout is preferred over old documentation. Exact paths, symbols, line numbers and hashes are in the companion evidence index; IDs below refer there. Installed Touch firmware copies are under ignored `.local/fab4-music-research/device/`; their provenance is in [the earlier evidence index](../docs/research/FAB4_LOCAL_MUSIC_EVIDENCE.md). The earlier [architecture report](../docs/FAB4_LOCAL_MUSIC_ARCHITECTURE.md), [source ledger](../docs/research/FAB4_LOCAL_MUSIC_SOURCES.json) and [profile](../docs/research/FAB4_LOCAL_MUSIC_PROFILE.json) remain unchanged.

The earlier Touch inspection found ARM1136/ARMv6, 125848 KiB visible RAM, no swap, TinySC off and no USB/SD medium mounted. It measured an encountered audio-active state, not a controlled idle baseline. There was no live `library.db` to inspect. Runtime MP3/FLAC queues, seek, simultaneous scan/play and insertion/ejection behavior remain unmeasured. This phase only examines existing source/artifacts and proposes tests.

Baby RAM is **unverified from device-specific evidence available to this analysis**. A generic Freescale patch has `SDRAM_MEM_SIZE SZ_64M`, but that is not sufficient proof of the shipping Baby memory configuration. `applet/Platform.lua:14` reads actual MemTotal; collect that and board identity in a future approved baseline session. Use conservative provisional budgets below; do not claim a measured 64 MB Baby. CPU classes come from the task/build targets; CPU frequency and available memory under the supported Baby image still require validation.

## 3. Existing Base inventory

### 3.1 Audio: client, server and proxy

| Component | Confirmed behavior | Consequence for local files |
|---|---|---|
| `sbbase` `handle_http`, B01 (`main.c:1265`, routing at 1325-1402) | Now Playing REST, health and CometD; other ordinary URLs rejected | No `/stream.mp3`, local music file or Range handler here |
| `sbproxy`, B04 | Loopback listener; curl fetches HTTP/HTTPS upstream, HLS relay; forwards eligible Range/If-Range headers | HTTP audio **proxy/server to the player** plus upstream **client**; not arbitrary filesystem serving or time seeking |
| `sbwebserver` `normal`, B10 (`http_server.c:535`, file serving 655/712) | Mongoose server for management/static/download resources | Existing file-serving primitive is reusable, but no authorized indexed-music route exists |
| Spotify bridge S02 `accept_http:91`, `main:136` | stdin Ogg producer -> bounded page/header handling -> single-client `/spotify.ogg`, default loopback 17880 | Separate existing app transport; not evidence Base can serve local FLAC/MP3 |
| Radio R01 and Spotify/Qobuz S01/Q01 | Lua configures decoder and HTTP GET directly via `_streamConnect` | Existing playback bypasses a full Base SlimProto controller |

Buffer ownership today is split: proxy transport/curl/HLS buffers; Spotify page queue; SqueezePlay encoded/decode/output buffers; Lua recovery/control state; and `np_manager` confirmed metadata plus position extrapolation. `sbbase` does not own those encoded bytes. Its playback position is supplied by app updates and advanced using a monotonic clock, not decoded `STAT` feedback. HTTP byte Range on a proxy/file server does not establish MP3 VBR/FLAC time-to-frame mapping, codec initialization or queue seeking.

Recommendation: add `/music/stream/<opaque-token>` to the existing `sbwebserver` route table and use the existing file-serving machinery after strict resource authorization. Base supplies the HTTP address/path in `strm-s`; the firmware makes the GET. This requires an internal contract between Base processes, not another public listener, HTTP stack or general-purpose local-file proxy. Zero-offset MP3/FLAC streaming is plausible with small HTTP extensions, but **whole playback is not** a minimal change: controller/status/ownership work is substantial.

A Base-owned private Unix socket (or equivalent bounded local IPC integrated into the existing loops) resolves a short-lived opaque stream token to a validated file descriptor plus length, MIME, media generation and track identity. Prefer descriptor passing after an anchored safe open; the web server owns response bytes, not the playlist. A bounded I/O worker must read removable files off the control loop: nonblocking sockets alone do not make regular-file reads nonblocking. The existing Mongoose file function needs old-kernel tests for backpressure and removal; if it cannot safely consume the validated descriptor, extend its file-provider interface rather than accepting arbitrary paths. Do not expose a filesystem root through static serving.

### 3.2 Metadata

`nowplaying.h` B03 already models title, artist, album, album artist, duration/position, track/disc number, year, genre, content type, source/station, artwork URL and audio properties. B02 `np_api` validates app snapshots; `np_playerstatus_json` and `collect_fields` serialize LMS-like status/songinfo/contextmenu. These are reusable value/serialization contracts, not parsers for music files.

Radio R01 `StreamPlayer.lua:321` parses ICY `StreamTitle`, R02 publishes structured snapshots. Proxy B05 `hls_id3.c:207` handles TIT2/TPE1/TALB and small validated APIC, with streaming v2.3/v2.4 ID3 handling and timestamp/private data; B06 exposes transient HLS snapshots and embedded covers. This bounded parser can contribute text/encoding/image validation routines, but is not a full MP3 indexer: no complete album-artist/disc/track/duration/Xing/ID3v1/file identity implementation follows from it. APIC limits designed for a live stream would reject many ordinary album covers.

No local FLAC Vorbis Comment/Ogg comment scanner is built by B14/B15. Spotify metadata arrives from its application/engine, Qobuz from its service/daemon and Lua snapshots; neither demonstrates reusable on-device file indexing. Keep `np_track` as the bounded display projection of richer database records; long paths and multi-valued artists must not be truncated to fit fixed UI fields. Report truncation only in presentation; preserve full normalized metadata in the database within explicit input limits.

### 3.3 Artwork

Reuse three existing layers instead of adding an independent cache/server:

- B08 `Artwork.route:19`, B09 URL normalization and `ImageRender.lua` route supported Jive artwork URLs through shared handling. Existing firmware SlimServer artwork queue/cache is also retained (F03).
- B04 `/artwork/<width>/<height>/<quality>/...` handles upstream artwork through proxy workers. Raster bytes generally stream through; SVG is delegated. This is not proof that every remote JPEG is resized locally.
- B07 `/api/image/render` supplies asynchronous conversion/cache via a forked worker and pipe. Limits: SVG input 1 MiB, general image input 8 MiB, PNG output 2 MiB, disk cache 20 MiB, TTL seven days, at most two waiters for the active job; worker address-space limit 40 MiB. Cache uses atomic publication, oldest-first eviction, bounded regular-file checks and existing settings/fallback policies. Conversion paths include SVG and validated raster/fallback handling; use the actual renderer options rather than assuming all formats share a resize path.

The native HLS registry B06 retains two transient sessions, small revisioned APIC JPEG/PNG responses at `/hls-meta/<session>/art/<revision>`; this is not a persistent album cover database. No local-file cover selection/extraction lifecycle is present. Extend the image subsystem to accept a trusted local artwork descriptor (embedded offset/length or sibling cover file); keep remote URL checks intact. Never turn a `file://` URL into an unrestricted render source. Cover identity must include medium, relative path, file fingerprint, embedded block/sidecar selection and renderer version/dimensions. Thus a changed cover invalidates its URL immediately instead of waiting seven days. Avoid duplicate original blobs; retain bounded thumbnail variants on the selected medium. Read-only media gets bounded volatile thumbnails or placeholders.

The current persistent image cache expects verified `/mnt/sbdata` unless an override is provided (B07 `cache_directory:524`); that path was absent on the inspected Fab4. It cannot be assumed available for music. Reuse the implementation with an explicit, validated per-medium cache root. Current 40 MiB worker virtual limit is not a measured RSS budget and is too large to adopt blindly for Baby. Schedule scan and expensive cover generation so their worst-case peaks do not overlap unchecked.

### 3.4 SlimProto and state

B01 `handle_slim:1567` parses frames but only acts on HELO (MAC association and publish). B01 `keepalive:1550` emits `strm-t`; `switch_server:960` emits `serv` to a matching connection. B01 does not interpret `STAT`, RESP, DSCO or META. HELO capabilities/model/codec extension data are not fully parsed. Serverstatus currently reports a single global player and hard-coded Baby model details, even in a dual-target product. This is an existing compatibility shortcut, not multi-player music support.

Pause/play/stop and playlist index commands enqueue external-owner messages through B02 `np_queue_command:953`; they do not send pause/start/quit/audio-gain frames. The 32-message queue is bounded and useful for existing integrations but cannot represent a library queue. Seek, buffering recovery, end-of-track advancement, prebuffering and track transitions need native control. See the protocol matrix for exact directions and event distinctions. Initially support one local player and honest capability reporting, not network-player synchronization.

### 3.5 CometD, commands and persistence

B01 reuses request HTTP connections and a chunked Comet event stream, handles handshake/connect/reconnect/disconnect, command response routing and selected subscriptions. Reconnect replaces stale streams and replays player status. Existing notifications include server/player status and firmware status; menu/menustatus is currently alarm-oriented. There is no native My Music query backend. Unknown commands can receive `successful:true` with an empty list; that must never be classified as supported functionality. The dispatch mixes bounded argument parsing with substring matching; music commands require exact validated typed dispatch before generic play/menu fallbacks.

`/slim/request` and `/slim/subscribe` are channels inside POST `/cometd`, not independent HTTP paths. No `/jsonrpc.js` HTTP route or port-9090 text CLI was found in B01. Reuse the command dispatcher behind a bounded JSON-RPC envelope adapter; do not create another server or duplicate query logic. A complete LMS command surface is unnecessary: implement the documented Jive subset and return explicit unsupported responses for other commands without breaking the legacy installer/alarm flows.

Settings B11 and alarm state B12 use filesystem persistence with fsync/rename patterns. No SQLite dependency appears in Base native CMake/build integration (B14/B15). There is no generic portable music-file API. Baby storage helpers concern internal storage recovery/extension; they are not a Fab4 USB/SD automounter. B13 intentionally separates board identity/capabilities.

`sbbase` has one poll loop (32 client slots, 1-second poll timeout), fixed request buffers and single-owner global state; no scanner worker or database locks. `send_data` can poll a slow peer for up to two seconds (B01:199), so loop responsiveness is not guaranteed merely by using poll. `sbproxy` has bounded pthread workers and locks; `sbwebserver` has its own Mongoose loop and a forked image job. These are reusable patterns, not a shared cross-process scheduler. Add short bounded command/result queues, output backpressure and cancellation, never filesystem traversal or tag decoding on the protocol thread.

## 4. TinySC reference: retain behavior, replace implementation

L01-L10 and F01-F06 are installed source references, not runtime dependencies. The earlier report establishes nine installed files matching upstream 7.7.3. Base need not open or migrate LMS `library.db`.

### 4.1 Smaller proposed schema

LMS models tracks, albums, contributors with role links, genres, playlist entries, file URLs, artwork IDs and normalized sort/search strings. Its plugin/migration/statistics framework is unnecessary. Installed migrations reach 18, but no active on-medium schema was observed. Schema 10 playlist members became text URLs; do not copy an assumed integer track-FK layout from older migrations.

Design notation only; no DDL was executed:

| Table | Essential columns and constraints |
|---|---|
| `media` | stable logical ID, filesystem UUID/serial hints, display label, last mount generation, writable/online state; mount path is runtime state |
| `tracks` | ID, media ID, byte-preserving relative path, parent directory ID, size, mtime, inode hint, scan epoch, title/sort/search key, album ID, disc/track number, year, duration_ms, codec/audio properties, audio offset/length, artwork ID, error; unique media/path |
| `albums` | ID, title/sort/search key, album-artist ID, compilation flag, disambiguation key using album artist, title and directory/release hints |
| `contributors` | ID, name, sort/search key; no heuristic splitting at commas |
| `track_contributors` | track, contributor, role, ordinal; performer and album artist, optional composer |
| `genres`, `track_genres` | normalized genres and many-to-many membership |
| `directories` | ID, media, parent, relative path, display name for Music Folder |
| `artwork` | ID, media, relative source path, kind, embedded offset/length, fingerprint, MIME/dimensions; references instead of full image blobs |
| `scan_runs` | media/generation, epoch, start/end, state, cursor/error counters, completed-root markers |
| `saved_playlists`, `saved_playlist_items` | identity/name/source and ordered media/path entries with nullable resolved track ID |
| `queue_entries`, `queue_state` | immutable entry ID, track plus fallback media/path, order; current entry, repeat/shuffle, revision, checkpoint position |

Use `PRAGMA user_version` and transactional forward migrations; reject newer schemas without deleting them. Use foreign keys, prepared statements, bounded integers and parameterized search. Keep raw filename bytes separate from display Unicode; test invalid UTF-8. Essential indexes: unique media/path; album/disc/track/ID; title-sort/ID; contributor/role/track plus reverse lookup; genre/track; media/parent/name/ID; media/scan-epoch; playlist/order and queue/order. Deterministic ID tie-breakers prevent unstable pages. Start with normalized prefix search; substring search needs a bounded strategy or measured FTS. SQLite NOCASE is not full Unicode collation: define accent/case normalization and test locale behavior.

LMS file URLs include unstable mount prefixes. Use stable media identity plus relative path. Filesystem identity and, only with later authorized writable storage, an application marker can identify media. Cloned/absent identifiers need explicit ambiguity/session-identity behavior. Preserve track IDs on ordinary updates. Match rename only with reliable unique same-filesystem inode/size/mtime hints or a bounded fingerprint; otherwise represent delete/add and retain unresolved playlist/queue entries. Do not hash every full track on every scan.

Put the derived index on selected writable external media under `.StandaloneBase/music/`, separate from `.Squeezebox`, to avoid filling internal flash. One active medium is MVP policy; keys permit multiple later. Read-only media needs an explicit bounded temporary-index policy or a clear unsupported message, not a silent internal-flash fallback. Persisted queues must not autoplay after boot.

### 4.2 Scanner and metadata alternatives

L04 `rescan` stages URL/mtime/size and identifies new/changed/deleted files. L05 File::Next applies file/directory filters and async scheduling, while filesystem calls can still block. L06 batches directory-watch changes (about 15 seconds in earlier analysis). Reproduce bounded traversal, changed-fingerprint metadata reads, idempotent upserts and mark/sweep deletion only after a completed root/generation. Cancel/EIO/removal must not delete an offline library. Watch new directories; inotify overflow schedules reconciliation. Periodic/explicit rescan remains the correctness backstop.

MVP candidates `.mp3`, `.flac`, `.ogg` require signature sniffing and decoder capability checks. MP3/FLAC playback is the first acceptance gate; Ogg follows its own corpus. AAC/ALAC MP4 stays opt-in until metadata, container streaming and playback are validated. Ignore cache directories and special files; bound depth/path lengths and do not follow symlinks by default. LMS shortcut/alias handling is visible, but Linux File::Next symlink-cycle behavior was not fully established; test Base policy explicitly.

No alternative was cross-built or benchmarked in Phase 0. All require ARMv5 soft-float and ARMv6 builds plus old Linux/musl syscall checks.

| Alternative | Coverage/integration | Resource/build/license tradeoff | Decision |
|---|---|---|---|
| Existing HLS ID3 routines plus focused C readers | Existing text/APIC validation; missing general MP3 properties/album-artist/disc/track tags, FLAC/Ogg and MP4 | Controllable footprint likely; largest parser/fuzzing maintenance burden; preserve source licenses | Compare limited MVP route |
| TagLib behind C ABI | Broad MP3/FLAC/Ogg/MP4 tags/properties; artwork needs richer API than generic binding | C++ runtime/version requirements, CMake integration, LGPL/MPL choice; size unknown | Benchmark candidate/reference |
| libid3tag + libFLAC metadata + libogg/comment reader | Native C, focused formats; MP3 properties and MP4 still need work | Multiple dependencies; GPL libid3tag and BSD-style Xiph terms need pinned review | Strong C-oriented comparator |
| Trimmed libavformat/libavutil | Broad probing/demux/metadata and potential seek helpers | Larger dependency/probing surface, cross-build complexity; license depends on configuration | Fallback if small readers fail correctness |

Sources: [TagLib API](https://taglib.org/api/), [build requirements](https://github.com/taglib/taglib/blob/master/INSTALL.md), [libid3tag upstream mirror](https://github.com/tenacityteam/libid3tag), [FLAC metadata API](https://xiph.org/flac/api/group__flac__metadata.html), [Vorbisfile](https://xiph.org/vorbis/doc/vorbisfile/), [FFmpeg API](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/avformat.h) and [license configuration](https://ffmpeg.org/legal.html). Vorbisfile includes decoding, so a limited Ogg comment reader may fit better. No footprint claims are derived from these pages.

WP0 should compare C composition against TagLib using the same ID3v1/v2.2/v2.3/v2.4, Unicode, VBR/Xing/VBRI, FLAC comments/PICTURE, Ogg chained/large-comment, malformed/truncated and large-cover corpus. Measure correctness, stripped static text/data size, startup/peak RSS, CPU-ms/file and maximum bytes read on both targets. Pin a compiler-compatible version only after that gate; GCC 8.4 must not be assumed compatible with every current release. Duration probing must not decode an entire track. AAC/ALAC and gapless metadata are separate gates. Static-link obligations and any LMS code borrowing require a pinned-source license/notice review during implementation.

### 4.3 Original playback and navigation

L01 browse handlers produce menus; L02 `myMusicMenu` registers nodes. F02 `_actionHandler` combines item/base actions, variables, input and pagination; F03 `userRequest` uses CometD. Preserve `count`, `offset`, `item_loop`, `window`, `base.actions` and item actions. Raw `albums_loop` is insufficient for SlimBrowser. The protocol document maps all fourteen requested operations.

L07 opens decoded file URLs with offsets/logical end and format-specific seek state. L08 interprets status/HTTP/disconnect signals and the server controller advances the queue. F01 `_strm:852` starts native decode/HTTP and handles pause/resume/flush/quit; PCM goes to existing ALSA helpers. TinySC's `/stream.mp3` name does not imply transcoding. Base takes over queue, codec selection, commands, status and resource lifetimes; SqueezePlay keeps decode/PCM/ALSA.

MVP: byte-zero MP3/FLAC, pause/resume/stop, honest duration/elapsed, queue replace/add, next/previous, deterministic end and explicit errors. HTTP EOF is not audio drained; STMd is decoder-ready, not audible completion; STMs marks track start. Interpret STMu and disconnect/error events in context, using stream epochs to reject stale events. Later: format-specific seek, gapless/prebuffer, persisted resume and advanced shuffle. Firmware transition support does not prove Base can drive it correctly.

### 4.4 Storage UX

F06 udev already mounts media under `/media/<device>`: no new automounter. F04 MediaManager handles Fab4 uevents, mount checks, storage menus/eject callbacks. F05 SqueezeCenter supplies TinySC start/select/scan actions; replace those with a narrow Base storage adapter. L09 selects one medium and `.Squeezebox/cache` by policy.

Reuse MediaManager visuals and OS mounts. Base actions select medium, show/cancel/reconcile scan, browse My Music and safe eject. Never call SqueezeCenter start/rescan or write its prefs. Future integration must intercept old activation paths; the existing startup port guard cannot guarantee TinySC stays off after insertion. Fail clearly on conflicts rather than stopping services automatically.

On-mount callback registration stores a table, but an invocation on the active mount path was not found. Do not rely on it untested. Use existing uevent notification plus bounded mount reconciliation/startup enumeration; tolerate duplicate/reordered events. Safe eject quiesces scan, revokes streams, stops local playback, closes audio/artwork/DB descriptors, checkpoints/closes DB and acknowledges before the existing UI unmounts. Physical removal may arrive first: invalidate generations, cancel, mark offline, show unavailable, retain unresolved queue entries and do not skip through every missing track. Never recreate a database inside an empty former mount directory.

## 5. Proposed execution and ownership contracts

Group responsibilities into the existing backend: sbbase owns typed dispatch, player/owner epochs, queue, transport, storage state and notifications; a Base database worker owns SQLite; a single scanner helper emits bounded records without a network listener; existing sbwebserver serves authorized audio and artwork; sbproxy retains existing network/HLS traffic; Jive retains UI with a narrow storage and owner-handoff adapter. These are logical modules, not nine new daemons.

Prefer a low-priority separate native scanner process. Parser faults and slow USB operations cannot crash/block sbbase and exit reclaims parser heap. It costs stack/allocator/IPC memory, but remains a helper of the same backend without an independent server. A thread is smaller but shares heap faults and cannot safely be cancelled during a blocked parse. Reconsider only with measurements. A single in-process DB worker owns its connection; scanner records go through it, not a second writer. Cap pending requests, keep DB calls off the control loop and show unavailable on timeout. A separate DB helper is the fallback if removal tests reveal inadequate isolation.

Proposed internal contracts (not implemented):

- `storage_snapshot`: stable medium ID, validated root descriptor, availability/write policy and mount generation. Every operation carries that generation.
- `scan_start/cancel`: job ID, bounded metadata/progress batches and terminal completion; stale generations cannot publish.
- `library_query(filters,offset,limit,revision)`: immutable bounded page, initially 50 rows plus a byte cap; interactive reads take priority between scan batches.
- `queue_apply(owner_generation,expected_revision,operation,selection)`: resolve stable track IDs and atomically update order/revision. Browsing/scanning never claims audio.
- `transport_start(entry,stream_epoch)` and transport events: controller owns player state; workers only return results.
- `resource_open(token,epoch,media_generation)`: anchored safe regular-file open, read-only descriptor, exact byte bounds and MIME through bounded local IPC. Prefer descriptor passing; reject path escapes/symlinks/stale tokens.
- `artwork_resolve(cover_id,fingerprint,size)`: reuse image worker/cache, separating trusted local sources from remote URL policy.

Build SQLite into Base, never load old firmware glibc SQLite/Perl bindings into musl binaries. Start THREADSAFE enabled, one exclusive DB-worker connection, ~1 MiB page-cache target, mmap disabled, prepared statements and bounded busy retries. Commit batches of up to 100 records or 100 ms. Worker completion wakes the poll loop through an FD with bounded processing per turn; no DB lock waits on the protocol thread. See [SQLite threading](https://www.sqlite.org/threadsafe.html).

Choose journaling after filesystem tests: one writer and short transactions allow DELETE journal; synchronous NORMAL/FULL is a measured durability choice rather than inheriting TinySC synchronous OFF. WAL needs correct locking, bounded checkpoints and coordinated removable sidecars; its [requirements](https://www.sqlite.org/wal.html) preclude blindly forcing it on every mount. Preserve a corrupt original; rebuild only derived data on validated writable storage under an explicit recovery policy. Keep user playlist/queue export separately recoverable and leave LMS DB untouched. USB/FAT power-loss reliability remains unverified.

### Ownership and handoff

B02 claim resets status and increments generation; old sessions fail updates. This is metadata ownership, not physical audio arbitration. Radio R01 wraps strm/cont/aude and gain/disconnect; Qobuz Q01 disconnects SlimProto; Spotify S03 advertises all Base control capabilities false because there is no safe Lua-to-librespot user-command path.

Propose an acknowledged transaction: explicit local Play, request previous owner yield, app stops transport and suspends/restores hooks, acknowledgement or refusal, player reattachment, new owner generation, then local strm-s. Timeout aborts local start visibly. Spotify yield may need a separate adapter independent of its false user-control flags. New explicit app activity may reclaim; retired generations cannot silently resume/change a queue. Native local controls go directly to the controller; external owners retain existing REST/status-code/command-poll semantics. Alarms/sleep participate. Insertion, scan, browsing and subscriptions never claim audio.

## 6. Performance targets and measurement

All figures below are proposed acceptance targets, not measurements or promises. CPU means one-core utilization. Measure additional private/PSS memory where available, RSS separately, including scanner, DB, web I/O, artwork child and added Lua. Baby limits are provisional pending actual RAM evidence.

| Metric | Target | Measurement condition |
|---|---|---|
| Additional idle CPU | <=0.5% over 10 minutes | Same baseline firmware/apps/settings; no scan |
| Additional idle RAM | <=3 MiB | Separate no-medium and mounted-indexed cases |
| Scan CPU | <=20% playing, <=50% otherwise idle | Explicit duty-cycle cap; nice alone is insufficient |
| Scanner private RAM | <=4 MiB target, 8 MiB experimental ceiling | Include DB/cache separately; reject oversized inputs |
| Combined new RAM | <=8 MiB playing/browsing, <=12 MiB scan/thumbnail peak | Preserve device-specific free/reclaimable headroom; reduce concurrency |
| Browse p95 | <=150 ms warm, <=500 ms cold | 50-row page, 10k tracks, random/last pages, scan on/off |
| Artwork p95 | <=100 ms hit, <=1 s ordinary cold | <=1 MiB typical input, 320px output; bounded failure/placeholder |
| Playback | Zero new underruns/errors in 60 minutes | MP3/FLAC plus browsing/scanning; distinguish normal terminal drain |
| DB size | <=2 KiB/track average including indexes | 10k documented corpus, excluding covers; bound pathological tags |
| Cold ready | <=2 s existing valid library | Start to first page; migrations reported separately; no blocking full scan |
| Incremental | 10 changed files/10k warm corpus <=5 s after debounce | Full remount reconciliation separate <=30 s reference-media target |
| Initial scan | >=10 ordinary MP3/FLAC files/s idle | Report active CPU cap/bytes; no universal bound on faulty media |
| Cancellation | UI acknowledgement <=250 ms, helper exit target <=2 s | Kernel uninterruptible I/O can exceed exit target without blocking control |
| Internal flash | No periodic idle writes | DB/cache external, queue checkpoints only on events/coarse intervals |

Test 1k/10k/50k fixtures, long tags/large covers, multi-disc/compilations/Unicode, FAT/NTFS/ext where supported, USB/SD, slow and read-only media. Query plans must use intended indexes; 50k is a stress case requiring reported scaling. Collect p50/p95/p99, CPU/wall time, page faults, private/RSS peaks, byte counts, DB/journal/WAL size, loop lag, FD count, reconnects and decoder/ALSA errors. Summed RSS is not unique memory. Earlier short passive samples are not a controlled before/after baseline.

Current Base has bounded workers/polling but no common CPU/I/O governor. Proposed scanner uses nice plus explicit work/sleep duty cycles outside locks, bounded reads and cancellation between chunks/files. Check old-kernel ioprio/fadvise support; fall back to portable cooperative budgets. Back off under playback pressure, prioritize control/Comet heartbeats and bound artwork/scan overlap. C alone does not guarantee these targets.

## 7. Open questions and follow-up gates

Decided: Base-only backend, no TinySC/Perl, stock decoder/ALSA/Jive, existing HTTP/artwork stack, one active medium initially, portable core, MP3/FLAC first and no transcoding. Parser choice, SQLite journal mode, resource IPC and concrete handoff APIs remain WP0 decisions.

1. Verify Baby board/CPU/MemTotal/image and free-memory headroom; the generic 64 MiB Freescale patch is not shipping-device proof.
2. Validate source-derived Jive fixtures for all menu/actions/notifications. Examples in the protocol document are reconstructed, never captured traffic.
3. Test Radio hook release, Spotify yield, Qobuz SlimProto reconnection, alarms/sleep, reconnect and reboot in a later approved session.
4. Validate old-kernel file serving: large files, HEAD/Range, slow clients/media, resource revocation, removal and FD/process cleanup.
5. Negotiate codecs and test AAC/ALAC/container limits, duration/gapless metadata and format-specific seek headers.
6. Test SQLite locking/journal/power loss on supported filesystems, identity collisions, read-only policy and offline-copy corruption recovery.
7. Trace every firmware path that could activate TinySC after insertion; verify the apparently unused mount callback and ensure future adapters bypass all TinySC actions.
8. Measure cold-cover conversion and parser choices before adopting memory limits for Baby/Fab4.

Start with WP0 contracts/regression fixtures and parser/build measurements behind a disabled feature, as detailed in the roadmap. No WP0/WP1 implementation has started. Stop after Phase 0; further implementation requires explicit approval.

### Additional source-confirmed constraints

Radio handoff R01 StreamPlayer.lua:593-597 explicitly ignores localhost/127.* destinations. Its existing external-library chooser hook therefore does not solve takeover by a local Base music library; the small Lua ownership adapter is mandatory. Qobuz disconnects the shared SlimProto connection, so restoring metadata alone is insufficient.

Vendored Mongoose B16 mg_http_serve_file:1942 already implements HEAD and basic Range/206/416, but its offsets and content length use size_t (line 1989). On 32-bit ARM, large-file and suffix/multiple-range behavior cannot be assumed fully compliant. Its static file callback does regular-file reads on the event loop. Reuse this server and response plumbing while extending the validated file provider/off-loop bounded I/O and width-safe counters as needed. Do not count the entire Range implementation as missing or count the existing primitive as sufficient for production music.
