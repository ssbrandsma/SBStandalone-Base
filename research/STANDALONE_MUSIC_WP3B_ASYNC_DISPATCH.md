# WP3b asynchronous dispatch and completion ownership

## Execution model

The Base event loop validates commands, submits copied requests, polls notifications and serializes bounded rows. One new native coordinator thread (256KiB configured stack) invokes the existing WP1 serialized broker. The existing SQLite worker remains the sole connection owner. WP2 scanner exchanges share the same broker. There is no synchronous SQLite read, writer transaction wait or database completion drain on the event loop.

Eight fixed jobs transition FREE -> WAITING -> RUNNING -> READY -> FREE. Each holds one copied request and result. Admission, polling, cancellation, stop and destroy use try-lock operations; BUSY is retried by the caller, never waited on the protocol thread. Round-robin queued selection prevents one logical call retaining the dispatcher. An idle worker waits on a condition variable. Each completion signals a nonblocking close-on-exec pipe; the caller consumes its own job by ID. No unbounded queue or idle busy polling.

The coordinator calls `exchange_browse`, which reserves a20ms broker read turn before trying acquisition. Existing scanner exchange yields1ms when a read turn is reserved. The coordinator releases/resets its reservation after one exchange and leaves a2ms gap between attempts, including successes. BUSY/transaction CONFLICT retries are limited to500 and to the request deadline. Scanner-owned transactions are never stolen; reads do not observe uncommitted writes. These are cooperative fairness mechanisms, not an exact CPU quota or a hard latency guarantee under kernel stalls.

## Identity, cancellation and shutdown

Front job IDs, WP1 allocated request IDs, browse tokens and scan tokens have separate roles. Only a matching active browse token may be cancelled. The broker serializes submission, completion and receipt, so a scanner cannot consume a browse completion or vice versa. Tests exercise actual WP2 scanning and a held writer transaction on the same handle.

A call checks deadline before accepting even a ready result. Expiration cancels/discards late work; a running slot remains owned until worker completion. If cancellation cannot take the try-lock, a bounded retired-ID list retains it for tick retries. Source/media generation changes invalidate readiness immediately and increment the media epoch before the asynchronous SOURCE update completes. Calls use scoped source/generation/revision; already assembled HTTP output is rechecked against the media epoch. Before headers, it becomes an explicit media_changed response; after partial transmission, the connection closes rather than completing stale content.

Stop marks queued work cancelled and wakes the coordinator. Destroy returns BUSY until it has exited, then joins an already exited thread. Shutdown closes/cancels client calls first, waits outside the serving loop with a five-second bound, then closes WP1. A blocked filesystem/kernel operation may delay destruction beyond its budget; the implementation does not claim cancellation forcibly interrupts arbitrary kernel I/O or join a live thread from the protocol loop.

## Pagination, publication and backpressure

Each short native page has a read snapshot. Page1 pins the revision for remaining pages; mutation yields STALE instead of mixing rows. Next page submission happens immediately before returning to poll so an already drained notification cannot leave a page waiting for a periodic timer. No recursive page processing or whole-library cache is used.

Summary refresh runs at most once per second when online and no scan is active. During a scan, publication is suppressed; completion causes a coalesced revision refresh. Source acknowledgements do not bump the view version repeatedly if readiness did not change. Subscription state is per stream and bounded; reconnect replays a fresh logical query.

At most four native HTTP batches own response state. Complete content length is known before headers. Responses larger than the existing64KiB socket queue are fed in at most16KiB pieces on POLLOUT. POLLOUT is not enabled merely because SQL is pending. Generic events during a partially enqueued Comet chunk use a separate64KiB deferred tail to prevent byte interleaving; overflow closes the stream. Batch assembly deadline is five seconds with250ms error-response grace; event output deadline is15s. EOF cancels that connection's work. Unsolicited pipelining while pending is rejected; legacy pipelining is not newly implemented.
