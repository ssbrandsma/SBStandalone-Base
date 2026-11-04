# WP3a native SlimProto and physical ownership

Native foundation completed 2026-11-02. The integration plan below was recorded before implementation. Product activation stays disabled. No device, service, user library or sibling repository was modified. Real physical app handoffs remain unqualified.

## Ownership review and planned integration

| Path | Producer / HTTP stream | Decoder and UI controls | Metadata / position / recovery | Physical release requirement |
|---|---|---|---|---|
| Radio | sbproxy HTTPS/HLS or direct HTTP; StreamPlayer Lua | Direct decode:start and Playback stream; overrides strm/cont/aude and remote gain while owned | Radio ICY/HLS callbacks and Base metadata lease; Lua watchdog/standby recovery | Stop producer/retries, clear desired station, relinquish ownsPlayback/hooks; loopback source selection currently excluded |
| Spotify | Rust stdin Ogg bridge, loopback /spotify.ogg | SpotifyPlayback direct Vorbis decoder, local gain curve/sequence and remote control from daemon | Spotify snapshot lease; app manages reconnect and pause/resume | Stop engine/bridge and Playback, prevent restart, acknowledge physical quiescence |
| Qobuz | Existing sbproxy with service URLs | QobuzPlayback disconnects SlimProto, starts MP3/FLAC directly, installs HTTP-header reporting hook | Qobuz generation and decoder/output monitor, metadata publisher | Stop daemon producer, stop decoder, restore header hook and SlimProto attachment; current stop alone does not prove restoration |
| Base | No encoded audio producer | Existing command mailbox for external apps, heartbeat and server switch only | np_manager owns metadata lease/projection, not physical decoder; alarm/sleep enqueue commands | Metadata expiration/release never serves as physical acknowledgement |
| Stock firmware | Playback HTTP stream, decoder shared buffers and ALSA | LocalPlayer/Playback volume, routing, standby; SlimProto callbacks | STAT/RESP/DSCO; reconnect replays readiness/underrun | Fresh explicit adapter acknowledgement; socket disconnect alone proves neither output drain nor hooks restored |

Reuse protocol.c framing, discovery, identity, keepalive and the existing poll loop. Add independently testable typed wire/state/ownership APIs under sbbase; no second server, scanner/SQL dependency, new listener or normal-operation start command. Native construction must do nothing when disabled. Existing applet takeover paths remain unchanged; fake registered adapters exercise an explicit future integration boundary.

SlimProto contains no stream epoch. A local token cannot disambiguate arbitrary late wire packets on a reused connection. The conservative initial controller therefore admits only one stream per attached connection, requires explicit trusted quiescence confirmation, and requires a new connection before another stream. Transport generations reject queued callbacks; fresh connection and explicit hook/producer reset are prerequisites, not invented on-wire fields. No gapless/queue/playback activation is attempted.

Replace Base's repeated two-second blocking send waits with bounded per-client output, writable poll readiness, partial-write handling, finite per-pass work and close-on-overflow/deadline. Preserve HTTP close-after-drain and Comet response order. This networking fix is justified independently of the disabled music gate. Slow clients must reconnect rather than silently lose subscribed messages. Record remaining synchronous operations separately.

Baseline and final regression suites, exact protocol fixtures, mock owner lifecycle, slow-client/backpressure tests, sanitizers, both static ARM builds and host-kernel QEMU will provide evidence. Hardware audio, old-kernel behavior and app adapter qualification remain explicitly unverified. Stop after WP3a.

## Actual implementation

Added slim_controller.h/.c under the existing sbbase component: validated HELO identity/model/capabilities, exact supported STAT/RESP/DSCO layouts, bounded strm/audg/aude/cont serializers, a deterministic caller-owned transport and physical ownership manager. Existing WP0 identities, yield guards and independent stream event flags are reused. No second server, producer, playlist, file stream, SQL/scanner dependency or decoder implementation is introduced. Installed firmware has no codc handler, so no codc support is invented.

Ownership uses explicit registered adapter incarnations, request/deadline/connection/generation tuples, asynchronous pending-yield snapshots and positive producer/hook/decoder release proofs. A metadata lease never supplies physical proof. Refusal/timeout/crash/reconnect fails closed, and release/recovery is explicit. Future Music owned_prepare/owned_command check the same arbitration interface as mock Radio/Spotify/Qobuz owners. No real applet adapter bridge or takeover behavior is activated.

