# WP6a tests, builds and resource observations

2026-11-02. Tests use controlled temporary host databases/files, synthetic event/mount tables and an actual host `/dev/shm` mount. No target device or real removable medium was accessed. Machine-readable results/hashes are in [measurements](STANDALONE_MUSIC_WP6A_MEASUREMENTS.json); source anchors/provenance are in [the evidence ledger](STANDALONE_MUSIC_WP6A_SOURCES.json).

## Host regression and focused tests

The final complete available host run passed **71/71 tests in 156.69 seconds**, including all existing WP0/WP1 regressions and two new storage/Lua tests. The first full run also passed 71/71 in 158.94 seconds. The final focused run after the Lua ingress ordering adjustment passed **2/2 in 0.43 seconds**. The harness respects transient BUSY returns during worker startup. The library's 1k/10k/50k fixtures, migrations, lock contention, corruption/newer-schema preservation, read-only behavior and real hot-journal recovery remain part of the suite. Production WP1/schema files are unchanged by WP6a.

`test_storage.c`, invoked by `test_storage.py`, covers the following requirements. Assertions remain enabled in host and ARM builds.

| Required scenario | Executable coverage |
|---|---|
| 1. No media | Empty injected mount snapshot; adapter reaches idle without periodic deadline |
| 2-4. USB, SD, two devices | Portable model source types, distinct sources and simultaneous availability |
| 5. Delayed mount | Pending mount ID=0 completed by validation; finite adapter retry deadline |
| 6. Read-only | Model and adapter report available/read-only, no probe writes |
| 7. Unsupported filesystem | exFAT refused explicitly by validation |
| 8-9. Duplicate/out-of-order | Identical insertion keeps generation; stale event/ingress sequence refused; Lua SEQNUM test |
| 10. Unexpected removal | Immediate offline generation guard; removal during a deliberately blocked worker |
| 11. Changed device on reinsertion | Same reliable source/library association and new generation; committed WP1 track retained |
| 12. Mountpoint reuse | Different mount ID/device replaces old generation and source; read-only change observed |
| 13. Duplicate UUID | Distinct serials remain separate; exact simultaneous composite clone quarantines/taints |
| 14. Missing UUID/identity | Session source changes on reinsertion; explicit registry import/restart behavior |
| 15. Symlink/traversal | Real anchored open; relative symlink to /etc/passwd refused, dot/dot-dot/absolute/empty segments rejected |
| 16. Stale directory | Ordinary temporary directory absent from authoritative mount records is refused |
| 17. Disappearance during validation | Reader loses mount record between checks; failure closes descriptor |
| 18. Failed/incomplete operations | Injected OS read error, truncated/malformed escapes/table, FIFO/special-file refusal; existing WP1 EIO/recovery regression |
| 19. Stale generation | Old removal/validation/root/DB completion rejected; late discovery cannot undo revocation |
| 20. WP1 identity | Synthetic committed track retains ID after reliable changed-node reinsertion; offline browse denied; open transaction rolled back; locked pending SQL cancellation bridge returns promptly |
| 21. Disabled feature | No model change, native worker/adapter constructor returns before resources, Lua listener not registered |
| 22. Baby no storage | Same zero-resource constructor/model path; no Fab4 dependency/menu/hardware claim |

Additional checks cover registry/slot/generation limits, exact mount read-only options/escaped spaces, mounts-only authorization refusal, nested bind mount rejection, FD_CLOEXEC, source/library association guards, repeated async stop/destroy, stale request IDs and descriptor return to baseline. A controlled fixture coalesces **200 hints into one discovery job**; three total fixture reads comprise an earlier isolated worker probe, adapter startup and this hint batch.

Positive root tests use a real kernel-mounted tmpfs read from `/proc/self/mountinfo`, with controlled synthetic byte files created under a temporary directory. They do not mistake mkdir for mount success. Adapter lifecycle integration injects a `/dev/sda1` name onto that actual mount record, explicitly to test event behavior without a host USB device; it is not a hardware/driver compatibility result. Negative fixtures exercise disappearing/reused/nested mount observations. No mount/unmount syscall or command is issued. Filesystem writes are limited to disposable host test data and synthetic SQLite libraries.

`test_storage_lua.lua` tests disabled/Baby gating, independent block notifications, basename forwarding, exact representable kernel sequence handling, stale events, state access and inactive callbacks after stop. It supplies stubs for the native bridge and listener, not a real Jive/native IPC connection. No live UI/firmware behavior is claimed.

## Builds, emulation and sanitizers

