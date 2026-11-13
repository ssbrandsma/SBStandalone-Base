# Standalone Base 0.7.1 build and Radio deployment

Built normal Baby and Fab4 packages from the current working tree, retaining the previously tested browser-pairing, standby and connection fixes. Native Music and PlaybackFence files are excluded from both packages.

Version updated in native Base, proxy, applet menu and package metadata. The release was subsequently published to the download server. The immutable packages retain their build-time catalogs (Base 0.7.0 and Radio 0.9.18); refreshed public catalogs advertise Base 0.7.1 and the concurrently published Radio 0.9.20. Packages were not repacked to embed their own final checksums.

Validation: 39 host CTest regressions passed, with the WSL Lua-dependent idle-fence test skipped and passed separately on Windows. Static Baby/Fab4 ARM builds passed. Both target Base binaries passed the standby and shared-Radio Comet tests under QEMU. The first Fab4 standby-test invocation failed its startup deadline; a direct startup check and subsequent complete test rerun passed. Existing compiler warnings remain in image-render path sizing and isolated scanner indentation. Proxy-specific host TLS tests were not rerun; their code change for this release is the version string.

Deployment target: 192.168.1.223, verified Baby board with firmware 7.7.2 r9663 and the previously trusted SSH host key. Backups were made before changes. Installed all 50 package files, preserving configuration and all Radio applet files. The already-installed Radio standby guard was retained. All installed hashes and live Base/web/proxy executable hashes were verified. Base health reports 0.7.1. Jive PID 7209, watchdog PID 698; boot ID stayed bf2acc61-949a-4715-b512-dd255e98404a. One normal SqueezePlay restart; no reboot. Playback is stopped after the restart, and audible playback/standby confirmation for this deployment remains with the owner.

Private backups, build/test logs, hashes and deployment receipt: `.local/base-071/`.

Build staging note: old Fab4 generated files had WSL permission problems. Automatic approval review rejected recursive removal of the previous staging directory. It was left intact; packaging used a fresh private staging directory via the new optional SB_PACKAGE_STAGE override.

## Remote publication

The existing 0.7.1 packages matched current build binaries/assets and passed ZIP integrity checks, so no rebuild was needed. Published both target ZIPs, the Base extension list, combined extension list, public config and bootstrap config on 49.12.198.91. The Radio extension list and Radio 0.9.20 were preserved. The initial transaction stopped before writes when its preconditions detected a concurrent Radio release; metadata was refreshed before retrying.

Public archive downloads match the release manifest SHA-1/SHA-256 hashes. Public XML routes on the IP and bytestack domain, XML checksum sidecars, public JSON, the mounted bootstrap config and running container were verified. Live bootstrap queries return Base 0.7.1 and Radio 0.9.20 for both Baby and Fab4 under firmware 7.7.2, 7.7.3 and 9.0.2.

The broad host run exercised 127 registered tests: 120 passed under the ordinary account; six root-dependent maintenance tests passed when rerun as root; one Lua-dependent test passed separately on Windows. The PlaybackFence Lua regression also passed separately on Windows. ARM/QEMU results are recorded above. No safety checks were weakened to accommodate host permissions.

Publication backups and receipts remain private under `.local/release-071/`; the server retains its rollback catalog backup. Historical release catalogs are unchanged. Current repository catalogs and tracked 0.7.1 XML/JSON manifests describe the published release. The normal packages keep native Music disabled; separate Touch maintenance experiments remain outside the release commit.
