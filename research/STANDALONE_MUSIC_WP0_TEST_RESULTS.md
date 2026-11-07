# Standalone Music WP0 Test Results and Baseline

Inspection date 2026-11-01. Baseline was established before production edits. No physical device, running device service, firmware setting, music database or storage medium was modified. No TinySC, scan, playback or USB/SD action was initiated.

## Reproduce host checks

From repository root in Linux/WSL (the Windows shell wrappers still forward ARM builds to WSL):

```sh
CMAKE=/mnt/c/Projects/SBHttpsProxy/build/toolchains/cmake-3.31.8-linux-x86_64/bin/cmake
CTEST=/mnt/c/Projects/SBHttpsProxy/build/toolchains/cmake-3.31.8-linux-x86_64/bin/ctest
"$CMAKE" -S . -B build-dual-tests -DBUILD_SBPROXY=OFF
"$CMAKE" --build build-dual-tests -j4
"$CTEST" --test-dir build-dual-tests --output-on-failure
python3 tests/music/benchmark_base.py build-dual-tests/native/sbbase/sbbase --seconds 5 --count 200 --output .local/wp0/after-performance.json
sh scripts/build-arm.sh all
sh tests/music/cross_contracts.sh
```

Run suites serially: existing integration tests share host loopback ports 9000/3483 (and management/proxy ports). They launch and terminate temporary host processes with test configurations; no external service is targeted. If those host ports are occupied, fail/report the conflict rather than terminating arbitrary processes. For a quiet performance comparison, stop build/test jobs first and run the same command/count/duration against baseline and changed binaries. The recorded short samples were taken in the WSL development environment and are observational, not a statistical speed claim.

Baseline: existing build succeeded; **60/60 tests passed in 156.74 s**. Post-change full regression: **67/67 tests passed in 162.91 s**. An intermediate 65-test run also passed, and final five music tests passed separately. Added coverage includes five music boundary/contract/HTTP/wire/descriptor tests and enabling the existing independent HLS ID3 and metadata-wait tests when libcurl-dependent proxy builds are disabled. Existing tests were not weakened or removed.

AddressSanitizer/UndefinedBehaviorSanitizer command and contract executables passed (commands source + nowplaying; contracts source + nowplaying). These are focused checks, not whole-product fuzzing. ARMv5 and ARMv6 contract/command executables compiled statically and passed under qemu-arm; this is emulated execution on the host kernel, not device qualification.

## Baseline boundaries and dependency blocker

Native host `BUILD_SBPROXY=ON` configuration was attempted and failed specifically at `native/sbproxy/CMakeLists.txt:3`: `Could NOT find CURL (missing: CURL_LIBRARY CURL_INCLUDE_DIR)`. The executable curl client exists but development headers/library do not. No system package installation was performed. To run that remaining suite, provide a native host libcurl development prefix, pass `CURL_INCLUDE_DIR` and `CURL_LIBRARY` or install the host development package, configure a separate build with BUILD_SBPROXY=ON, build, and run CTest serially. TLS, HLS HTTP/proxy integration and image-proxy guards in that conditional block are **pending**, not counted in 67 passes. The normal static ARM sbproxy built for both targets. Its production source was unchanged.

Radio/Spotify/Qobuz source/API compatibility was inspected; existing Base Now Playing REST, metadata, control-queue, artwork, firmware, alarm, CometD and management host regressions passed. No sibling app source was changed. Real app audio handoff, remote Connect sessions, sound output, storage insertion/removal and reboot were not tested. No device-level regression coverage is claimed.

## Bounds, allocations and loop analysis

New command parse: fixed 24x512 argument slots, 64-byte player, 16384-byte per-message limit, no heap allocation. Outer Comet body remains bounded at 131072 bytes; JSON depth <=16, <=4096 values and <=32 object keys. Each recursive JSON frame has bounded key bookkeeping. Existing HTTP header limit is 16384; client slots 32 with a 147457-byte input buffer each (about 4.50 MiB reserved for those buffers), response buffers 24000/20000/18000 bytes in HTTP/dispatch. The new parser does not allocate another full HTTP body. Batch output remains bounded by existing add_json behavior; exhaustive large multi-result batches and an explicit overflow error are still a later envelope/output-queue improvement, not a new unbounded queue.

