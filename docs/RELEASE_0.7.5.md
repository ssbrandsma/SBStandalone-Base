# Standalone Base 0.7.5 — software validation checkpoint

**Publication, 2026-11-17:** At the owner's explicit request, 0.7.5 was published to 49.12.198.91 and the normal Base/bootstrap update catalogs were updated. It is labelled experimental with the known startup/reboot failure in its description and changes. This publication does not clear failed hardware acceptance; the recovered development Radio remains on 0.7.3. See [publication record](RELEASE_0.7.5_DEPLOYMENT.md).

**2026-11-17 hardware checkpoint: 0.7.5 failed startup acceptance and was rolled back.** The Radio is restored to its original working 0.7.3 installation at 192.168.1.78. All 59 original files verified byte-for-byte, HTTPS identity and both VPN profiles' credential-configured states were preserved, and the same Jive process remained healthy for over four minutes after recovery. The failing 0.7.5 tree and logs are retained for diagnosis. The full reboot-loop cause is not established; 0.7.5 is not qualified for general deployment. See [recovery record](RELEASE_0.7.5_RECOVERY.md). No factory reset, firmware reflash or permanent watchdog change was performed.

Prepared 2026-11-16. These are private review candidates, pending full Baby and Touch acceptance. Software validation was followed by owner-authorized Baby installation testing and controlled restarts. Two stock-installer attempts rolled back before commit: executable-permission command failure, then inability to start a permission probe under memory pressure after extraction. The existing runtime was preserved.

The installer now precreates and qualifies executable permissions before downloading, keeps those same inodes through extraction, and verifies inode/owner retention before commit without spawning a process after extraction. A further preflight failure before download led to bounded garbage collection of temporary inventory buffers. Tests include unavailable process creation, inode replacement, and an 8 MiB inventory under a retained 10 MiB Lua heap with less than 1 MiB of temporary growth. The correction enabled a verified stock transaction commit, but subsequent runtime startup failed. The candidate bootstrap is no longer active on the recovered Radio.

The owner restored connectivity over Ethernet at 192.168.1.151 during testing. Later SSH recovery succeeded at the original 192.168.1.78 after the owner reported repeated full startup logos. Recovery used the firmware's temporary maintenance stop, verified backups, and a single Jive start with restored files. The private candidate update entry was reverted to 0.7.3 and the workstation test server stopped. No network configuration was changed. Public catalog publication occurred later at the owner's request, as recorded above.

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
| StandaloneBase-0.7.5-baby.zip | 4461688 | 56382ffbdda80bb4165d6996b83eb641a37382ff6270c868a80350260615abd0 |
| StandaloneBase-0.7.5-fab4.zip | 4357977 | bddc42906eddd6e3db556f24190a50ea3c358b3a6bed5b81c88113eb4d274276 |

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
