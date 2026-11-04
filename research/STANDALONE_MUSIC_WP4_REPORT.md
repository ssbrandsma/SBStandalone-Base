# WP4 ? Native local music playback and queue management

Implementation and host-fixture integration, 2026-11-03. Standalone Music remains disabled by default. No physical device, production service, sibling application or existing user library was modified. Real application handoff and actual audio remain unresolved activation gates.

## Integration plan recorded before playback integration

Reuse the existing WP1 worker/broker, WP3b async dispatcher, WP6a descriptor-relative storage APIs, WP3a physical owner/transport/output queue and existing sbwebserver Mongoose event loop. Extend these with a bounded native queue, descriptor-capability resource registration over private inherited Unix IPC, bounded off-loop pread jobs and test-only integration. No second HTTP listener/server implementation, arbitrary path route, audio decoder, external runtime or ownership inference from metadata leases.

Queue operations stage at most256 entries in50-record pages and atomically commit one revision. Distinguish intended and STMs-confirmed audible entries. Check operation/revision/context/owner/media identity at every callback. A failed load must retain the committed queue and leave external playback untouched. Validate metadata, codec and resource readiness before authorizing a start.

Physical owner handoff requires all WP3a proof flags: producer stopped, hooks released, decoder quiescent, correlated to adapter incarnation/request/connection/generation. Rejection, timeout or missing adapters must fail closed. Real sibling sources are available for read-only analysis, but are not silently assumed to provide such an adapter. Mocks exercise the contract; actual adapter qualification remains a release gate.

Keep one stream per SlimProto connection. On successful audible drain (STMs plus STMd plus STMu), advance at most once and wait for a fresh connection and explicit quiescence before preparing the next track. STMd and DSCO EOF alone do not advance. Never re-label an old stream with a new epoch on the same socket.

Resources use the WP0 media/owner/stream/expiry/method contract and32-byte opaque tokens. Only a securely opened regular-file descriptor crosses private IPC; web clients supply no path. Eight registry slots and eight read jobs are bounded. Worker results and each output segment must still match current resource generation. HEAD/ranges preserve encoded bytes and64-bit sizes. File reads never run on the Base protocol loop.

## Implemented components

- Native bounded transactional queue: load, add, insert, selection, next/previous, delete, clear, pause/resume and stop; separate intended and confirmed audible identity.
- Paginated asynchronous WP1 ingestion through the existing WP3b dispatcher; revision/context checks and rollback on failed or oversized selection.
- WP6a validated descriptor-relative secure-open worker, version-pinned files and cancellation on generation loss.
- Private inherited Unix descriptor IPC, eight bounded resource slots, GET/HEAD and 64-bit ranges through the existing sbwebserver event loop. No client-supplied paths or additional listener.
- WP3a physical ownership and one-stream-per-connection controller, explicit release proof, terminal-event deduplication and fresh-connection sequencing.
- Original Jive command/action shapes and existing Now Playing projection. Native elapsed time derives from confirmed STAT; MP3 and FLAC playback are eligible, Ogg is not advertised.
- An inactive, dependency-injected Lua ownership seam plus deterministic mock adapters. No production startup or installed app integration was enabled.

The implementation reuses existing storage, database/broker, protocol/output queues, metadata model, HTTP server and artwork placeholder. No new third-party dependency, decoder, LMS/TinySC/Perl runtime, image renderer, mount/eject system or network port was introduced. Native resource limits and detailed state transitions are documented in the companion reports.

## Validation and scope limits

See [test results](STANDALONE_MUSIC_WP4_TEST_RESULTS.md), [measurements](STANDALONE_MUSIC_WP4_PERFORMANCE.md) and [hashed evidence](STANDALONE_MUSIC_WP4_EVIDENCE.md) for the final frozen-source results. HTTP fixtures transfer original encoded bytes and exercise the real Base/web processes; firmware RESP/STAT and physical adapter proofs are mocked. They do not demonstrate actual audio output.

Real Radio/Spotify/Qobuz bindings must fence every producer restart, release hooks, establish a trusted asynchronous bridge and positively observe decoder/output quiescence. Existing stop methods alone are insufficient. No hardware ownership compatibility is claimed. Root rebinding/eject orchestration, persistent queues, seeking, gapless transitions, automatic underrun recovery and artwork rendering are not implemented here. Queue artist presentation retains the first contributor; full metadata stays in WP1.

WP4 stops for review. Qualify the ownership adapters and hardware behavior before any activation; WP5 artwork is the next planned feature package after explicit review. No subsequent package was started.
