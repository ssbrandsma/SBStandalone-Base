# 0.7.5 Baby recovery — 2026-11-17

The development Radio reported repeated full startup logos after the 0.7.5 test deployment. Recovery restored the exact original working 0.7.3 applet tree. The full reboot-loop cause remains unconfirmed; successful compilation, host tests and a committed installer transaction did not establish runtime readiness.

## Recovery performed

- Reconnected at 192.168.1.78 and checked the original CPU UUID before acting. Used the supplied `squeezeplay stopwdog` maintenance action to stop Jive temporarily while keeping the hardware watchdog serviced.
- Captured available boot logs. Verified all 67 files in the transaction's rollback tree against its recorded SHA-1 inventory. Verified all eight native binaries against the original pre-test archive.
- Stopped only native processes whose command lines matched the Base installation. Preserved the failing 0.7.5 tree at `/mnt/sbdata/.sb075-test/rejected-075-runtime`, then restored the saved tree.
- Restored the two changed original Lua files and moved eight added bootstrap modules out of the active tree. Verified all 59 resulting files against the original working archive, whose SHA-256 is `0aa6898f212a49349cfa1aea7856374e7bf92687d3850fd6eeaac8fab0dc47e2`.
- Restored only the Base 0.7.3 installer registration and private catalog entry, retaining other applet/settings data. Archived the historical committed transaction rather than rewriting its receipt. Stopped the workstation's private candidate package server.
- Removed the temporary maintenance sentinel and started Jive once. No firmware reflash, factory reset, kernel modification or permanent watchdog change was performed.

## Verified result

Base `/health` reports `status: ok`, version `0.7.3`. HTTPS returns 200 with the same previously verified SHA-256 certificate fingerprint. Both VPN profile IDs and their credential-configured flags match the pre-install snapshot. Credentials and the private TLS key were not exported or replaced. The normal clock screen was observed.

Jive PID/start time remained unchanged across checks. The final observation was over four minutes after its recovered start, with kernel uptime continuing and Base still healthy. This is a recovery stability check, not renewed 0.7.5 acceptance.

Detailed logs, checksums, original/candidate archives and recovery manifests remain in the ignored `.local/release075/hardware/` directory. The original persistent configuration/certificate backup also remains on the Radio at `/etc/standalonebase.pre075.tar`. These private recovery artifacts are not release packages.

## Outstanding

Investigate the captured startup/fallback logs and repeated full reboot report offline before another hardware deployment. The strict-module `require`/`type` fix and bounded installer-memory tests are committed, but they do not resolve or explain the full boot-loop report. Both platforms still require complete runtime acceptance; the current Radio remains on 0.7.3.
