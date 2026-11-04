# WP4a.2b evidence index

Date: 2026-11-04. Paths below are private local artifacts under the repository,
not release payloads. No credentials are included in this index. Source/build
results do not imply hardware qualification.

| Artifact | Evidence |
|---|---|
| `.local/wp4a2b/configure.log`, `build.log` | Final host configuration/build |
| `.local/wp4a2b/ctest-final.log` | 102/102 serial, 227.28 s |
| `.local/wp4a2b/lifecycle-tests.log` | Earlier detailed lifecycle cases |
| `.local/wp4a2b/coordinator-tests.log` | 15 detailed native restart cases |
| `.local/wp4a2b/maintenance-tests.log`, `restart-probe-tests.log` | Primitive/probe cases |
| `.local/wp4a2b/internal-install-tests.log` | 13 native copy/hash cases |
| `.local/wp4a2b/deployment-final.log` | Final six deployment/rollback cases with sanitized native copier |
| `.local/wp4a2b/sanitize-final.log` | ASan/UBSan lifecycle, file and coordinator suites |
| `.local/wp4a2b/overlay-tests.log` | Qualified actual firmware Lua, 19 callbacks |
| `.local/wp4a2b/cross_changed.sh`, `cross-final.log` | Exact cross-build/emulation commands and output |
| `.local/wp4a2b/arm/{baby,fab4}/` | ELF reports, binary hashes and static ARM artifacts |
| `.local/wp4a2b/sanitize_final.sh` | Exact sanitizer commands |
| `.local/wp4a2b/check_candidate.sh` | Seven shell syntax checks |
| `.local/wp4a2b/host-performance.json` | Current host metrics |
| `.local/wp4a2b/hardware/snapshot/` | Sixteen originals, metadata, archive and snapshot hashes |
| `.local/wp4a2b/candidate/plan.json` | Eleven precise targets and known-good recovery sources |
| `.local/wp4a2b/candidate/host-qualification.json` | Source/plan-bound host checks and log hashes; not hardware approval |
| `.local/wp4a2b/read-only/` | Baseline boot, process, mount, watchdog and idle playback observations |
| `.local/wp4a2b/hardware/readonly-install-preflight.txt` | Earlier read-only path/storage evidence |
| `.local/wp4a2b/hardware/jive-environment.json` | Whitelisted original Jive environment |
| `.local/wp4a2b/hardware/final-readonly.txt` | Unchanged boot/Jive/watchdog, original services, disk counters |
| `.local/wp4a2b/hardware/final-integrity.json` | Original hashes/metadata, absent new paths, zero replacements/restarts/reboots |

The failed installation command returned code 2 from native `restart-preflight`:
`missing/stale Jive UI or feeder keepalive evidence`. It ran from the verified
83,020-byte RAM helper before persistent staging. The failing IPC values/GETPID
actors were not emitted, so their exact discrepancy remains unknown. The hardware
report explicitly preserves that limitation; no success result is fabricated.

Production Fab4 sizes: Base 229,880 bytes; controller 74,828; maintenance helper
83,020. The Music regression build is 1,130,440 bytes and is not a deployment
candidate. Both architecture outputs passed ELF checks without INTERP/NEEDED.

Development Linux Lupa 2.8 wheel SHA-256:
`9f6f41c91366e7d0d474f87d81c1274af861f40812bf729c9f97ab4c8f3c7ac8`.

Pinned stock source hashes are enforced by the firmware patch generator.
Original snapshot archive/file hashes and candidate hashes are recorded in their
manifests rather than duplicated here. A changed source or candidate invalidates
the host receipt. There is no hardware success receipt and no restart/reboot
attempt journal because neither operation was dispatched.

No factory/bootloader changes, Music activation, intentional TinySC startup,
new application installation, playback, scan or USB writes were performed on
the device. The temporary RAM bootstrap was the only deployed artifact and was
removed after refusal. Do not distribute this candidate as hardware qualified.
