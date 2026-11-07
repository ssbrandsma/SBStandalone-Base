# Evidence index

Private current-phase root: `.local/wp4a3b/`. Earlier `.local/wp4a3` and other phase evidence remain untouched. Credentials/token contents are not included in public reports.

- `read-only-state.txt`, `installed-applications.json`, `installed-*.lua`: actual Fab4 identity/state and the three older installed application sources.
- `current-snapshot/{snapshot.json,originals.tar,files/}`: fresh read-only backup including modes, ownership and hashes; no device DB copied to flash.
- `current-preflight.json`: latest strict-host read-only idle/profile checks.
- `ctest-final-full.log`, `final-corrections-tests.log`, `admission-final-tests.log`, `recovery-wrapper-final.log`: complete host and focused follow-ups.
- `fence-final.log`, `composition-final-complete.log`, `radio-regression.log`, `spotify-final.log`: actual-source Lua and sibling regressions.
- `sanitize-focused-final.log`, `sanitize-playback-final.log`, `sanitize-admission.log`: relevant sanitizer results and their precise scope.
- `cross-*-verified.log`, `build-validation.json`, `qemu-production-verified.log`, `qemu-maintenance-final.log`: final ARM/QEMU execution and exit codes.
- `candidate*/plan.json`, generated installed/recovery/live-inode checks: unqualified preparation until a current source- and plan-bound host-qualification.json is present. Never reuse a stale receipt.

Exact preparation uses `python scripts/fab4-music-qualification.py backup --snapshot .local/wp4a3b/current-snapshot` then `prepare --snapshot ... --output ...`. This uses only designated .244 strict-known-host SSH. Installation/restart commands require the source-bound receipt, current preflight, full recovery/space validation and recorded owner idle confirmation. No deployment/restart/playback outcome is claimed until its corresponding result/evidence file exists.

## Final evidence and exact commands

- `candidate-final/host-qualification.json`: software receipt bound to final sources and candidate plan; before-space-rebase receipts are preserved separately.
- `candidate-final/install-result.json`, `install-attempt.log`: 24-file verified installation; no Jive restart.
- `restart-attempt.log`: local pre-dispatch free-space refusal only; no native attempt/marker created at that point.
- `space-rebase-verified.log`: default installation and calculated post-install reservation tests.
- `coordinator-attempt.log`, `candidate-final/hardware-attempts.json`: exactly one native REFUSED attempt, PID 26832/start 21598315.
- `refused-readonly-preflight.txt`, `refused-live-receipt.txt`, `refused-running-state.txt`, `refused-diagnosis.json`: read-only failure localization, mapped executable inode and ENOENT; no stop/retry commands.
- `file_only_rollback.py`, `file-rollback.log`, `candidate-final/file-rollback-result.json`: checked file-only rollback using a verified RAM native helper; no service restart or TinySC release.
- `library-preflight.json`, `final-browse-protocol.json`, `final-verification.json`, `final-system.txt`, `final-performance.json`: real unchanged library, current browse data, TinySC/idle state, PID/RSS/FD/USB observations.

Exact authorized deployment invocations were `python scripts/fab4-music-qualification.py install --snapshot .local/wp4a3b/current-snapshot --output .local/wp4a3b/candidate-final` and the corresponding `restart` command. Password/idle confirmation were supplied only through temporary environment variables and are omitted here. Read-only diagnosis did not invoke coordinate/launch/stop. The rollback script invokes only verified copy/remove/check operations on the fixed manifest plus removal of its own RAM marker/helper; its exact source and result are retained privately.

All payload entries were restored to their Before values (or absence), with no production Music activation. The table below records original/restored and candidate SHA-256 values; the full source/metadata manifest is in candidate-final/plan.json.

