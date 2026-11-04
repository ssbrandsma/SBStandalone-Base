# WP4a.1 ? Firmware integration and physical ownership

2026-11-03. Production bridge code and opt-in adapters are implemented and tested. **Activation remains blocked:** qualified producer-stop drivers, exact firmware/probe qualification and the disabled production Music bootstrap are still required. No device, existing service, boot script, output configuration or user library was modified. No deployment, commit, push or tag was performed.

| Repository | Actual package changes |
| --- | --- |
| StandaloneBase | Authenticated protocol on existing HTTP listener; native Music cleanup/readiness/proof bindings; bounded asynchronous Lua client/runtime/binding; exact hook tracking and queued-stop observer; opt-in Music profile; tests/build scripts. |
| squeezebox | RadioOwnership profile; station/restart/watchdog/recovery/gain-hook fences; tracked wrappers; explicit release and fresh-grant reacquisition. |
| SBSpotifyConnect | SpotifyOwnership profile; playback/resume/gain gates; service operation and startup callback fences; watcher marker consumption; asynchronous producer-driver contract. |
| QoBuz | QobuzOwnership profile; recovery/start/resume/gain gates; exact header restoration; opt-in Base connection retention; stale command callback rejection; producer-driver contract. |

Existing uncommitted changes were preserved in every repository. The input and final source ledgers distinguish previous work from this package.

The bridge requires an explicit token-path argument, kernel-verified loopback peer and a per-start256-bit capability in a private volatile directory. It uses no fixture proof route, new public port, metadata lease or blocking Jive operation. New Lua entry points have no startup callers; Music remains disabled by default.

Release uses fence ? producer acknowledgement ? hook restoration ? asynchronous decoder stop observation ? correlated native ACK. Missing, stale or failed callbacks cannot grant ownership. Music additionally waits for private resource RESET/ACTIVATE work; one native stream per qualified connection remains mandatory.

## Remaining activation gates

Spotify supervisor/bridge and Qobuz daemon do not currently provide a qualified correlated asynchronous shutdown/restart-fence acknowledgement. Concrete adapters require `ownershipDriver:stop(operation, callback)` and remain unavailable without it. Radio direct-stream cleanup is implemented; HLS requires a qualified session-stop driver. No process-disappearance, shell-stop, HTTP200 or returned stop shortcut is substituted.

`qualifiedFirmware` defaults false. The implemented observer checks two separate zero-state/empty-FIFO observations; it is not measured analogue silence or proof that ALSA's kernel ring is empty. Native Music bindings exist, but the bridge-only CLI does not construct the disabled production root/library/resource/controller bootstrap. Actual app/hardware compatibility is unqualified.

This package is ready for code review, **not a claim that all production handoff acceptance items are qualified**. Complete/review the unavailable producer and bootstrap bindings before WP4a.2 hardware execution. The controlled hardware/rollback procedure is prepared and unexecuted. No WP5/WP6b work began.

## Validation

95/95 available host CTests pass in215.88s. Additional standby/failure assertions pass under host, QEMU and sanitizers. Existing Radio23 Lua suites, Spotify25 tests and both Qobuz Lua suites pass. Baby/Fab4 static ownership/playback builds and defined QEMU/ASan/UBSan runs pass. Fresh host proxy configuration still lacks CURL_LIBRARY/CURL_INCLUDE_DIR; those tests are not counted.

Dedicated host measurements: HTTP ownership request p95=0.391ms; four held subscriptions added no measurable CPU ticks over0.5s at100Hz; Base peak RSS6,528KiB and FDs7?11?7.5,000 pipe/JSON protocol requests ran at10,818/s including Python/IPC. Controlled producer/decoder simulations completed handoffs in50?100ms, not device timings.

See [bridge](STANDALONE_MUSIC_WP4A1_BRIDGE.md), [Radio](STANDALONE_MUSIC_WP4A1_RADIO.md), [Spotify](STANDALONE_MUSIC_WP4A1_SPOTIFY.md), [Qobuz](STANDALONE_MUSIC_WP4A1_QOBUZ.md), [hooks](STANDALONE_MUSIC_WP4A1_HOOK_LIFECYCLE.md), [tests](STANDALONE_MUSIC_WP4A1_TEST_RESULTS.md), [hardware/rollback](STANDALONE_MUSIC_WP4A1_HARDWARE_PLAN.md) and [evidence](STANDALONE_MUSIC_WP4A1_EVIDENCE.md).

## Integration plan recorded before implementation

Reuse the WP3a physical manager, WP4 MusicOwnership seam and existing Base HTTP listener/output queues. Explicit opt-in creates a per-start random capability in a private volatile directory; requests require the capability and loopback peer. No public port or unauthenticated proof route. Bound commands, identities, retries, long-poll subscriptions and deadlines. Authentication trusts the same privileged Jive process and local administrator; peer credentials on Unix sockets were considered, but installed Lua Unix-socket support is not established, whereas the existing asynchronous SocketHttp/RequestHttp is available. No secret is embedded in app sources.

Each application receives a concrete opt-in fence/release/reacquire adapter. Fence all restart, resume and gain paths, cancel producer work, restore exact owned wrappers without overwriting a newer wrapper, then observe producer and decoder stop. Firmware stop is queued, not synchronous. Unknown firmware/producers must refuse proof. The decoder status observation is conservative and is not a measurement of analogue silence; hardware qualification remains mandatory.

Preserve the one-native-stream-per-connection boundary and current queue intent. Bridge connection tuples originate from actual Base HELO/detach, not arbitrary client-supplied generations. Reconnect/idle acknowledgement requires independent release evidence. Alarm/standby holds cannot manufacture grants. Missing application/probe bindings leave takeover unavailable.

Add real Lua/native protocol and application-adapter tests using instrumented dependencies, then run existing regressions, dual cross-builds, QEMU and practical sanitizers. Produce all nine reports, source-line matrix, security model, resource observations and an unexecuted hardware/rollback procedure. Stop after WP4a.1.

Additional offline native regressions: Spotify supervisor1/1 and Qobuz daemon7/7 unit tests pass. Only private test executables ran; producer services/daemon main routines were not started. Cargo.lock remains unchanged.
