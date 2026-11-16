# Standalone Base 0.7.5 — software validation checkpoint

**2026-11-17 hardware checkpoint: startup acceptance failed.** The stock installation committed and its 93 files and 14 executable/script modes verified. A subsequent Lua module-scope startup fix was deployed, but the owner now reports repeated full startup logos. Neither known Radio address is reachable for SSH recovery. The cause of the full reboot loop is not yet established. The priority is restoring the preserved previous applet tree; 0.7.5 is not qualified for general deployment. No factory reset or firmware reflash has been performed for recovery.

Prepared 2026-11-16. These are private review candidates, pending full Baby and Touch acceptance. Software validation was followed by owner-authorized Baby installation testing and controlled restarts. Two stock-installer attempts rolled back before commit: executable-permission command failure, then inability to start a permission probe under memory pressure after extraction. The existing runtime was preserved.

The installer now precreates and qualifies executable permissions before downloading, keeps those same inodes through extraction, and verifies inode/owner retention before commit without spawning a process after extraction. A further preflight failure before download led to bounded garbage collection of temporary inventory buffers. Tests include unavailable process creation, inode replacement, and an 8 MiB inventory under a retained 10 MiB Lua heap with less than 1 MiB of temporary growth. The correction is committed, built, and its bootstrap is on the Baby; a successful stock installation is still unverified.

The owner restored connectivity over Ethernet at 192.168.1.151. The trusted SSH key and existing HTTPS certificate matched, and the exact test catalog checksum was verified. After the next approved bootstrap reboot, neither Ethernet nor the previous Wi-Fi address answered. Hardware testing is paused for the owner to check Ethernet link/address. No network configuration was changed by deployment, and public catalogs and hosting have not been changed.

## Implemented

- Integrated the finalized UPnP/DLNA runtime, original Jive My Music and playlist contract, stable queue actions, pagination, stale-action rejection and ownership handover. Added the previously omitted four ownership Lua modules to both packages.
- Included network playback by default in the release build. MediaRenderer remains an explicit opt-in. Added a single VERSION source and package/runtime contract checks to reject mixed or incomplete installations.
- Added transactional installation with checksum, Lua, target, static ARM ELF and IPC validation, executable permissions before activation, and preservation of the previous tree on failure. The old installer requires a backed-up Lua bootstrap before using this path; full SSH overwrite of a network-runtime ZIP is refused.
- Added exact Baby 9.0.2 r17160 admission alongside Baby 7.7.3 r16676. Fab4 7.7.3 r16676 retains its existing TinySC arbitration requirement. Unknown profiles fail closed for network playback.
- Updated sbupnp to 0.3.1. Discovery binds an eligible private IPv4 multicast LAN and excludes tunnel/point-to-point interfaces. A VPN default route does not redirect SSDP onto the VPN interface; loss of the bound LAN triggers supervised recovery.
- Removed temporary HTTP/body diagnostics and disk stream tracing from PlaybackFenceV6, bounded its diagnostics in memory, and fixed its missing-playback timer handling.
- Preserved VPN and HTTPS credential provisioning, certificate persistence, physical pairing and transport protections. Corrected the secure portal banner punctuation and VPN capability description. No automatic TUN loading or firmware/kernel modification was added.
- Added self-contained build, isolated test, package verification, local catalog preparation and source/provenance export tools. Archived the original UPnP research separately from current release instructions.

## Verification

The complete host suite contains 155 tests. The initial final run passed 154; its only failure was an artwork error test relying on external DNS inside an isolated network namespace. That test now uses a deterministic upstream disconnect and still requires the same 502 response. The corrected artwork and installer tests pass. The final complete rerun passed **155/155 tests**, with zero failures in 445.93 seconds; evidence is recorded in `.local/release075/tests-confirmed.log`.

Both ARM packages pass package CRC, inventory/dependency closure, runtime/version contract, source/asset matching, native executable permissions, static ARM ABI and manifest checks. QEMU checks pass for the Baby ARM926 and Fab4 ARM1136 component version/core tests. The final Baby network webserver passes the ARM HTTPS provisioning suite.

Seven tests using the original native Jive ZIP/SHA1 and LTN12 installer path pass: real Baby/Fab4 packages plus checksum, disk-space, wrong-platform, wrong-wire and chmod failures. Failures preserve the installed tree; positive cases verify executable permissions.

The original Jive playlist contract also passes against both the stock reference and extracted official 9.0.2 firmware DB implementation. Six wrapped Playback/Player method bodies match the reference after comments are excluded. This is a source comparison, not physical qualification. Official Baby firmware ZIP SHA-256: `8fbe5c8cbec90ca97ec8fa8ee254ecdcfc2e1b6d6cd4319eacae10d841f82382`.

## Private candidates

Under `.local/release075/candidates/`:

| Package | Bytes | SHA-256 |
| --- | ---: | --- |
| StandaloneBase-0.7.5-baby.zip | 4461648 | 27f0f188b0555fc2813cce71ffaf5ca40b3d53abbf7b4d5faacfa7d2203ba248 |
| StandaloneBase-0.7.5-fab4.zip | 4357937 | d9ca6b425c6bcb92e1806b697d35b2dfb2c332c86cae8628a6b2404e8b72f922 |

Each contains 11 native executables. `release-verification.json` records the verified hashes. Per-target XML/manifest files accompany the ZIPs. `catalog-review/` contains proposed 0.7.5 config.json and extensions XML files, prepared locally against those hashes. The published root catalog remains at its existing version; nothing was uploaded. Embedded package catalogs do not offer the stale Base self-update.

## Hardware acceptance remaining

1. Back up the existing installation and persistent configuration; apply the Lua bootstrap through the supported procedure. Obtain explicit approval before restarting Jive or rebooting. Exercise the stock transactional installer, rollback, activation, watchdog and UnionFS behavior on Baby first.
2. On actual Baby 9.0.2, discover a real MediaServer, browse/search, play supported MP3 resources, use the original playlist and Now Playing, and verify pause, stop, natural next, volume and queue edits. Exercise Radio/Spotify/Qobuz ownership handovers and stale actions.
3. Enable the optional renderer deliberately and check controller actions and GENA events. Test discovery/playback with VPN disconnected and connected, including LAN changes.
4. Verify existing VPN profiles and credentials, HTTPS pairing and unchanged device certificate fingerprint. Qualify TUN availability and startup using the firmware-supported mechanism; this work does not claim that the existing TUN startup prerequisite is resolved.
5. Repeat platform-specific acceptance on Touch with its TinySC arbitration profile. Unsupported firmware and resource types must remain safely rejected.

Supported network resources remain numeric private/link-local IPv4 HTTP MP3 with known length and audio MIME type. HTTPS/hostname/public resources, redirects, chunked audio and additional codecs are outside this implementation. See [UPNP.md](UPNP.md) for bounds and protocol behavior.

## Source and distribution review

`scripts/export-release-sources.py` prepares the uncommitted working source snapshot, pinned dependency sources and consumed configuration/library hashes under `.local/release075/sources/`. This records the dirty working tree rather than presenting HEAD as the implemented source.

Public distribution is not cleared by functional testing: matching vendor source/configuration for the authorized Baby sbubifs module remains unverified, and the Mongoose GPLv2 / portal wolfSSL GPLv3 static-link licensing combination requires clearance. The source inventory records these holds. No license grant or corresponding-source completeness is assumed.
