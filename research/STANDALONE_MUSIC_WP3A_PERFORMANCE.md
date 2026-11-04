# WP3a performance and resource observations

2026-11-02. **Host WSL/Linux observations only**, no hardware audio measurements or ARM performance extrapolation. Raw values and executable/source/log fingerprints are in [measurements](STANDALONE_MUSIC_WP3A_MEASUREMENTS.json). The test driver uses original source-derived packets and disposable loopback sockets, no network services outside the fixture process.

## Controller and memory

Host cc -Os, no LTO/sanitizers for timing; one million typed STAT parses, one million timer transitions, 100,000 in-memory mock ownership acquisitions/yields/acks. Caller monotonic timestamps are supplied directly; simulated handoff timing does not include real producer/hook release, IPC, output drain or human interaction.

| Observation | Host value |
|---|---:|
| sb_transport per player | 1368 bytes |
| Temporary typed event, including bounded HELO | 1240 bytes |
| sb_physical_owner | 120 bytes |
| sb_output fixed client state | 56 bytes |
| Maximum lazy pending output payload/client | 65,536 bytes |
| STAT parse mean | 40.473 ns/event |
| Corresponding parse throughput | 24.71 million events/s |
| Transport timer transition mean | 11.745 ns/event |
| Mock complete acquisition/handoff sequence mean | 17.026 ns |

Short synthetic means are compiler/host observations, not tail-latency bounds. They cover in-memory API work, not network/audio timing. No controller/owner object, timer, worker, listener or database work is constructed in production; disabled init returns before touching caller storage. Typed HELO parsing and the network fix are separately justified compatible Base improvements while Music stays off.

The existing 32-slot Base client array changes from 147,560 to 147,624 bytes/client: **64 bytes/client, 2,048 bytes total** fixed overhead. Its large existing request buffers remain. Output payload allocations occur only after a nonblocking immediate send cannot complete. Maximum 32 x 64 KiB = **2 MiB**, excluding allocator bookkeeping. Queues are freed on drain/close. No controller heap allocation or new descriptor exists; networking reuses existing sockets.

## Slow-client before/after

Two Comet event peers stop reading (small test socket send/receive buffers); a third reads complete chunks. A metadata request connection emits updates and measures the request-to-complete-response wall time. The host-only accept/send shim is never packaged. After each final-run update, the harness waits for the fast peer's observation, so this tests bounded backpressure and ordering rather than unlimited-producer throughput.

The before executable is compiled from the captured pre-WP3a main.c and unchanged dependent sources. Before mode stops after its first >1-second stall and skips final closed-peer assertions; it does not pretend to have completed the same 160 updates.

| Run | Updates | Update p50 ms | p95 ms | Maximum ms | Unique ordered fast-peer updates |
|---|---:|---:|---:|---:|---:|
| Before, blocking send | 1 | 4045.778 | 4045.778 | 4045.778 | 1 |
| After, real partial progress/backpressure | 160 | 0.476 | 0.824 | 2.852 | 160 |
| After, <=37 bytes/send, every third EAGAIN | 160 | 0.392 | 0.655 | 0.818 | 160 |

Both final runs close the two saturated clients observably, while preserving every fast-client update in order and complete JSON/HTTP close-after-drain responses. The maximum before stall is approximately two sequential two-second waits. This small controlled sample does not establish production p99 latency, fairness under every input mix, or old-device scheduling. The 16 KiB/four-send flush limit is an implementation bound; malloc, syscalls, OS scheduling and other synchronous Base operations still affect end-to-end time.

## Idle, descriptors and repeated cleanup

Normal before/after idle samples each observed **zero CPU ticks over 300 ms**. This is a coarse short sample, not proof of zero CPU use. No new controller idle work exists. Existing Base heartbeats/alarm scheduling remain.

Normal after RSS samples: 6400 KiB before load, 6528 KiB after. The retained 128 KiB increase includes libc arena/page residency; it is not evidence of a live 256 KiB queue or a proof of arbitrary-duration heap stability. ASan/leak checks report no live leaks. Unit tests create/close 10,000 socketpairs, exercise bounded queued allocation/drain/clear/error paths and verify exact output; the live server additionally rejects 100 malformed reconnects and finishes with baseline FD count. No exhaustive heap profiler run is claimed.

FD samples are 6 baseline, 10 with the persistent peers attached, 6 after cleanup. Sampling can miss temporary request sockets: network structural maximum is three listeners plus 32 accepted sockets, with stdio and existing temporary file operations separate. Controller/ownership APIs themselves open **zero** descriptors.

Overflow is fail-close before accepting another oversized backlog. A 15-second contiguous-backlog deadline is tested with a deterministic clock; real enforcement happens on poll-loop servicing (existing maximum poll sleep one second), not a separate real-time timer. Scope revocation of pending/partly sent frames requires connection close. Tests do not call that successful frame delivery.

## Target and qualification boundary

Both static ARMv5 Baby and ARMv6 Fab4 builds and user-mode QEMU functional/wire tests pass; instruction attributes and absence of dynamic dependencies are checked. QEMU uses the host kernel. Existing image_render/nanosvg warnings remain; new transport sources build without warnings. Normal packaging still contains the same six executables, with no helper/runtime parser dependency. sbbase gains compatible HELO validation/output handling; inactive controller functions are garbage-collected in the normal ARM link.

No measured Baby/Fab4 CPU/RAM/audio-underrun result, real adapter handoff latency, kernel socket-limit behavior, analogue/SPDIF routing, standby/alarm sound, or network-interruption audio result is claimed. Existing storage fixtures use targeted host fixture-filesystem flushing because unrelated WSL 9p global sync is stalled; this does not qualify removable-media durability. Full host proxy coverage remains unavailable due missing CURL_LIBRARY/CURL_INCLUDE_DIR.
