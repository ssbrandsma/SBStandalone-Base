# WP3b: Native My Music browsing

Completed implementation and available validation, 2026-11-03. Original Jive compatibility is source-derived and tested with native host/QEMU protocol fixtures; physical Jive UI validation remains outstanding. No device, existing service or user library was modified.

Native C now supplies Artists, Albums, Tracks, Genres and Music Folder browsing, metadata-only track leaves, summary counts, stable IDs, deterministic pagination, menu merging, subscriptions and reconnect replay. No Play/Add action, local playback, artwork endpoint, automatic scanner or storage event activation was added. Product Standalone Music remains disabled by default.

WP1 remains the single SQLite owner. A bounded asynchronous dispatcher submits through its existing serialized broker, shared with the WP2 scanner. SQL and broker waits run outside the Base event loop. Up to four revision-pinned 50-row native pages satisfy the original Jive 200-row wire contract. Generation changes revoke pending output; scanner publication is coalesced. Neither reader consumes another client's completion.

Validation: baseline 76/76 tests; final 82/82 tests in 192.42 seconds, including all six new browse tests. ASan/UBSan native/concurrent-scanner and partial-write HTTP runs passed. Baby ARMv5 and Fab4 ARMv6 static builds passed, as did native and HTTP functional scenarios under QEMU for both CPUs. The existing missing host libcurl development files still exclude proxy-specific CMake coverage; ARM proxy builds pass. Hardware application behavior is not asserted from these tests.

Host 50k fixture: end-to-end browse p95 18.360 ms, p99 20.247 ms, sampled process peak RSS 8192 KiB, concurrent health p99 0.858 ms. These are mixed-query host observations, not target performance predictions. A summary count query initially missed the media predicate required for the existing compound index; the corrected final three-size run passes. Consecutive page submission also now occurs before returning to poll, avoiding a timer-only handoff gap.

Reused components: WP0 grammar/DTO/media contracts; WP1 schema, indexing, connection, worker and broker; WP2 scanner and transaction ownership; WP6a trusted media identity/generation interfaces; Now Playing JSON tokenizer/escaping; WP3a poll/output queue and existing CometD/menu infrastructure. No database schema change, new external runtime dependency, service, port or Jive applet change.

## Deliverables and boundaries

- [Protocol and source trace](STANDALONE_MUSIC_WP3B_PROTOCOL.md)
- [Native browse API](STANDALONE_MUSIC_WP3B_BROWSE_API.md)
- [Async dispatch and ownership](STANDALONE_MUSIC_WP3B_ASYNC_DISPATCH.md)
- [Tests and build results](STANDALONE_MUSIC_WP3B_TEST_RESULTS.md)
- [Performance and budgets](STANDALONE_MUSIC_WP3B_PERFORMANCE.md)
- [Evidence and hashes](STANDALONE_MUSIC_WP3B_EVIDENCE.md)
- [Observed request/response examples](STANDALONE_MUSIC_WP3B_EXAMPLES.json)

Physical UI navigation, localization, arbitrary LMS extensions and mixed meta-plus-native batches remain unqualified. No search, playlists, Play/Add, artwork serving or music transport is implemented. Numeric cache revisions are not an arbitrary-precision Lua contract; row/navigation IDs are strings. Product media/database placement and owner-quiescence gates from WP6a/WP3a remain prerequisites for activation.

WP4 is the recommended next package only after explicit approval: local file/stream serving and playback must preserve WP3a physical ownership, generation/token revocation and safe producer quiescence. WP3b supplies browse contracts, not a playback-capable library. WP4 and device activation have not begun.

## Preimplementation integration plan (retained)


Integration plan recorded 2026-11-02 before implementation. No physical device, existing service, sibling application or user library is to be modified. Product Music activation stays OFF; only a separately compiled test binary can explicitly open disposable fixture databases. No playback/queue/scan activation or new port.

Reuse WP1's single worker/connection and WP2's serialized broker. Add a bounded query-dispatch worker with a nonblocking caller mailbox/wakeup pipe; only that worker invokes broker exchange. Scanner and browser never independently drain the raw SQLite completion pipe. Extend worker query results with bounded browse projections without changing the WP0 50-row DTO. Cancel only a matching browse token; deadlines and stale-media rejection remain on the control side even if kernel I/O delays worker exit. Explicit broker turn windows and bounded retries permit scanner batches and browse reads to make progress without uncommitted reads.

Reuse WP0 command grammar and Now Playing JSON tokenizer/escaping; do not add a general JSON parser. Source-derived artist/album/genre/folder navigation uses native stable IDs. Track leaves use the installed SlimBrowser action=none contract, preventing built-in playback fallback. Unsupported search/playlist/Play/Add remain absent. Artwork URLs are omitted until WP5.

Important source finding: installed SlimBrowser DB.lua uses **200-row wire blocks**, whereas WP1 allows 50-row native pages. The browse layer must assemble up to four revision-pinned native pages into one bounded Jive response rather than silently cap a wire block at 50. Response memory and queued socket bytes are independently bounded; large fixture responses must drain incrementally through existing nonblocking output.

Merge My Music child nodes into existing alarm/sleep menu data and the menustatus notification-array shape. Fixed IDs/weights follow LMS BrowseLibrary; SlimMenus maps myMusic to its local _myMusic node. Add/remove notifications, reconnect replay and subscription cancellation must preserve current firmware/player/alarm channels. Online/source generations revoke pending results before publication; notifications are coalesced, not per indexed file. Baby no-storage capability never advertises a library.

Installed SlimBrowser/DB/LocalPlayer/NowPlaying/MediaManager/SqueezeCenter and LMS BrowseLibrary/Jive/Queries/Commands/XMLBrowser are references. The earlier firmware subset omits SlimMenus; use the explicitly identified upstream source pinned to the prior squeezeplay commit cca1bba4abeb3fef2b7c4a48a4e4da8fc255666d, not a claimed installed capture. Hardware UI compatibility remains unverified.

Baseline: 76/76 available tests, 175.68 s, with the existing private host fixture-filesystem sync wrapper for stalled WSL global sync. Validate source-derived goldens, pagination, concurrency/cancellation/subscriptions, 1k/10k/50k libraries, sanitizers, both static ARM targets and QEMU. Keep missing host libcurl coverage explicit. Stop after WP3b.
