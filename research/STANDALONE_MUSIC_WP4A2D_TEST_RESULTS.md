# WP4a.2d test results and evidence

Source changes: --music-browse-only production entry gate, no playback/resource/
owner construction in this mode; same-worker SQLite integrity operation; direct
Tracks menu node and goldens; scan CPU/DB statistics and bounded browse logs.
Existing deploy atomic-copy primitive is shared for the exact native plan.

| Validation | Result |
|---|---|
| Final serial CTest | 104/104, 312.94 s |
| Exact session restore boundaries/symlink/unknown revision | 3 tests, passed |
| Shared applet/native atomic copy/restore/transport | 9 tests, passed |
| Dedicated worker integrity + transaction refusal | Passed |
| Production browse-only invalid ownership/resource/root flags | Passed |
| Native HTTP menu, all modes, paging, disabled leaves | Passed host and actual device protocol |
| Static Fab4 full native build | Passed; existing image_render snprintf warning retained |
| Final Music ARM link | Passed with -Werror, static ELF without INTERP/NEEDED |
| ARM binfmt self-test/version | Passed, Base 0.7.0 |
| Actual TinySC arbitration | Inactive path verified before DB/scan; ongoing fresh status |
| Actual USB scan | 15 files, 11 tracks, no errors, 8014 ms |
| Native/independent-copy SQLite integrity | ok / ok, schema 3 |
| Actual protocol/menu/paging/negative playback | Passed |
| Boot/process stability | Original boot/Jive/watchdog/proxy/web identities retained |
| Physical Jive navigation | Pending owner; not visually verified |

The first 103-case run failed two checks: a test incorrectly required integrity
verification during an intentional storage-EIO/unavailable-library close, and a
pre-existing coordinator diagnostic race omitted jive.exit in one init-exit case.
The integrity assertion was moved into explicit healthy/read-only/transaction
tests; close behavior is unchanged. Coordinator code was not changed or deployed;
its rerun and final suite passed. This does not qualify that coordinator on
hardware. The first physical activation refusal (tmpfs mode 1777) and guarded
setup correction are separate and fully recorded; no DB/scan existed at refusal.

Host environment: cached Docker node:22, network disabled; existing CMake output
and Linux ARM musl GCC 8.4 SDK. BUILD_SBPROXY=OFF excludes libcurl-dependent host
proxy suites; the ARM proxy build passed. Optional sibling adapter suite is OFF.
No new ASan/UBSan or Baby run is claimed for this package. Previous sanitizer/
Baby evidence remains historical; real audio/decoder tests were not run.

Exact build commands/logs:

```sh
/cmake/bin/cmake --build /out/host -j 4
/cmake/bin/ctest --test-dir /out/host --output-on-failure -j 1
ARM_TOOLCHAIN_DIR=/toolchain SBHTTPSPROXY_ROOT=/proxy sh /work/scripts/build-arm.sh fab4
ARM_TOOLCHAIN_DIR=/toolchain sh /work/scripts/build-music-session-arm.sh fab4
sh /out/build_music_final.sh
python3 /out/test_session_files.py -v
python3 /work/tests/music/test_applet_only_deployment.py -v
```

Source read-only, build output writable; no device operation occurs in Docker.
Final native link command is preserved in .local/wp4a2d/build_music_final.sh.
Host logs: .local/wp4a2b/wp4a2d-final-{build,ctest}.log. Device/build/session evidence
under .local/wp4a2d: fab4-build.log, fab4-music-final.log, install.log,
core-activation.log, browse-activation.log, first-activation-refusal.txt,
ram-mode-correction.json, browse-after-mode-fix.log, core-active.json,
browse-active.json, final-files.json, final-{health,lifecycle,nowplaying}.json,
protocol/*.json, scan-and-browse.log, library-readonly-copy.db, observation.json,
audit/{before,after}-audit.{tar,json}, session-files-tests.log, atomic-deploy-tests.log.

No new Lua exception/reset was identified in captured logs; old crashlog upload
attempts remain. Existing Radio/Spotify Jive settings and Meta files hash-match
the preflight; neither engine was started. Custom-station files outside the audit
were outside the write set; no independent pre/post content hash is claimed for
such files. No playback, Qobuz install, USB music modification or new public port.
