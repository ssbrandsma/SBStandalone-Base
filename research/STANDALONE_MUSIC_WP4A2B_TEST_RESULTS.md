# WP4a.2b tests, builds and measurements

Date: 2026-11-04. Host success does not qualify Fab4 hardware behavior.

## Environment

Windows host, Docker cached `node:22`, Linux x86_64, network disabled for tests.
CMake 3.31.8; ARM Buildroot GCC 8.4 musl SDK stable-2020.02-2. WSL currently fails
with E_UNEXPECTED; Docker was used without restarting Windows/WSL services.
Lupa 2.8 development wheel provides Lua 5.1 (wheel SHA in evidence). No Lupa is
added to the target. Tests use temporary files, synthetic TinySC/Jive processes,
a private IPC namespace and a regular fixture watchdog file, never hardware.

## Final results

| Check | Result |
|---|---|
| Configured CMake build, Music default OFF | Passed |
| Full serial CTest | 102/102, 227.28 s |
| TinySC lifecycle | 18 tests; final CTest 5.98 s |
| Maintenance primitives | 20 tests |
| Watchdog read-only probe | 9 tests |
| Restart coordinator | 15 tests; final CTest 15.75 s |
| Internal native file copy/hash | 13 tests |
| Deployment/rollback planning | 6 tests; also rerun with sanitized copier |
| Qualified stock SqueezeCenter Lua | 19 guarded callbacks, OFF startup retained |
| UI heartbeat Lua | Passed in CTest |
| Candidate/recovery Lua syntax | Four generated Lua files passed |
| Candidate/recovery shell syntax | Seven files passed |
| ASan + UBSan | Lifecycle 18, copy 13, coordinator 15 passed; final rollback 6 passed |
| Baby ARMv5 / Fab4 ARMv6 | Base, Music regression build, controller, maintenance passed |
| ARM emulation | Base self-tests, empty TinySC detection, API versions, read-only 20-slot IPC/profile refusal passed for both |

Full CTest includes prior WP0/WP1/WP6a and existing synthetic scanner, queue,
ownership, browsing, playback and HTTP regression tests. These were host tests;
no physical audio playback, scan or Music-runtime startup occurred on the Touch.

Lifecycle cases cover empty/active services, stop timeout, stale/mismatched PIDs,
late launches, orphan children and persisted identity evidence, corrupt journal,
crash/restart quarantine, duplicate ownership, unexpected appearance and admission
revocation, disabling/release checks, hung stop requests and FD 4096 inheritance.
Stock Lua fixtures cover repeated startup/USB/reinsertion/late timer/wipe/prefs
callbacks and guarded service actions.

Coordinator tests cover normal detached restart, SIGHUP/SSH loss, concurrent
requests, stale watched identity, feeder failure, bounded preflight timeout, old
Jive exit with orphan decoder, initialization exit code 23, missing new identity,
wrong nonce, frozen UI, original UI restoration and failed recovery with explicit
maintenance supervision. They do not reproduce every 2014 kernel IPC writer.

Deployment tests restore original bytes after all 12 boundaries of an eleven-file
deployment, and after post-rename failure at each file. Unknown owner changes
are refused. Attempt journals forbid automatic repeated restart/reboot; reboot
requires completed restart and postchecks. Native copy tests separately check
mode/mtime, bounds, symlinks, forbidden targets, expected hash/absence and known
SHA-256 vectors. Full operational rollback on a running Touch remains untested.

The initial parallel CTest run had shared-port collisions. Serial rerun passed;
an early Lua wrapper argument-index problem was corrected before final results.
No unexplained failure is hidden by these final host results. Hardware preflight
failure remains a separate unresolved blocker.

## Host performance

Measured with current CMake test fixtures on Linux x86_64, not sanitizers:

| Measurement | Observation |
|---|---|
| Empty detector, 20 subprocess runs | Median 4.83 ms, maximum 5.82 ms |
| Worker to verified absence, no TinySC | 105 ms |
| Worker to verified absence, three synthetic services | 206 ms |
| Whole worker fixture process RSS/HWM | 1792 KiB |
| Threads / descriptors | 2 / 6 |
| CPU in a three-second idle window | 0.00 s empty; 0.01 s after synthetic stop |
| Cooperative worker shutdown | 3.45 ms empty; 3.25 ms after stop |

RSS is whole-process resident memory, not incremental Base overhead. VmSize
68156 KiB includes glibc virtual allocation and is not target musl memory demand.
CPU counters have host clock granularity; zero does not mean no CPU consumption.
Sampling windows are short observations, not CPU-percentage guarantees. Detection
and stop timing are synthetic, not firmware TinySC shutdown measurements.

On Fab4, only the 83,020-byte production maintenance helper ran briefly from RAM
for read-only preflight. Arbitration memory/CPU, restart duration, Jive startup,
shutdown and initialization overhead were not measured: qualification stopped
before activation. Polling design is bounded, not a claimed hardware CPU budget.

## Reproduction

Configure/build the repository normally with Music OFF, enable the existing
host regression targets and a Lua 5.1 executable, then run `ctest -j 1`.
The specific Python fixtures accept their compiled helper/fixture paths as used
in `tests/CMakeLists.txt`. `benchmark_lifecycle.py CONTROL FIXTURE` emits JSON.
Build ARM with the existing per-package scripts, including
`build-lifecycle-arm.sh` and `build-maintenance-arm.sh`, for `baby` and `fab4`.
Private exact command scripts and logs are indexed in the evidence report.
