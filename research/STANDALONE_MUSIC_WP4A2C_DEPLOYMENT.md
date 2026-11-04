# WP4a.2c deployment

**Executed successfully on disk, without reload or restart.** Password supplied
through SB_DEVICE_PASSWORD, not command arguments. Exact command:

```text
python scripts/deploy_dist.py .local/wp4a2c/dist/StandaloneBase-0.7.0-fab4.zip root@192.168.1.244 --applet-only --no-restart --backup-dir .local/wp4a2c/applet-backup
```

Normal workflow: deploy_dist.cmd -> scripts/deploy_dist.py, flat package validation,
trusted Paramiko SSH, SCP to generated /tmp/standalonebase-deploy.*, extraction,
copy to .new and atomic rename under merged /usr/share/jive/applets/StandaloneBase.
No direct /mnt/storage/usr copying. Native binaries do not build automatically
inside the deploy tool. Existing normal full-package behavior is retained.

New explicit applet-only mode requires --no-restart, trusted SSH, verified Fab4,
idle playback and a local backup path. It rejects selected symlinks/redirected
parents, backs up originals with verified bytes/metadata and stages only twenty
whitelisted Lua/data files. No experimental ownership/Music Lua, controller ELF,
coordinator, native executable, firmware patch or persistent config is uploaded.
Changed files only are copied; read-back hashes/permissions are verified. No
applet unload/reload call exists in this path. Mid-copy errors restore originals
from the verified archive; unknown revisions are refused. Staging is removed.

Exact differences, all root:root 0644:

| File | Before SHA-256 | After SHA-256 |
|---|---|---|
| `ArtworkUrlNormalizer.lua` | `52e9e5b7413d83b6b5d1c5610287f008283c07492347cea60fa33d4416d49567` | `2c73792430affe3b59966a433d6c8992df0de9eed8314d7f18caef2483e86f97` |
| `LifecycleHeartbeat.lua` | `ABSENT` | `5e9a543a895d07d28dc82399ce4b2a0c59c6e7ec7c4dbc061764ea9442711159` |
| `StandaloneBaseApplet.lua` | `443f2775e854d73753c3131193d3f342fd0a24811a6cc75c26e361b0b8c622c8` | `a5f52fbf2526d49a271ace8d37f779c6f31ebfa357b56bfdd250fb6e961e2330` |

The other seventeen selected files matched and were not rewritten. Full audit
comparison confirms only those three differences, with the other 67 original
files' hashes, owner/mode and mtime unchanged. Full before/after inventories:
.local/wp4a2c/touch/{before,after}-audit.json and matching tar archives. The
deployment receipt is .local/wp4a2c/applet-backup/deployment.json.

Source/package version is 0.7.0. Native /health remains 0.6.7, as intended for
this scope. Persistent catalog version and installer settings were not rewritten
to claim a native 0.7.0 upgrade. Running Jive still uses its existing applet.

## Local commands/results

Existing Docker host build/test commands, source read-only and build output RW:

```sh
/cmake/bin/cmake --build /out/host -j 4
/cmake/bin/ctest --test-dir /out/host --output-on-failure -j 1
ARM_TOOLCHAIN_DIR=/toolchain SBHTTPSPROXY_ROOT=/proxy sh /work/scripts/build-arm.sh fab4
sh /work/packaging/build-package.sh fab4
```

Host CTest: 102/102, 214.93 s. The current build has no native source changes
after this run. Lua 5.1 syntax: 27/27. Fab4 static ARM EABI/soft-float checks passed
for Base/proxy/webserver/storage helpers/renderer. Existing build script also
compiles isolated Music libraries; none was installed or run on hardware.
Package output: build-package/StandaloneBase-0.7.0-fab4.zip, copied unchanged into
the private dist path used above. Only selected applet data from it was uploaded.

Eight new tests exercise real shell copying in a temporary host tree, explicit
restore preserving original mtime, unknown revision/corrupt backup refusal,
symlink refusal, post-rename failure recovery, temporary cleanup and binary SSH
response handling. Offline tests stub only the unavailable SSH import; no device
or SSH connection is simulated as hardware success. Registered CTest 1/1 passed
2.67 s; targeted lifecycle/heartbeat 2/2 passed 7.26 s. Previously configured
102 tests plus the new case now register 103; the entire 103-case suite was not
rerun. No claim that it was. Host libcurl proxy tests and optional sibling adapter
suite are excluded by existing configuration; static ARM proxy build passed.

Evidence: .local/wp4a2c/{deploy.log,applet-deploy-tests.log,lua-syntax.json,
fab4-build.log,fab4-package.log}; reused host output .local/wp4a2b/wp4a2c-*.log.
No large benchmarks, new release publication, commit, push or tag were performed.
