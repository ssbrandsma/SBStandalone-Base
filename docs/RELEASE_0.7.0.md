# Release 0.7.0

This release adds explicit Baby/Fab4 static builds, safe board/package checks, TinySC conflict diagnostics and a working Virtual Touch with a generated photorealistic Touch image. Baby retains its ARMv5 soft-float baseline; Fab4 uses ARMv6 soft-float with compatible ARMv5 static dependency libraries. GCC is Buildroot musl 8.4.0.

Build and package on Linux/WSL (Git Bash entry points forward to WSL):

```sh
sh scripts/build-arm.sh all
SB_PACKAGE_CONFIG="$PWD/packaging/releases/0.7.0.package-config.json" \
  SB_RELEASE_BASE_URL=http://49.12.198.91/sbstandalonebase \
  sh packaging/build-package.sh all
```

The frozen package input catalog retains the previous published Baby Base entry and includes Radio for both targets. The final public catalog has Base 0.7.0 and Radio 0.9.18 for both Baby/Fab4, with the actual ZIP checksums. Do not repack to insert a ZIP's own checksum: that changes the checksum. Runtime catalog synchronization imports the current public catalog. The release ledger is `packaging/releases/0.7.0.json`. ZIPs are generated artifacts outside Git; their hashes identify the published bytes.

Use `packaging/prepare-release.py` to merge Base and Radio target entries into supplied JSON/XML catalogs while preserving other applets and server settings. It generates Base-only and Radio-only extensions plus a release ledger. Supply the Radio ZIP and public Base/Radio URLs, the output archive directory, repeated `--config` JSON paths and repeated `--extensions` XML paths. Prepare against a fresh copy of the live bootstrap configuration and merged XML; do not replace live settings with a workstation's example server config.

The updated `packaging/deploy-remote.sh` validates both targets, all archive SHA-1/SHA-256 values, JSON/XML consistency and live metadata preconditions before publishing. The stage contains the three ZIPs, release ledger, `base-extensions.xml`, `radio-extensions.xml`, `merged-extensions.xml`, `bootstrap-config.json`, `public-config.json` and `preconditions.json` (map of live catalog absolute paths to SHA-256). Private deployment environment files remain local-only and now define `RADIO_ROOT` too. Set `SB_DEPLOY_CONFIG`, `SB_RELEASE_VERSION` and `SB_RELEASE_STAGE` on the server. Artifacts install before metadata; metadata is atomically replaced per file, backed up under `/var/backups/standalonebase/`, and restored if deployment fails. Only the existing bootstrap container is restarted. Other applet entries are preserved.

Validation: both ARM builds passed static ELF/EABI/CPU/dependency validation; 60 host tests passed in 155.24 seconds. Archive CRCs, executable permissions, version strings and matching metadata were verified. Shared Virtual Touch changes were previously verified on physical Fab4 hardware; no fresh Baby hardware test was performed. Standalone Radio 0.9.18 is the existing Lua/artwork-only release and uses one ZIP for both targets. No Radio code change or new Radio version was needed.
