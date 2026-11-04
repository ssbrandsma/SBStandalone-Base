# WP4a.1 validation and resource measurements

2026-11-03. Disposable host fixtures and instrumented producer/decoder interfaces only. No physical device or production service was accessed.

| Check | Actual result |
| --- | --- |
| WP0?WP4 and ownership CTests | 95/95 pass,215.88s; includes output/socket/backpressure tests |
| Additional adapter assertions | Standby/release/resume, delayed/missing producer, HLS refusal, foreign hook, timeout/late callback: pass |
| Radio existing suites | 23 Lua suites pass |
| Spotify existing suite | 25 tests pass |
| Qobuz existing suites | lifecycle-policy and nowplaying pass |
| Baby ARMv5 / Fab4 ARMv6 | Production bridge Base + native protocol fixture and seven WP4 binaries per platform build; static EABI5, no INTERP/NEEDED |
| QEMU ARM926 / ARM1136 | Five WP4 native tests, HTTP stream/playback, production-auth HTTP, ownership protocol and actual Lua/native adapters pass per CPU |
| ASan/UBSan | Playback/scanner, partial-send HTTP pipeline, protocol, production-auth HTTP and Lua/native-core integration pass within defined instrumentation |
| Full host proxy | Fresh configure lacks CURL_LIBRARY/CURL_INCLUDE_DIR; not counted as passing |

The production route test uses real token provisioning, modes0700/0600 and real loopback sockets. It rejects wrong/duplicate credentials, invalid bodies, stale registrations, unsafe directories and stale token reuse; health stays responsive while a query waits; real HELO/detach changes the tuple. Protocol tests reject overflow, malformed fields, incomplete proofs, stale requests, concurrent takeovers, timeout, crash and reconnect shortcuts. Native Music resource proof cannot bypass RESET readiness.

Lua integration loads actual Radio/Spotify/Qobuz Playback/Service methods and concrete profiles against the same C protocol core over a bounded stdio driver. Controlled native producer/decoder dependencies supply observed state changes; the bridge does not blindly return positive ACKs. Authentication is independently exercised on production HTTP. WP4 native integration also checks actual private resource RESET ACK before Music readiness and verifies that readiness alone does not release ownership.

Shared firmware helpers run under Lua5.1, covering two-turn stop observation, capture/exception/stale/missing/running/FIFO refusal, exact restoration and isolated module imports. App files with Jive bitwise syntax run under development-only Lupa Lua5.4; existing legacy module tests also pass. Lupa2.8 was privately unpacked, not installed as a product/system dependency.

Sanitizers cover new native bridge/HTTP glue and WP4 queue/load/file/playback/IPC/async/controller paths. Existing scanner/storage/metadata/SQLite archives are not fully instrumented in this package; helper remains uninstrumented for existing process limits. Process-terminated web fixtures do not establish complete graceful webserver leak cleanup. No findings occurred in defined coverage. Existing unrelated firmware/image warnings remain visible.

QEMU runs actual static ARM binaries and production-auth route. HTTP media fixture preparation uses host scanner/helper; it is not an ARM scanning or audible-output test. Native Rust producer daemons were inspected, not modified, started or claimed qualified. Actual producer shutdown/restart fencing, kernel/drain and cloud/audio behavior remain activation gates.

## Host measurements

WSL Ubuntu24.04 x86_64, dedicated measurement without concurrent compilation. Four held subscriptions: no CPU ticks advanced over0.5s at100Hz accounting. Base peak RSS6,528KiB; FDs7?11?7.100 authenticated snapshot requests: p50=0.244ms, p95=0.391ms, p99=0.812ms.5,000 protocol requests over Python/JSON/pipe took0.462s (10,818/s), native fixture peak RSS1,664KiB and FDs4?4. This includes interpreter/IPC costs, not pure parser throughput.

Four real app/profile fixtures plus modules used275.3KiB idle Lua heap; no new messages occurred over1,000 simulated idle milliseconds. This includes test/app code and is not an isolated per-adapter device budget. Instrumented handoffs completed in50ms Radio?Music and100ms for the other five directions with5ms observation steps. Controlled producer delay50ms and two25ms decoder samples determine those simulations; they do not predict device latency.

Bounds: one pending physical handoff, one client mutation per adapter, four subscriptions,384-byte commands,1024-byte replies and fixed native adapter records. Completed timers are cancelled. No reconnect retry loop was added. No exact CPU percentage, hardware silence or Baby/Fab4 RAM budget is claimed.

Fixed bridge structure size: host7488 bytes; ARM32 Baby7464/Fab47464 bytes (ABI size observed under QEMU, not device RSS). It includes five bounded adapter/cache records, with one unused NONE slot. The final focused ownership rerun passes5/5 after extra standby and isolated-import assertions. Final affected-suite and sibling regressions also validate the added retired-wrapper epoch guards; native code is unchanged after the95-test run.

Additional offline native regressions: Spotify supervisor1/1 and Qobuz daemon7/7 unit tests pass. Only private test executables ran; producer services/daemon main routines were not started. Cargo.lock remains unchanged.
