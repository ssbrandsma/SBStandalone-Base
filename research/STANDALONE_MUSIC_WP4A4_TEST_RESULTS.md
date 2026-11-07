# WP4a.4 — Test results

Host CMake build passes. Both Baby ARMv5TE/Fab4 ARMv6 static soft-float builds
pass; eight production ELF hashes/architectures are recorded in
`.local/wp4a4/artifacts.json`. No PT_INTERP/NEEDED. Existing image-render snprintf
truncation warning remains. Source freeze covers156 native/build/test inputs.

Broad selected regression: 67 pass, one Lua skip and five permission-sensitive
maintenance refusals under the ordinary WSL user. All five pass when rerun with
the root rights their global `/proc` inspection needs (`regression-root-rerun.txt`).
The earlier root-qualified selection passed72 with one skip. The skipped Lua
idle fence separately passed under Windows/Lupa using the actual Radio and
Spotify repositories. Full proxy build/tests remain excluded for missing host
libcurl development files. No claim of every repository test or live audio.

New tests exercise create/open/no-scan, stable IDs, unchanged no publication,
rename/deletion/empty-folder pruning, backup/restore, lock conflicts, HUP detach,
TERM/cancel marker, malformed metadata preservation, interrupted pending,
corruption/recovery preserving bytes, foreign SQL media, symlink/unknown files,
asynchronous refresh, offline/reinsertion and safe launcher options. Existing
WP1/WP2/WP6a cover migration, transaction failures and stale generations.

## QEMU

| Case | Pass | Seconds |
|---|---|---:|
| baby / lifecycle | True | 14.82 |
| baby / snapshot-view | True | 4.06 |
| fab4 / lifecycle | True | 14.01 |
| fab4 / snapshot-view | True | 3.79 |
| shared / test_scanner_qemu.py | True | 60.45 |
| shared / test_storage_qemu.py | True | 1.82 |
| shared / test_library_qemu.py | True | 38.19 |
| shared / production-baby-mutation-refused | True | 0.00 |

Test-only fixture executables never deployed. Production Baby mutation refuses.

## ASan/UBSan

| Group | Exit | Seconds |
|---|---:|---:|
| lifecycle | 0 | 5.02 |
| snapshot-view | 0 | 1.57 |
| scanner | 0 | 9.75 |
| library-storage | 0 | 49.50 |
| library-large | 0 | 472.61 |

Coordinator/database executables are instrumented. The ordinary constrained
metadata child is used because ASan virtual reservations exceed its RLIMIT_AS;
parser limits were not weakened. This is not instrumented-child coverage.

## Hardware

Native production read-only reopen of the preserved11-track baseline passes.
The first full USB attempt was cooperatively cancelled by the observer; one
owner-authorized retry traversed the root but failed on a non-recognized audio
signature (first64KiB zero). Neither published a database. Complete persistent
hardware reopen/unchanged/My Music validation
remains open. See HARDWARE for exact evidence and observation-path diagnosis.
No physical power loss/unplug, native audio or active runtime replacement.

## Reproduction and artifact receipts

```sh
cmake -S . -B build-wp4a4 -DBUILD_SBPROXY=OFF -DSB_BUILD_MUSIC_SESSION=ON
cmake --build build-wp4a4 -j4
ctest --test-dir build-wp4a4 --output-on-failure -R 'music_|maintenance_|tinysc_'
sh scripts/build-music-session-arm.sh baby
sh scripts/build-music-session-arm.sh fab4
```

Maintenance file/executable tests need root on Linux for global process identity
inspection. ARM fixtures/QEMU runners use the existing musl SDK and qemu-arm.
The host CMake/Lua/proxy prerequisites and skipped tests above still apply.
All native/build/test inputs were checked unchanged before physical dispatch.

| Artifact | SHA-256 |
|---|---|
| `build-arm/baby/music/sbmusic-library` | `9aa24e9b30d94e300fdb86a88fd1f858600750cb3e4563b7f37764c14399dddd` |
| `build-arm/baby/music/sbmusic-metadata-helper` | `793f26f0ac5e49be38d435a09bdd715193b8f04a9928861ab41c04d6c6528490` |
| `build-arm/baby/music-session/sbbase_music` | `e397f04c8ac2faef64e16e5dd695ef5eda38e71bc98054aeec75f855ec7827c6` |
| `build-arm/baby/music-session/sbmusic-session` | `868ecb787c903f7058d34deb815857179f604bc6274c5d4eb91f0647b5469985` |
| `build-arm/fab4/music/sbmusic-library` | `ea1f65199a06d6aa5c787a1811ba495adbfe8970244e54fbd2ad9f51e57a8dc1` |
| `build-arm/fab4/music/sbmusic-metadata-helper` | `dc2512300fe2c39c6f22e2b52aa875abd8d0263013737ce48787d58c59f37a54` |
| `build-arm/fab4/music-session/sbbase_music` | `1a149f9f1a074d8426dfa953e76912ea7cdec76ccfb441d0bb3c2bd2a87d5fe4` |
| `build-arm/fab4/music-session/sbmusic-session` | `85e4a54cf11ea19673265f7d3055cf18fb5817b015e2c188b48c10e443fbe6c7` |

Frozen-source manifest SHA-256: `eeab6b00e94d2643f8a8a420431ffc3fda6d6c0afa824645a399f6b88dd97b7a`.
