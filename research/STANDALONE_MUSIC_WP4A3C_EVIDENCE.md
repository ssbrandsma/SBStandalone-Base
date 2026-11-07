# Evidence and reproducibility

All new evidence is under `.local/wp4a3c/`; previous phase evidence is preserved. No credentials are written into these reports.

- `hardware.json`, `identity-probes.json`: exact errno/stage/level, PID/start/parent, SHA and latency observations. `positive_identity:false`.
- `base-proc.txt`, `proxy-proc.txt`, `web-proc.txt`: proc status/stat/cmdline/maps/mountinfo/fd/exe observations; role installed-stat files record inode and unavailable stat utility.
- `upper-branch-readonly.txt`: current upper directory and whiteout listing, without historical claims.
- `initial-system.txt`, `final-system.txt`, `semaphores.json`, `browse.json`, `helper-cleanup.json`: unchanged identities, mounts/USB counters, IPC and 11-track browse/cleanup.
- `used-on-device/sb-maintenance-helper`: exact production ELF executed on the Touch, with its original build manifest. It was checksum-verified over the strict SSH channel before chmod/exec from private RAM.
- `host-tests-final.log`, `final-host-results.json`: twelve passing CTest groups. New identity and generation fixture sources are under tests/music.
- `sanitize-tests-final.log`, `sanitize-build-final.log`: six passing focused ASan/UBSan groups on the final relevant native code.
- `build-results.json`, `arm-{baby,fab4}/{elf.txt,sha256.txt,sb-maintenance-helper}`, `qemu-production.log`: exact production cross-build commands/hashes and private-IPC QEMU results.
- `helper-stack.su`, `performance.json`: compiler static frames, real read-only hardware latencies and explicitly host-only receipt fixture measurements.
- `readiness.json`: no qualified activation candidate and no permission to resume replacement.

Exact build tool: `scripts/build-maintenance-arm.sh baby|fab4` with ARM_TOOLCHAIN_DIR set to the existing Buildroot SDK and SB_MAINTENANCE_OUTPUT pointing to the new phase directory. Host CMake build /tmp/sb-menu-fix targets maintenance_helper_fixture, sb_maintenance_helper and restart_probe_fixture; CTest selection maintenance|tinysc. Sanitizer selection and complete commands are retained in validate_sanitize_final.py. Production QEMU uses unshare -i and the existing read-only arm_ipc_test.py.

The private read_only_hardware.py script allows only identity/inspect/pin/watchdog-preflight/stack/sem helper commands; it contains no install, stop, launch, restart, scan or decoder operation. It uses read-only HTTP browse/status requests. No deployment command was executed or issued for continuation. Standard SSH authentication was supplied through a temporary environment variable only.

## Final source hashes

- `native/sbmaintenance/executable_identity.inc`: `e0c8a06226b2c8a1524aa7488c8def6866f4862696a4cdc3aae44cdf15b28fa5`
- `native/sbmaintenance/helper.c`: `5289247c30a332efeafcf16145f221a30d963d8c82e652d494aa703823118204`
- `native/sbmaintenance/internal_files.inc`: `7d6f9302b5afd0b3967e0cd32ecf6e7dc1e7fcad3adfdfb2d748354fd579b284`
- `native/sbmaintenance/coordinator.inc`: `8877c80feca2397d4baf83573af66af001d94a018504ae66876e31bd80796dc4`
- `scripts/fab4-lifecycle.py`: `52d52e8d6b79f4f5b8739a6c8cac168d6639ce7601ec3dfe8afc9651fd134a64`
- `scripts/music_generation.py`: `66d1cb0ce237b0ada7d94c755ddea0a83233c1468b1b2d646eae74115050e5ba`
- `tests/music/test_executable_identity.py`: `b8bb378cb47795848c535ae8d4119ea4c4ab48191bec74e1a88a1e636d44b5e1`
- `tests/music/test_music_generation.py`: `3a25596274d520b14bb6a58553074e5d9fed2c45e44f82e25a016b9c39ed24b5`
- `tests/music/test_stack_topology.py`: `73eacc8cbdfd93acd3503216beb1b642a48428c9512f74a928cfddebfd8c94f3`
- `tests/CMakeLists.txt`: `488208bae4b0268079d94831af1c0c49119034caa22126d8099852cc3d95f236`

## Production artifacts

- baby: 91212 bytes; SHA-256 `dfcc490fc16aae595f6dfe4a1a7a701c3a42af6d7b3265c8edc750fdc1afffa4`.
- fab4: 91212 bytes; SHA-256 `dfe0132317b0e1392e06a7f1a79daca047633eef4a129d84ec4d384570202c21`.
- Device diagnostic build: `4db74630945bf6d1e467567f2a6e5db742a2d0c7e036071bfa4606aff53ba343` (preserved separately from final local builds). Identity algorithm is unchanged in the final build; later guard/topology additions were host-tested and were not executed as device transitions.
