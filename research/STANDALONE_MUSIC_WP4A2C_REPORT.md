# WP4a.2c report

2026-11-04. **Applet deployed on disk; new Lua runtime not activated.**

The attachment named .245, which initially lacked a trusted SSH key. After the
owner independently provisioned trust, .245 identified as Radio/Baby, so nothing
was deployed there. The owner then requested verification of .244 and resumed
the assignment after it was confirmed as the development Fab4 Touch. That later
direction supersedes the attachment's earlier .244 exclusion for this deployment.

The established `scripts/deploy_dist.py` workflow was extended with a restricted
`--applet-only` mode: same trusted SSH/SCP, RAM staging, merged applet destination
and atomic copy/rename, with verified originals and explicit rollback. The old
`--no-restart` alone would still copy native binaries/config and run a firmware
patch, so it was insufficient. No alternative installer or live reload was built.

The current 0.7.0 Fab4 package was built locally. Twenty Lua/data dependencies
were selected; seventeen already matched. Three files changed: main applet,
ArtworkUrlNormalizer and the newly required LifecycleHeartbeat module. All have
verified SHA-256, UID/GID 0/0 and mode 0644. Native Base remains 0.6.7; native
binaries, web assets and all persistent settings/configuration were preserved.

**Deployed on disk, not yet activated in the running Jive instance.**
The existing tool has no supported live applet refresh. The applet's `free()` stops
native services, so it was not used. No Jive restart, reboot or native service
restart was performed. Unchanged Jive PID/start time and no reload invocation are
not evidence that new Lua initialized; runtime qualification remains incomplete.

Existing Base/proxy health and web root returned HTTP 200. CometD handshake,
serverstatus/players read requests and disconnect passed over existing SSH direct
TCP channels (no new listening port). Now Playing stayed inactive/stopped. Five
samples over 60.14 seconds showed unchanged boot/PID/start identities, stable
FD/RSS and no TinySC or audio engine startup. No new Lua/watchdog incident was
identified in the captured log window. Existing Radio/Spotify files and settings
were preserved. The owner subsequently confirmed that the main menu, Standalone
Base, Radio Browser and Spotify Connect still open/work. This verifies the
existing running UI, not activation of the newly deployed Lua. Qobuz is absent and was not
installed. These observations qualify existing runtime preservation, not new Lua.

TinySC arbitration/controller/coordinator were not installed or enabled. The old
Base lifecycle endpoint returns 404; TinySC absence does not prove arbitration.
No Music runtime, database, scan, ownership takeover or playback was activated.
USB write counters stayed six writes / 29 sectors. No USB/factory writes were
requested. The historical WP4a.2b watchdog gate remains unresolved, not bypassed.

Local checks: 102/102 existing host tests, 27 Lua syntax checks, complete Fab4
native build/package/static checks, eight new file-deployment/rollback tests,
plus targeted applet/lifecycle CTest reruns. Host proxy/libcurl suites remain
excluded (BUILD_SBPROXY=OFF); static ARM proxy build passed. No large benchmarks.

The bounded smoke test and disk deployment are finished under the no-restart
constraints. The next hardware work would require a separately scoped safe
activation plan and resolution of restart/native-compatibility questions; do not
start restart, decoder or native music qualification automatically.

- [Preflight](STANDALONE_MUSIC_WP4A2C_PREFLIGHT.md)
- [Deployment, command and hashes](STANDALONE_MUSIC_WP4A2C_DEPLOYMENT.md)
- [Smoke tests and measurements](STANDALONE_MUSIC_WP4A2C_SMOKE_TEST.md)
- [Rollback](STANDALONE_MUSIC_WP4A2C_ROLLBACK.md)