| Target | Before / restored SHA-256 | Candidate SHA-256 |
| --- | --- | --- |
| `/usr/share/jive/applets/StandaloneBase/MusicProduction.lua` | `absent` | `bf14f9ea4e938804c0946693dbf75b7a6854f93d3a8d8b51b876c5fdc52a4da8` |
| `/usr/share/jive/applets/StandaloneBase/MusicIdleFence.lua` | `absent` | `d48795e4156e2bf7d8793b6055b522cdc94ad064cc2cdc0f2bc4e9bf76b40f93` |
| `/usr/share/jive/applets/StandaloneBase/MusicOwnership.lua` | `absent` | `4461a3c897071cd8aa7a82d7918bcb4e8c08558a62aece7ba67ba190f3d3c22c` |
| `/usr/share/jive/applets/StandaloneBase/NativeMusicOwnership.lua` | `absent` | `722a8b5b14080d230379dbcab1e6dfde55434198e20878342dbfd4072d1fcb8f` |
| `/usr/share/jive/applets/StandaloneBase/OwnershipBinding.lua` | `absent` | `72f2436556feea4d274981d3b5afcdaafbe0715f1e7b8012984596b41cf8749d` |
| `/usr/share/jive/applets/StandaloneBase/OwnershipClient.lua` | `absent` | `f3a55b9b872b8efcecb7f1a300f3505b326b8001af772707b8728adbe639af59` |
| `/usr/share/jive/applets/StandaloneBase/OwnershipDecoder.lua` | `absent` | `952990de08e9840051bba54a2fd8aec9827d7feab07e09681cad842322cde8d9` |
| `/usr/share/jive/applets/StandaloneBase/OwnershipHooks.lua` | `absent` | `f0e7c0cf98ed608ed582d40328d5847adcdc17353dc6510f4cebe5aaed6c4f47` |
| `/usr/share/jive/applets/StandaloneBase/OwnershipHttp.lua` | `absent` | `8686abc475237a4b3e509a3389361cfdaba1f094cddad8fb86cc100138da7c30` |
| `/usr/share/jive/applets/StandaloneBase/OwnershipRuntime.lua` | `absent` | `a4040ea3f318f2f78c2a8d6ff6b644440e10f46241fb72fb1ea08f318cfd2ffb` |
| `/usr/share/jive/applets/StandaloneBase/LifecycleHeartbeat.lua` | `5e9a543a895d07d28dc82399ce4b2a0c59c6e7ec7c4dbc061764ea9442711159` | `5e9a543a895d07d28dc82399ce4b2a0c59c6e7ec7c4dbc061764ea9442711159` |
| `/usr/share/jive/applets/StandaloneBase/StandaloneBaseApplet.lua` | `a5f52fbf2526d49a271ace8d37f779c6f31ebfa357b56bfdd250fb6e961e2330` | `4dfc92d520b81de32b50ea738100bcc325f7592326ed7eca2d7b82b8ebe0511d` |
| `/usr/share/jive/applets/StandaloneBase/loadPriority.lua` | `absent` | `8911d0bcc686d859df79fd7f340cbd703fdffb2d94c993aaca64f226ab0f4e2d` |
| `/usr/share/jive/applets/StandaloneBase/sbbase-music` | `682cff02825f5f9b6e56f9787eea4931cc8d82a77bb7a488dfcbd1bca03ad90c` | `89c5324bd43ac0db23a3338dc55ae279cf1da34e87f208f54ec81231e83e4a3c` |
| `/usr/share/jive/applets/StandaloneBase/sbwebserver-music` | `absent` | `05860854c81681cebc41e60b11e66f758c73dd2263bef6084bee9e99c6dff2f7` |
| `/usr/share/jive/applets/StandaloneBase/sbmusic-session` | `absent` | `61141bc2d5c569b854fb763ee48a3cb182cf40b0063ad8a5ede34ccbedcba0a3` |
| `/usr/share/jive/applets/StandaloneBase/sbmusic-metadata-helper` | `dc2512300fe2c39c6f22e2b52aa875abd8d0263013737ce48787d58c59f37a54` | `dc2512300fe2c39c6f22e2b52aa875abd8d0263013737ce48787d58c59f37a54` |
| `/usr/share/jive/applets/StandaloneBase/sb-maintenance-helper` | `absent` | `ff7f7bff09b62043f020a942c29ac023beb5a3331e2b326c1bf72206cdb1d68f` |
| `/usr/share/jive/applets/StandaloneBase/restart-actions.sh` | `absent` | `615cd2f0cde8fa4804c962ac79f90a1c054a9233d4e7cc12a3b76c11d481e23f` |
| `/etc/init.d/squeezeplay` | `8c412da36a2b0404a923fd70e6413eda4bced055fdeea444bb24c4a64dd9694c` | `cfaee0ebc2c40daba44f316cbd1789dd3a34849ef7f7ba1c352a9db97c0f3d45` |
| `/usr/share/jive/applets/StandaloneBase/sbbase` | `492f3fd3dc93c33746e32c792ddda0f00ad39aa234a97f51f23ab1cdf6732e41` | `492f3fd3dc93c33746e32c792ddda0f00ad39aa234a97f51f23ab1cdf6732e41` |
| `/usr/share/jive/applets/StandaloneRadio/StreamPlayer.lua` | `054b8ee6e75b53b8fa71949e2adb8d31d3ce9278359f952e7c177eaa923201b8` | `92668af4fb80f012e8e83e8c4e734645cd5dcd3055bc0c932204aacf00b5442f` |
| `/usr/share/jive/applets/SpotifyConnect/SpotifyPlayback.lua` | `3e28389acb9c3cad8255dfb3f4b0f5cba99c56d7e5fd359851e377b1b318456d` | `091acf32aceef1363f4becc9094702b0178335c00cfa7ba305154f9b61b63731` |
| `/usr/share/jive/applets/SpotifyConnect/SpotifyService.lua` | `c6354f55cfda8518a3cd2cc8a66980418ccc953b74acd9c2d7b64be2ac43701d` | `7a1e6636e401c34d755bc67f4d617e944cd75365197f39a6044df2028eba0c34` |
