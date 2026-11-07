# Validation results

Host configuration: `-DBUILD_SBPROXY=OFF -DSB_BUILD_MUSIC_SESSION=ON`, GCC/Linux via WSL and CMake 3.31.8. The full root CTest run reports 111 tests: 110 passed, one optional Lupa test skipped. That fence test passed separately on Windows Lupa with the actual Radio/Spotify sources. Focused follow-up runs cover subsequent idle policy, RAM destination and recovery changes. Applet-only backup fixtures require root; their initial non-root refusal was resolved by running with the required identity, without weakening validation.

Eight Lua/native composition cases cover successful initial admission/release, delayed and missing IPC, hooks/fence/producer/decoder rejection and missing bridge. Actual firmware methods are instrumented in software only. Radio regression suite passes; Spotify 25 tests pass from its repository directory. Initial Spotify invocation from the wrong working directory failed three path-dependent fixtures; the correct invocation passes all 25.

Baby/Fab4 static ARM production and maintenance builds pass. QEMU production bootstrap/resource HTTP, 1000-track SQLite correctness, metadata/scanner lifecycle and storage checks pass on both platforms. A first SQLite FD-count failure was a QEMU first-thread internal descriptor: warming only an empty guest thread before baseline (no library access) retains strict equal-descriptor checks around every library lifecycle. Guest syscall traces showed the actual database and pipe descriptors closing.

ASan/UBSan focused ownership/playback/HTTP/maintenance/recovery checks pass. The full sanitizer all-target build is excluded because an unrelated HTTP trace fixture conflicts with optimized fortified syslog; selected relevant targets build. A sanitized metadata helper cannot run under the production parser address-space limit, so the scanner/playback integration uses the ordinary bounded native helper while the coordinator/library/queue code remains sanitized. Normal host and QEMU cover the actual helper. No production limit was relaxed.

`pkg-config --modversion libcurl` still fails: the libcurl-dependent proxy-host suite is explicitly excluded. Existing proxy binary is retained and hash-checked. None of these software results proves physical audio.

## Final follow-up and hardware distinction

The final boundary tests retain the default 4096 KiB pre-install requirement and enforce the computed 1428 KiB post-install atomic rollback reserve, rejecting 1427 KiB and zero. Generated recovery, once-only reconstruction and all file-boundary rollback checks pass. Actual production supervisors on host and both ARM emulations reap both children and close the web listener after partial startup failure.

On real Fab4, file installation and watchdog/listener/profile preflight passed. Native coordinator attempt REFUSED at live Base executable provenance: `/proc/3933/exe` could not be opened on this UnionFS/kernel after replacement. No subsequent activation test or retry was performed. Exact file-only rollback and final read-only browse/database/TinySC/watchdog/USB checks pass. The Linux host executable-replacement fixture does not reproduce this old target-filesystem behavior, so a passed host test is not hardware qualification.

The final diagnostic CometD disconnect returned an EOF before the optional body on one SSH-forwarded request; successful read-only browse responses were independently retained. The diagnostic tolerates that disconnect-only EOF; no production protocol source was changed for it.
