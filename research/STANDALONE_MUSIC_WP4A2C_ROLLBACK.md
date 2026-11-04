# WP4a.2c rollback

Original selected files: .local/wp4a2c/applet-backup/originals.tar. SHA-256 and
UID/GID/mode/mtime: matching manifest.json. It records nineteen originals and
original absence of LifecycleHeartbeat.lua. The verified selected upload is
applet.zip; deployment.json records the three actual changes and all installed
hashes. Broader immutable audit is touch/before-audit.tar (69 records); after
audit has 70, with only the expected two replacements and one addition.

Exact owner restore command, from repository root with trusted SSH/password env:

```text
python scripts/deploy_dist.py .local/wp4a2c/dist/StandaloneBase-0.7.0-fab4.zip root@192.168.1.244 --applet-only --no-restart --restore-backup .local/wp4a2c/applet-backup
```

This is available, **not executed on the Touch**, because the successful update
did not require rollback. Do not substitute .245 or another backup. Local tests
executed the same production copy/restore functions against a temporary Linux
tree: exact original bytes/mode/mtime returned and the newly added file vanished.
Interrupted post-rename copying automatically restored originals. Unknown owner
revisions and corrupt archives were refused. Actual old UnionFS live rollback
has not been exercised and is not claimed qualified beyond these file checks.

Restore uses the existing trusted SCP/stage/atomic rename route and has no Jive,
native service or watchdog stop/restart command. It checks Fab4 and idle state,
validates host-bound backup archive and current known original/new revisions,
restores selected originals and removes only known newly added files. Existing
other app/config/native/USB files are outside its write set. Symlinks are refused
before replacement; no affected symlink needs rewriting. Before/after audits
preserve existing symlink targets, should any be present elsewhere.

On-disk restore does not unload Lua modules or reverse initialization side effects.
Here the newly changed main applet/heartbeat were never loaded, and no reload was
attempted. Do not invoke free(), squeezeplay stop/restart, the maintenance helper,
reboot, or native takeover to make disk rollback appear like runtime rollback.

The original .245 wrong-device/trust issues were resolved by identifying and
explicitly resuming on .244, not by changing host trust or using an unverified
fallback. The earlier WP4a.2b backup/recovery installer was not used for deployment
or rollback and its unqualified coordinator remains absent.
