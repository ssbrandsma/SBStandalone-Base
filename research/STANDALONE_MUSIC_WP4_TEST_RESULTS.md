# WP4 test and build results

Validation on the frozen WP4 source snapshot; no hardware audio claim is made.

Baseline after user-restored WSL:82/82 available tests,260.44s. Before that, WSL failed even /bin/true with Wsl/Service/E_UNEXPECTED; no tests were falsely counted from that failure. An intermediate90-test run had one ownership-fixture failure while the playlistcontrol count contract was being corrected; it is not the final acceptance run.

New deterministic tests cover the pure queue,64-bit range/token policy, descriptor registry/read lifecycle, paginated140-track load and oversized rollback, actual WP2 scanning plus concurrent browsing while the mock decoder remains Playing, complete original Jive Play/Add/Insert/next/previous/clear/delete/pause/resume/stop flows, cancellation under a held SQLite lock, indexed symlink escape, media removal/reinsertion, and all three external-owner mock handoffs. The opt-in Lua seam separately tests all four owners, exact64-bit string identities, proof ordering, cancellation and timeout.

All generated media/DBs are disposable host fixtures. HTTP end-to-end tests verify original MP3/FLAC bytes served by the actual existing webserver and send mocked RESP/STAT; this is not observed audio output. Existing WP3a tests retain codec/error/timeout/stale-event and all directed arbitration transitions. Existing WP3b tests retain broker/scanner pagination/subscription regressions.

Missing host libcurl development files are still confirmed by a fresh CMake configure: CURL_LIBRARY and CURL_INCLUDE_DIR unavailable. Proxy-specific host tests excluded by that configuration must not be counted. Existing static ARM proxy build coverage is separate. Existing image-render and sanitizer-configuration warnings in unrelated firmware/backup/system-info sources are retained, not hidden or refactored.

Use a proper Linux fixture filesystem. This WSL environment uses the existing private per-fixture sync wrapper from WP3a to avoid an unrelated global9p sync stall; production sync/mount code is unchanged. Network tests run sequentially because Base fixtures share9000/3483. Cross-builds/compiles may run independently, but final performance measurements are taken separately.

## Final validation matrix

| Validation | Result | Scope |
| --- | --- | --- |
| Host available CTest suite | 91/91 pass, 194.36s | WP0/WP1/WP6a/WP2/WP3a/WP3b plus nine WP4 tests |
| Focused WP4 queue/load/policy/stream/playback | 8/8 pass | Lua ownership separately included in full suite |
| ASan + UBSan native integration | Pass | Actual scanner + shared DB queries + mocked decoder; leak detection enabled |
| ASan + UBSan HTTP integration | Pass | Actual web/Base, partial-send fault wrapper, five starts and9,560 verified encoded bytes |
| Baby ARMv5 / Fab4 ARMv6 | Seven static WP4 binaries each pass build | GCC8.4 musl, soft float, ARM926 / ARM1136 tuning; no INTERP/NEEDED |
| QEMU ARM926 / ARM1136 | Five native tests and two HTTP pipelines per CPU pass | Static native binaries; mock decoder; host scanner/helper prepare HTTP fixture DB |
| Normal dual platform builds | Pass | Existing packaging binaries, disabled Music startup unchanged |
| 1k / 10k / 50k concurrent browsing + HTTP | Pass | 120 successful browse requests each, real encoded GETs, mock Playing |

Sanitizer builds instrument new queue/load/file/playback/IPC/HTTP and shared async/library/Now Playing/controller code; existing SQLite/scanner/storage/metadata archives are not fully instrumented in this WP4 run. The metadata helper is uninstrumented for its existing process-limit contract. Native teardown is exercised; process-terminated web fixtures do not prove leak-free graceful whole-webserver shutdown. No sanitizer findings occurred in this defined coverage.

The native Lua seam tests four owners including canonical IDs above2^53 and cancellation/timeouts. Production app adapters remain absent and unavailable by default. All HTTP/ownership proof routes exist only in separately compiled disposable-fixture binaries. Prior app sources were read only; passing tests do not establish physical application compatibility.

The last source correction removed a remaining split C string emitting `can_seek:0`, because Lua zero is truthy. Focused/full tests then passed. The later benchmark-only Python fixture correction changed the initial selection page; default and benchmark flows passed, and the entire suite was repeated on that final snapshot. Earlier failed/intermediate runs are not counted as passes.

Final full-suite rerun on the frozen source: **91/91 pass, 194.36 seconds**. Frozen source hashes match; all prior research input hashes remain unchanged, documentation links resolve, JSON parses, and `git diff --check` passes. No commit, push, tag or deployment was performed.
