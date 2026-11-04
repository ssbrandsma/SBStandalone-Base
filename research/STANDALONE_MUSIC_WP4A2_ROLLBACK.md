# WP4a.2 actual rollback and recovery

Rollback was required after the Jive decoder-probe restart unexpectedly rebooted the Touch and stock TinySC started. No Music playback was initiated. Further Music qualification stopped.

## Backups

`.local/wp4a2/installed-backup.tar`:12495360bytes; SHA256 `f7b7ab430f9dfc08908e6ee781164a50dc5037d0c7b97310357b5a08ffd57ce7`,91 regular-file hashes verified. Local staging utility `scripts/music-hardware-rollback.py` verified91 and staged90 allowed applet/config files, excluding firmware-version reference. Additional exact backups/modes/hashes of Base/Radio/Spotify/SqueezeCenter Meta files are in `.local/wp4a2/resume/meta-backup.json` and `meta-backup/`.

## Exercised procedures

Private8MiB RAM scratch: upload/read-back hashes, atomic candidate replacement, exact original restoration, wrong-session cleanup refusal and unmount/rmdir cleanup passed before the native scan ran. Running cleanup from the mounted script itself initially held the mount busy; host-fed `sh -s` corrected that. Explicit SSH completion markers handled the old Dropbear missing exit-status behavior.

The native scan-only candidate completed, closed SQLite, removed its capability, and the entire RAM volume was removed. Later, four Meta probe guards were atomically installed from verified backups. Their RAM-marker dependency was unsafe across full reboot and must not be reused as a qualified startup policy.

## Actual recovery

TinySC/resizer were stopped through the stock service and confirmed absent. Jive was held with `/etc/init.d/squeezeplay stopwdog` and its keepalive PID. An unconditional temporary SqueezeCenter Meta guard protected recovery registration/configuration across loss of RAM. Other original Meta files were restored. Existing helpers had to be shut down before clean Jive startup because original Base's port-conflict check rejects pre-existing listeners.

Known-good native helpers and Jive were restarted, with the watchdog PID held on its live keepalive until Jive registration was established. The original SqueezeCenter Meta file was restored last, after the guarded incarnation loaded it. All four Meta hashes, original sbbase/sbwebserver/sbproxy hashes and Base config hash matched backups. Normal watchdog PID was restored to live Jive. Remote UI navigation and Radio playback logs were observed. The temporary RAM volume and Music processes are absent.

TinySC created `/media/sda1/.Squeezebox/cache`; the observed directory was empty. It was left intact; user data was not deleted. TinySC may also have written its own settings during startup; no blanket unchanged-firmware/config claim is made. No factory partition, output configuration or music file was intentionally altered.

Current Jive retains the unconditional TinySC guard in memory; disk files are original. A later normal reboot with attached USB can reactivate the stock TinySC policy. This remains a blocker to future qualification. No persistent Music/TinySC policy change was left behind.

Private scripts/logs under `.local/wp4a2/resume/` are a one-session execution journal, some with historical PIDs; do not rerun them as portable recovery commands. The next qualification requires a new validated reboot-safe guard and watchdog-safe startup/recovery procedure, not repetition of the failed probe.

Final recovery observation: NPO Radio1 was stopped through the existing long-pause/Stop action. Final Base metadata is inactive/stopped; native services and Jive remain running, TinySC/Music are absent, and the test RAM volume is absent. The temporary authenticated remote session was logged out and its local cookie removed. The owner replied after the stop that nothing was playing and Now Playing was empty; this matches the stopped end state and does not confirm sound during the earlier sample. Radio audible recovery remains unverified.
