# WP3a test results

2026-11-02. Tests are native host fixtures/source-derived protocol expectations and user-mode QEMU, **not observed hardware audio behavior**. [Raw measurements and hashes](STANDALONE_MUSIC_WP3A_MEASUREMENTS.json) and [source index](STANDALONE_MUSIC_WP3A_SOURCES.json) identify inputs/artifacts. No real device, library or service was used.

## Baseline and regression

Pre-change available host baseline: **72/72 passed, 160.39 seconds**. Four new tests: music_transport, music_transport_wire, base_backpressure, base_partial_writes. The final available suite passes **76/76**; exact final duration is recorded in the measurement ledger. Existing WP0/WP1/WP6a/WP2, HLS metadata, Radio management, Now Playing, Comet lifecycle, alarms/sleep, firmware/installer/catalog and other available regressions remain included.

The first run found two actionable fixture/environment issues: an alarm fixture still sent an eight-byte invented HELO (corrected to the actual 36-byte layout, as were server-switch and Now Playing fixtures), and storage_native_boot hit its ten-second timeout in global sync while unrelated WSL 9p flushes were stuck in kernel I/O. A private host-only PATH wrapper now invokes real sync -f on SB_STORAGE_ROOT under /tmp for those disposable storage fixtures, avoiding unrelated mounts. No production script/storage test implementation was changed, and no device durability claim follows. Raw initial failure and successful retest logs remain in the ledger. The wrapper is not a target dependency.

Full proxy/TLS/HTTP host tests remain excluded because a repeated configure probe reports **missing CURL_LIBRARY/CURL_INCLUDE_DIR**. Independent HLS tests still run; normal ARM sbproxy builds. Unavailable tests are never counted as passing.

## Native deterministic coverage

| Area | Assertions |
|---|---|
| Framing and typed input | Every fragmented prefix, coalesced frames, oversized frame, invalid HELO device/MAC/length, 20/36-byte HELO layouts, reconnect/received high-low endian fields, models/firmware/codecs, exact STAT lengths/fullness bounds, invalid HTTP line/CRLF/body, valid and invalid DSCO reason/length, unknown opcode; 10,000 deterministic arbitrary-byte probes |
| Golden wire | Independent Python struct.pack expectations and checked-in outgoing hex for strm-s/p/u/q/f/t, audg, aude, cont; bounded token/port/gain/enable/capacity rejection; original WP0 incoming HELO/STAT/RESP/DSCO fixtures pass the typed native parser |
| Lifecycle | Prepare requires explicit quiescence and advertised codec; start/buffer/HTTP/audible transitions, pause/resume confirmation, clock ordering/wrap, duplicate progress/start/decoder/terminal events, input FIN before decode/drain, missing audible start, HTTP failure, unsupported codec, input error, recoverable output underrun, timeout, late status after stop, explicit stopped acknowledgement |
| Generations | Wrong owner/stream/connection callbacks rejected; detach while active is failure; reconnect clears state, cannot auto-resume, requires new quiescence; second stream on one connection refused; stale outbound scope rejected and pending scope change requires close |
| Ownership | All twelve directed transfers between Radio/Spotify/Qobuz/Music; NONE acquisition, accepted/rejected/partial proofs, timeout, wrong tuple, stale/duplicate ack, concurrent requests, release/repeated release, disconnect/reconnect, crashed owner, new adapter cleanup, generation exhaustion, alarm/sleep admission hold; owned transport entry points reject stale/non-Music permission |
| Output | Small socketpair buffers force EAGAIN/partial sends; exact byte order, overflow, deterministic backlog expiry, send error/SIGPIPE safety, stale scope, drain/free/clear, 10,000 socketpair cleanup cycles |
| Real Base loopback | Two stopped readers plus one fast Comet reader and metadata request connection; every one of 160 updates delivered in order to fast reader; overflow closes stopped readers; valid complete close-after-drain JSON; repeated subscribe/unsubscribe; 100 malformed reconnects; final FD count returns to baseline; repeat with <=37-byte writes/every third send EAGAIN |
| Disabled operation | Disabled native init leaves caller sentinel storage untouched; no controller construction in production, no owner registration, no new start/control frame, no scanner/SQL/UI/listener/worker activation |

Tests use mock physical proof supplied by registered fake adapters. They do not prove a real producer stopped or a firmware hook was restored. Source review plus existing API/UI regression fixtures cannot replace sound/routing/handoff tests on hardware.

## Sanitizers, targets and dependencies

Direct native protocol/controller/ownership/output tests pass ASan/UBSan with leak detection. An instrumented Base also passes the live partial-write/backpressure workload, with the host shim loaded after libasan. Instrumented RSS is not used for product memory budgets. Sanitizers are host-only, not proof of old-kernel/ARM correctness.

An additional host Base build with -ftrivial-auto-var-init=pattern passes the same live partial-write workload, checking explicit client output-state initialization independently of zero-filled stack pages. This compiler option is host-test-only.

Both Baby ARMv5 and Fab4 ARMv6 isolated controller archives/test executables build warning-free. Both pass native lifecycle/output tests under qemu-arm, also with explicit ARM926 and ARM1136 CPU models, and the independent wire/old-WP0 fixture script invoking actual ARM test executables. Normal complete Baby/Fab4 builds and static EABI verification also pass. Existing image_render/nanosvg warnings are unrelated and remain explicit. No new dynamic/runtime dependency or packaged executable is introduced.

## Reproduction

Use the existing Linux/WSL toolchain and build-dual-tests configuration (BUILD_SBPROXY=OFF, BUILD_TESTING=ON). Run the CMake build, then ctest --test-dir build-dual-tests --output-on-failure sequentially because existing tests share ports. On this workstation, prepend the documented private fixture-only sync wrapper while global WSL 9p sync is stalled. Raw wrapper/input hashes are recorded with measurements; it never skips the fixture filesystem flush.

Run scripts/build-transport-arm.sh all and scripts/build-arm.sh all for isolated/normal targets. With qemu-arm installed, run each build-arm/<target>/transport/test_music_transport and the Python wire script using a tiny executable wrapper that execs qemu-arm with the target and forwards arguments. Run tests/comet/test_backpressure.py with the Base binary and host libbase_socket_fault.so; add --partial for injected writes, or --measure-only against the captured pre-change executable for the limited before sample. Timing uses a separately compiled cc -Os native harness --benchmark. No test downloads fixtures or needs an active Spotify/Qobuz service.

Outstanding qualification: real Baby/Fab4 HELO variants, decoder callbacks/drain/stop timing, adapters and producer restart races, firmware reconnect/standby, alarms/sleep sound, analogue/SPDIF/capture routing, slow-device/socket/CPU behavior and eventual WP4 multi-track correlation. These remain unverified, and product activation remains blocked.
