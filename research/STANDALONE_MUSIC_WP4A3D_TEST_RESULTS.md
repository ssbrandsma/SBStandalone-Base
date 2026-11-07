# Validation status

No production code changed in WP4a.3d. No new recovery transaction exists to test.
The required recovery failure matrix, source-bound baseline artifact builds,
ASan/UBSan, Baby/Fab4 cross-builds and QEMU recovery checks are **not completed**.
Prior WP4a.3c results remain historical evidence, not WP4a.3d acceptance results.

The existing relevant host CTest subset was invoked with:

```sh
/mnt/c/Projects/SBHttpsProxy/build/toolchains/cmake-3.31.8-linux-x86_64/bin/ctest \
  --test-dir /tmp/sb-menu-fix --output-on-failure \
  -R '^(protocol|nowplaying_unit|alarms_unit|sleep_unit|comet_connections|comet_menustatus|comet_server_switch|music_contracts|music_integrity_worker|music_library_storage|music_browse_.*|maintenance_.*|tinysc_lifecycle)$'
```

Actual result: 22/27 groups passed as ordinary WSL user `sjoerd`; five maintenance
groups refused filesystem operations because their global `/proc` safety checks
could not inspect other users' processes. Raw output is
`.local/wp4a3d/regression.txt`. Running those five under WSL root, matching the
maintenance helper's intended privileged execution, passed 5/5 in 11.72 seconds:

```sh
wsl --user root --exec /mnt/c/Projects/SBHttpsProxy/build/toolchains/cmake-3.31.8-linux-x86_64/bin/ctest \
  --test-dir /tmp/sb-menu-fix --output-on-failure \
  -R '^maintenance_(executable_identity|internal_files|deployment_rollback|music_deployment_rollback|music_recovery)$'
```

Output is `.local/wp4a3d/regression-root.txt`. The original 27-group run took
107.84 seconds. These are two runs, not a single 27/27 result. No production guard
or test was relaxed. The existing host build was not rebuilt in this phase;
there were no production-source edits made by this phase. Passing these existing
regressions does not qualify a new recovery mechanism or a new artifact.

Fresh target observations passed firmware/board validation, health/TinySC/idle
checks, original-protocol five-category browse counts, non-playable track actions,
host-copy DB integrity and exact database preservation, stable boot and unchanged
USB write counters. They did not exercise replacement, rollback or playback.

Diagnostic-script corrections: SQLite BLOB JSON encoding and old-kernel lack of
`/proc/PID/comm` were corrected locally before the final successful observation.
Those diagnostic runs are not hardware transition attempts.
