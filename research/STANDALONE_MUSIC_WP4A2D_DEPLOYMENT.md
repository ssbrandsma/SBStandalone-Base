# WP4a.2d deployment

Target exclusively root@192.168.1.244, trusted existing SSH host key. .245 is a
Radio and was not accessed. The owner explicitly corrected the attachment IP.

The named session driver reuses scripts/deploy_dist.py Radio SSH/SCP and its
shared atomic_replace primitive: generated /tmp/standalonebase-deploy.* staging,
verified upload, copy preserving metadata to .new, chmod, atomic rename through
merged /usr. No alternative deployment transport, Jive hot reload or new native
launcher was introduced. The entry wrapper execs the selected binary in the same
PID launched by the existing in-memory applet supervisor. Its existing argv
prefix liveness test recognizes sbbase-core and sbbase-music.

Exact commands, repository root (password supplied through SB_DEVICE_PASSWORD):

```text
python .local/wp4a2d/session.py prepare
python .local/wp4a2d/session.py install
python .local/wp4a2d/session.py core
python .local/wp4a2d/session.py browse
python .local/wp4a2d/session.py repair-ram-mode
```

prepare and install do not start a second server. install backs up/validates all
original targets and stages the recovery originals before replacement. It verifies
the pinned firmware source, directory ancestry, source/target hashes, owner/modes,
board, original Base version and idle playback. An installation quarantine exists
before the public TinySC service is replaced. Policy is last. File-only errors
restore attempted targets, including failures after rename.

core sends SIGTERM only to the verified old Base PID/start, after restoring a
valid sole-launcher entry. It never touches /var/run/squeezeplay.pid. The existing
supervisor launches Base 0.7.0; verified actor 31494/start 7686387 reported current
STOPPED_VERIFIED, operational/fresh and zero TinySC. The Music phase terminates
only that Base child and relies on the same supervisor again.

browse was refused before database creation because mounting tmpfs had reset
its root to 1777. Core fallback actor 304/start 7710880 remained healthy. The
reviewed repair-ram-mode action verifies empty DB volume, original mount identity,
core-only process, idle playback and verified arbitration, applies chmod 700,
then rearms the consumed pre-scan attempt once. Actual active Music actor is
2698/start 7738883. No failed/cancelled scan or corrupt database was retried.

All changed files, root:root, verified after activation:

| Path | Before SHA-256 | After SHA-256 | Mode |
|---|---|---|---|
| `/etc/init.d/squeezecenter` | `ae913ec4ba84d7b5003f08fddc2c3df15a453071784482e35f0dae2faf335c0d` | `4b92e4d68213b3e7bf441b52743eef971254e543dcf6540be6ca56ad8640f04b` | 0755 |
| `/etc/init.d/squeezecenter.stock` | `ABSENT` | `b6309001284e852e1328259f5d2a6a04e459cb93d664b0d6b2ccfa2d380273f9` | 0400 |
| `/mnt/storage/standalonebase/tinysc-arbitration.enabled` | `ABSENT` | `3c7a2f837de9082d657963b503d116b9ff252f854b8a888d6331dfee0b53cb50` | 0600 |
| `/usr/share/jive/applets/SqueezeCenter/SqueezeCenterApplet.lua` | `526973a57d2587cd86359dd0eded3a42744d142f2dbff3a1b199f5a77fec9dc6` | `e062d4123ddd92323765946da82f5d3c189b1cc65fee88c519d1677f0c1e894f` | 0644 |
| `/usr/share/jive/applets/StandaloneBase/sb-tinysc-control` | `ABSENT` | `5b64f5df0fa3d84fa1ba754b9d2fd99d79444e392f1e6fa3c4fbdeea3b78d837` | 0755 |
| `/usr/share/jive/applets/StandaloneBase/sbbase` | `b97b29a759589bf304ab3b7a5751c275c9477c4dfda12fc9c92edf5b93888b0a` | `492f3fd3dc93c33746e32c792ddda0f00ad39aa234a97f51f23ab1cdf6732e41` | 0755 |
| `/usr/share/jive/applets/StandaloneBase/sbbase-core` | `ABSENT` | `5754f168a141af7b359912431a1449c1a5c21eb40b5c09e64365eae8dcd14421` | 0755 |
| `/usr/share/jive/applets/StandaloneBase/sbbase-music` | `ABSENT` | `013cf9f4bfa6443e5cbdb5658504e2e60120bd4ceb00ca5e75152a0d98c05dd1` | 0755 |
| `/usr/share/jive/applets/StandaloneBase/sbmusic-metadata-helper` | `ABSENT` | `dc2512300fe2c39c6f22e2b52aa875abd8d0263013737ce48787d58c59f37a54` | 0755 |

Three original files were replaced and six paths added. Before/after records are
.local/wp4a2d/runtime-backup/manifest.json and final-files.json. Candidate hashes
are candidate/plan.json. The immutable archive originals.tar SHA is
471553cfa8345da6cf43f67ccb9d22ea05715e461febfd8b0aad577f0f015e07. Broader audit has 70 before/74 after records;
only the expected two replacements/four additions in that audit differ. The
additional SC Lua, stock service and policy paths are covered by the exact plan.

Native Base/SQLite/scanner/browse live in sbbase-music. sbbase-core is the ordinary
0.7.0 fallback; metadata parsing uses the isolated helper. No Music webserver,
sbmusic-session or ownership Lua/adapter was installed. Existing webserver/proxy
remain unchanged. Persistent catalog/installer release metadata remains 0.6.7;
this is a controlled 0.7.0 native test session, not a full published package upgrade.

The SqueezeCenter Lua overlay is on disk for later ordinary startup, not loaded
into the current Jive VM. Current TinySC exclusion relies on the active native
lease/latch/public service guard and ongoing detection, not a claim of live Lua
replacement. The inspected stock start/restart paths call the guarded service.
The current My Music selection path uses the connected native server and does
not invoke the stock SqueezeCenter storage setup.

RAM markers, DB and process PID files are transient operational state. Native
Music is never automatically rescanned on insertion or startup retry. After a
normal reboot, absent RAM marker means ordinary Base, not a new Music scan.
No Jive/ALSA/watchdog restart or bootloader/factory partition write occurred.