Transport distinguishes input EOF, decoder readiness, audible start and output drain; terminal results occur once. Incoming progress/start/terminal replay and stale connection/stream/owner callbacks are rejected. A flush packet is not physical stop acknowledgement. Unsupported codec/HTTP/input errors and missing confirmations have bounded failure paths. Because SlimProto has no stream token, the initial safe subset requires a fresh connection and explicit quiescence for every stream; same-connection multi-track correlation is a future integration gate.

Base TCP output now uses lazily allocated 64 KiB per-client queues, immediate nonblocking sends and poll-driven bounded partial-write flushing. Overflow/error/backlog expiry closes the client; HTTP close waits for drain. Heartbeat no longer bypasses ordering with an unchecked send. This compatible networking fix and strict typed HELO/model handling apply while Music remains off; the new controller/manager itself is never constructed by production Base. All other application controls, Now Playing metadata, UI/firmware audio hooks, alarms/sleep and routing remain on their existing paths.

## Results

- Baseline available suite: **72/72, 160.39 s**. Final available suite: **76/76, 167.30 s**, including prior WP0/WP1/WP6a/WP2 and current application/API regressions. Three legacy eight-byte fake HELO fixtures were corrected to the actual 36-byte format. A private host fixture-filesystem flush wrapper avoids unrelated stalled WSL 9p global sync; the original failure and successful retest are retained.
- Native ASan/UBSan/leak tests and instrumented real Base partial-write/backpressure tests pass. Both normal static Baby/Fab4 builds and isolated native controller archives/tests pass. QEMU executes ARM lifecycle/socket tests and independent wire/old-WP0 fixtures on both architectures. QEMU uses the host kernel; no hardware execution is claimed.
- Under two stopped readers, the pre-change host update stalled **4045.778 ms**. Final normal/forced-partial runs each deliver all 160 fast-peer updates in order, with maximum update latency **2.852 / 0.818 ms** respectively. Saturated peers close observably; FDs return **6 -> 10 attached -> 6**. These are small fixture observations, not device performance guarantees.
- Host transport/owner/event structures use **1368 / 120 / 1240 bytes**, with no heap allocation. Fixed existing client-array overhead increases **2048 bytes total**; pending payload storage is at most **2 MiB across 32 clients** and allocated only under backpressure. Exact throughput/CPU/RSS and sample limits are in the performance report.
- Repeated host full-proxy configure still fails for missing CURL_LIBRARY/CURL_INCLUDE_DIR. Excluded proxy coverage is not counted. Existing image_render/nanosvg build warnings remain; new native controller/output modules are warning-free.

## Readiness and remaining gates

WP3b can build the disabled browse layer using WP1/WP2 contracts, with a real asynchronous database dispatcher rather than blocking the protocol thread. WP4 can consume the typed transport/ownership/output APIs, but must first implement asynchronous real app adapter registration/yield proof, producer restart fencing, decoder/hook restoration, fresh connection handling and resource-token provisioning. The one-stream-per-connection subset is explicit; safe same-connection fencing, playback/queue policy and audible hardware timing are not supplied by this package.

Radio's loopback exclusion, Spotify producer/control boundary and Qobuz hook/SlimProto restoration remain concrete adapter dependencies. Host mocks do not qualify those real handoffs. Actual Baby/Fab4 playback, firmware reconnect, standby/alarm/sleep audio, analogue/SPDIF/capture routing, old-kernel behavior and resource headroom require a separately authorized device session. Synchronous logging/catalog/state/resolution paths outside addressed TCP writes remain unchanged. No forced TinySC restart/stop/start, deployment, commit, push or release was performed.

Recommended next package: **WP3b browsing**, retaining the activation gate. **Stop after WP3a; WP3b/WP4/device activation are not started.**

## Deliverables

- [SlimProto layouts, commands and state](STANDALONE_MUSIC_WP3A_SLIMPROTO.md)
- [Physical ownership and adapter requirements](STANDALONE_MUSIC_WP3A_OWNERSHIP.md)
- [Socket output and remaining blocking paths](STANDALONE_MUSIC_WP3A_SOCKET_IO.md)
- [Test results and reproduction](STANDALONE_MUSIC_WP3A_TEST_RESULTS.md)
- [Performance and resource limits](STANDALONE_MUSIC_WP3A_PERFORMANCE.md)
- [Evidence and provenance](STANDALONE_MUSIC_WP3A_EVIDENCE.md)
- [Source ledger](STANDALONE_MUSIC_WP3A_SOURCES.json), [measurement ledger](STANDALONE_MUSIC_WP3A_MEASUREMENTS.json)