Normal Baby/Fab4 static builds and the new isolated `libsbmusic-storage.a`/`test_music_storage` builds pass. Storage archives are **32,584 / 32,424 bytes** and stripped combined storage/library test executables **774,100 / 765,908 bytes** (Baby/Fab4). All six normal binaries retain their previous sizes; five retain identical hashes. sbwebserver's embedded build date/time changes its same-size hash. Native tests execute under qemu-arm for both targets using host-kernel mounts and disposable host DBs. Existing WP0 command/contract ARM tests and WP1 DELETE/WAL 1k correctness tests also pass under QEMU. CPU/ABI inspection confirms ARM EABI static binaries, Baby v5TEJ/Fab4 v6 and no dynamic dependencies or hard-float register ABI. The SDK libc/pthread can contribute v5TEJ instructions appropriate to Baby's ARM926EJ. Emulation does not prove actual old-kernel, USB, SD, FUSE, firmware callback timing or audio behavior.

AddressSanitizer/UndefinedBehaviorSanitizer plus leak detection pass the storage harness, native model/adapter/bridge and WP1 implementation. The upstream SQLite archive itself is not sanitizer-instrumented. New isolated storage sources build without warnings. Normal whole-project ARM builds still emit pre-existing image_render/nanosvg warnings; no unrelated refactor was performed.

The full native host proxy/TLS/HTTP suite remains **blocked by missing libcurl development library/headers** (`CURL_LIBRARY`, `CURL_INCLUDE_DIR`), confirmed again in `.local/wp6a/proxy-probe.log`. The 71-test count is the available BUILD_SBPROXY=OFF suite; it does not include those unavailable tests. Independent HLS regressions and normal ARM proxy builds pass. No package release, commit, push or deployment occurred.

## Performance and resources

One final host sample from the isolated benchmark (WSL Linux, development load, not device measurements):

| Observation | Result |
|---|---|
| 100,000 coalesced model events | 8.56 ms elapsed, 8.56 ms process CPU |
| One proc discovery | 0.569 ms |
| Real tmpfs root validation | 0.207 ms |
| Maximum of 20 nonblocking submit samples | 0.030 ms |
| Whole-process CPU over 200 ms sleeping-worker interval | 0.017 ms; includes harness/timer overhead, not precise worker attribution |
| VmRSS before worker / idle worker / after discovery | 2228 / 2356 / 2740 KiB |
| Whole benchmark peak RSS | 2868 KiB |
| Model / discovery DTO bytes on host | 52,008 / 351,768 |
| FDs before / enabled worker / after destroy | 3 / 5 / 3 |

RSS deltas are observations after earlier parser/root work and allocator reuse; they include caller result buffers and do not predict target memory. The adapter owns a model and result DTO; its worker owns another DTO plus a 256 KiB stack. Table parsing temporarily allocates at most 1 MiB input and bounded mount tables; validation allocates a bounded mount table per check. These bounds, transient peak and copy costs need on-device budgeting before enabling. Up to sixteen live root descriptors would be additional caller-owned resources; discovery validates/closes roots rather than retaining them. Disabled/Baby paths allocate none of this.

No production Base loop invokes or links the module, so WP6a contributes **zero storage calls and zero added runtime binary bytes** to that loop. Nonblocking submit timing above is a host measurement of the future ingress cost, not a measured Base/device loop latency. Future enabled control-loop integration must also measure bounded DTO copies/reconciliation, storage stalls and combined SQLite/scanner peak memory. An idle storage worker waits on a condition variable and performs no periodic work on its own. Adapter deadlines require the future existing scheduler; no-media idle has no recurring deadline. Actual physical notification/removal latency is unmeasured; absent notifications are reconciled within the configured mounted-media backstop once that scheduler is integrated, subject to kernel I/O stalls.

## Reproduction and stop boundary

Use the existing host CMake tree with BUILD_SBPROXY=OFF, build, then run ctest sequentially (existing integration tests share ports). Focused commands: `ctest --test-dir build-dual-tests -R music_storage --output-on-failure`, `python3 tests/music/test_storage.py build-dual-tests/tests/test_music_storage`, `build-dual-tests/tests/test_music_storage bench`, `sh scripts/build-music-arm.sh all`, `python3 tests/music/test_storage_qemu.py`, `python3 tests/music/test_library_qemu.py`, and `sh tests/music/cross_contracts.sh`. The ARM script accepts ARM_TOOLCHAIN_DIR; raw commands/logs and exact artifact hashes are under ignored `.local/wp6a` and the checked-in measurement ledger.

Remaining qualification gates: physical Baby capabilities/memory; target mountinfo/API availability; real USB/SD event order; source identifier quality; FAT/NTFS/ext locking, encoding and timestamp cases; safe DB placement/VFS; surprise removal and power failure; actual Jive bridge/scheduler activation; legacy TinySC startup bypass and safe-eject barrier. None is silently treated as tested. WP2 and WP6b were not started.
