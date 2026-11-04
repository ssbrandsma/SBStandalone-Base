# WP3b native browse API

## Files and ownership

`native/sbbase/include/music_browse.h` declares the caller/event-loop facade; `src/music_browse.c` owns bounded serializers and browse state. `native/sbmusic/music_async.h/.c` owns the asynchronous broker coordinator. `native/sbmusic/music_library.h/.c` extends the WP1 worker internally with BROWSE and BROWSE_SUMMARY operations. `src/music_browse_http.inc` adapts existing Base CometD only when compiled with SB_MUSIC_BROWSE_TESTING.

`sb_browse_start(library, media, enabled, storage_supported)` requires an explicitly opened WP1 library with startup SOURCE completion already consumed, and trusted WP0/WP6a media identity. It does not discover, mount or validate arbitrary external filesystem paths. Disabled or unsupported storage returns no context/worker. One async browse dispatcher may attach to a library. After attachment, clients must use the serialized broker; do not mix independent raw submit/receive completion consumers.

`sb_browse_fd` supplies the nonblocking notification descriptor to poll. `sb_browse_tick(now)` handles nonblocking source publication, coalesced summary refresh, cancellation retirement and revision changes. `sb_browse_player` sets current player identity. `sb_browse_media(media, scan_active)` immediately invalidates caller-side readiness on source/generation/offline changes and schedules SOURCE asynchronously. The storage adapter must publish trusted generation changes. Same-generation revival after revocation is rejected; reinsertion needs a new generation. `sb_browse_version`, `media_version` and `available` describe publication and revocation epochs.

`sb_browse_begin(command, deadline)` copies a parsed strict WP0 command and returns a caller-owned call, within the eight-live-call limit. `sb_browse_poll` returns 0 pending / 1 complete. JSON remains call-owned until `sb_browse_cancel`, which also frees completed calls. `success` distinguishes explicit data errors; `valid` distinguishes syntactically valid requests before subscription registration. No SQL or synchronous broker wait occurs in these calls. Tick/poll must continue while work is outstanding. `sb_browse_nodes` serializes bounded menu add/remove items. Stop is cooperative; destroy may return BUSY until the coordinator has exited.

## Database integration

BROWSE reuses WP1 query/count/sort SQL and source/generation/revision checks, adding a bounded per-row projection of artist, disc, track and year. Artist contributors are ordered by ordinal and ID and joined with `; ` for display. BROWSE_SUMMARY counts source tracks, used albums and used contributors in one read snapshot. Album existence includes matching media_id so the existing compound track index is usable. Schema remains version3 with no migration, new index, duplicate connection, writer or metadata store. Existing full track detail, Unicode handling, artwork and Now Playing structures remain intact. Contributor inputs remain capped at8 names of512 bytes each; SQLite retains its65536-byte length limit, so the intermediate artist aggregation cannot grow with library size.

The original WP0 page remains at most50 rows. Parallel `sb_ml_browse_info[50]` enriches the internal result; the resulting host `sb_ml_result` is21640 bytes. This is not a claim that the enriched result fits the original16KiB DTO bound. Native internal enum values change; they are not a persistent/wire ABI. Browse token is distinct from scanner token and database request ID.

## Resource and error contract

| Bound | Value |
| --- | --- |
| Native page / wire page | 50 / 200 rows |
| Live browse calls / async slots | 8 / 8 |
| HTTP native batches | 4 |
| Commands per batch | 32 |
| Per-call JSON buffer | 262144 bytes |
| HTTP state allocation, host | 340648 bytes, including output and deferred tail |
| Batch content / deferred tail | at most262016 / 65536 bytes |
| Presentation strings | at most127 UTF-8 bytes per projected field |
| Requested job deadline | at most5000 ms |
| Busy/conflict retries | at most500 attempts within deadline |

Oversized/invalid commands fail, queue saturation returns BUSY/HTTP503, and JSON overflow emits a bounded error instead of truncating valid-looking data. Offline/stale/deadline failures cannot publish old pages. Caller offsets/IDs are checked before SQL; no unvalidated path is accepted. Library summary counts use uint32 limits. Raw metadata and filenames are not expanded into an unbounded response.

## Test-only activation

Normal `sbbase` has no browse integration or SQL link. `sbbase_browse_test` is a separate non-installed target. Its `--test-music-db` accepts only existing canonical `/tmp/sb-music-browse-*/library.db` disposable fixtures carrying an O_NOFOLLOW-checked regular `WP3B_TEST_FIXTURE` marker with exact content `WP3B disposable fixture\n`. Startup opens the fixture before the protocol loop. `--test-music-no-storage` allocates no library even with an unapproved path, modeling Baby. Test-only lifecycle/metrics routes do not exist in the product. No automatic scan, hidden media directory or internal-flash fallback is added. Production feature gate remains OFF and ON remains rejected pending qualification.