Contract guards allocate no heap, open no descriptors and start no workers/listeners. Resource policy caps eight resources; library page policy caps 50 rows/16384 DTO bytes and queue policy 10000 entries without allocating them. Tests exercise argument/count/string/page/range overflow and stale identity boundaries. The host endpoint benchmark performs hundreds of connection cycles and checks the process FD count returns to baseline. The SCM_RIGHTS feasibility test performs 100 transfers with no FD leak. Parser tools expose their bounded/new-allocation metrics separately.

`native/sbbase/src/main.c` send_data calls `poll(POLLOUT,SEND_WAIT_MS=2000)` on EAGAIN from the same event loop. One stalled socket can therefore delay all other sbbase HTTP, CometD, SlimProto, discovery and scheduling work; repeated partial progress/EINTR can extend the total operation beyond one 2-second wait. sbwebserver/sbproxy are separate processes and are not directly stopped by that loop wait, but dependent control/status suffers. No broad network refactor was made. Before WP3a enables playback: use bounded per-client output queues, poll write readiness and cap work per turn, preserving existing streaming/reconnect semantics. Future tests must fill a slow peer's send buffer and measure health/heartbeat latency and fair cleanup.

## Conservative budgets and pending hardware procedure

Provisional additional music budgets remain: <=0.5% idle CPU over 10 minutes, <=3 MiB idle private memory, <=4 MiB scan-helper target/8 MiB experimental ceiling, <=8 MiB playing+browsing and <=12 MiB combined scan/art peak. Scan CPU <=20% while playing, <=50% idle with explicit duty cycles. Browse p95 <=150 ms warm/500 ms cold; cache-hit covers <=100 ms; no new playback underruns. These are targets, not measured WP0/device results. Baby RAM is still unverified; do not allocate against an assumed 64 MiB. Fab4's earlier 125848 KiB MemTotal is historical device evidence, not a new WP0 measurement.

Pending device baseline, read-only, using already authorized SSH access in a separate test session:

```sh
ssh root@DEVICE 'cat /proc/cpuinfo; cat /proc/meminfo; cat /proc/mounts; cat /proc/stat; cat /proc/net/tcp; cat /proc/net/udp'
ssh root@DEVICE 'ps; cat /proc/sys/fs/file-nr'
```

Resolve each sbbase/sbproxy/sbwebserver/jive/jive_alsa PID from that snapshot; collect `/proc/PID/stat`, `/proc/PID/status`, `/proc/PID/fd` count and available `/proc/PID/io` at t=0,30,60 seconds, then 10-minute idle sampling with the same workload. Preserve /proc field parsing around `(comm)` and use SC_CLK_TCK/aggregate ticks correctly. Keep TinySC off. Device MemTotal/clock/kernel/filesystems must accompany results. Use SSH port forwarding and the host HTTP library for read-only /health, /api/nowplaying and Comet date latency; do not rely on the device BusyBox wget that hung in earlier investigation.

Only in a later explicitly authorized feature/test-media session: compare 1k/10k/50k libraries, initial/incremental scans, warm/cold/random pages, covers and MP3/FLAC playback with and without concurrent work. Sample decoder/ALSA underruns, loop lag, private/PSS or RSS with shared-memory caveats, FD count, byte I/O and DB/cache growth. Repeat safe eject/forced removal/reinsert and each owner handoff. Corruption/power-loss tests use disposable media or offline copies. These tests are pending and were not executed in WP0.

Machine-readable measurements and cross-build sizes are in [WP0 measurements](STANDALONE_MUSIC_WP0_MEASUREMENTS.json). The report summarizes actual values and the evidence index identifies sources/log provenance.
