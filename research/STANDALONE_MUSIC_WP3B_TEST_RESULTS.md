# WP3b validation results

Executed 2026-11-02/03 on the Windows checkout through WSL Linux. All media/database fixtures were generated locally in disposable host directories; no live downloads are needed by tests. No device or production service was contacted.

## Regression and new tests

Baseline: **76/76**,175.68s (`.local/wp3b/baseline-tests.log`). Final: **82/82**,192.42s (`.local/wp3b/final-tests.log`). A final focused repeat also passed6/6 browse tests in18.41s after adding the metadata coexistence assertion. Sequential CTest avoids collisions on existing Base ports9000/3483. This includes prior WP0, WP1, WP2, WP6a, WP3a and existing control/menu/Now Playing/firmware/alarm/sleep tests available in this configuration.

| New test | Coverage |
| --- | --- |
| music_browse_native | all modes, strict parsing, four-page aggregation, eight-slot saturation, deadlines/cancel/reuse, stale revisions, media generations, same-broker transactions and actual WP2 concurrent scanner |
| music_browse_http | original envelope/IDs, golden menu/actions, counts vs SQLite, all pages, filters, Unicode/escaping, metadata leaves, info, mixed legacy command order, concurrent clients, subscriptions/offline/reconnect, existing player-status metadata during browse, no strm-s |
| music_browse_partial | same HTTP scenario with37-byte writes/EAGAIN injection; complete88621-byte response |
| music_browse_empty | zero and10-track libraries, all modes/counts |
| music_browse_faults | held SQLite exclusive lock, four pending requests/fifth503, disconnected client/replacement, deadline, nonblocking health, pending media removal, invalidUTF8 |
| music_browse_disabled | normal Base, inactive test Base and Baby no-storage allocate no library/worker and retain only existing menus |

Deterministic corpus: duplicate album titles with distinct keys, multiple discs, compilations, multiple contributors, nested folders, Unicode/quotes, long strings, stable generated IDs. Goldens are explicitly source-derived (`tests/music/fixtures/browse.json`), with pinned source provenance, not claimed traffic captures. Every fixture queries exact counts/IDs/filter results against SQLite and checks absence of Play/Add/artwork. Existing metadata-only producer lease during a subscribed browse refresh proves both events coexist without actual audio playback. It does not qualify physical Spotify/Qobuz applications on hardware.

## Sanitizers

ASan+UBSan with leak detection and halt-on-error passed native browse plus actual scanner, and long/partial-write HTTP runs. New async/library/frontend/main/JSON/control code is instrumented. Existing SQLite, scanner/storage/metadata archives and bounded metadata helper are not fully instrumented by this run; historical WP2 coverage remains separate. Native concurrent run:1000 indexed tracks,2110 queries,0 retryable results,max29ms query,6955ms scan; ASan RSS146268KiB is instrumentation overhead, not a device budget. HTTP max58.646ms and88621 bytes. Logs: `.local/wp3b/sanitizers-native.log`, `sanitizers-http.log`.

## ARM and QEMU

`scripts/build-browse-arm.sh all` passes Baby `-march=armv5te -mtune=arm926ej-s` and Fab4 `-march=armv6 -mtune=arm1136jf-s`, soft-float, static EABI5. Both test helper and test Base have no INTERP/NEEDED; manifests record compiler, flags and SHA-256. Normal `scripts/build-arm.sh all` also passes both platforms and existing product binaries. Existing image-render/nanosvg warnings remain; new browse modules compile warning-free.

QEMU user-mode `arm926` and `arm1136` runs pass native1000-track contracts, empty/10-track HTTP, and3000-track long-string HTTP including subscriptions/reconnect/media removal and the existing metadata status event. Largest response88621 bytes; maximum observed mixed-request latency129.269ms Baby /101.678ms Fab4. These are emulator observations, not old target-kernel, hardware memory, UI or storage tests. Logs `.local/wp3b/qemu.log`; wrapper scripts preserve the actual ARM executable, not a host replacement.

## Reproduction and qualification limits

```sh
cmake -S . -B build-dual-tests -DBUILD_SBPROXY=OFF -DBUILD_TESTING=ON
cmake --build build-dual-tests -j4
ctest --test-dir build-dual-tests --output-on-failure
sh scripts/build-browse-arm.sh all
sh scripts/build-arm.sh all
```

Use a proper Linux host filesystem for storage fixtures. This WSL run uses the pre-existing private host sync wrapper (`.local/wp3a/host-fixture-bin/sync`): per-filesystem sync on the disposable SB_STORAGE_ROOT avoids an unrelated global9p sync stall. No production sync script is changed. Test harness scripts in `.local/wp3b/run_*` preserve exact commands.

Host libcurl development files remain missing: a fresh configure probe fails at CURL_LIBRARY/CURL_INCLUDE_DIR (`proxy-probe.log`). Thus these82 tests do not represent excluded proxy-specific tests. Static ARM proxy binaries do build with the existing SDK. No release package or deployment was produced. Product gate stays OFF; installed Jive navigation, localization, arbitrary LMS extensions and device-level Radio/Spotify/Qobuz behavior remain unverified.
