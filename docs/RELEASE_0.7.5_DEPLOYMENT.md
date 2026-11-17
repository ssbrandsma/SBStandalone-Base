# 0.7.5 publication — 2026-11-17

Published to 49.12.198.91 at the owner's explicit request after the failed hardware test and recovery. **This is an experimental build with an unresolved Baby startup/reboot failure, not a validated stable release.** Both the applet description and changes disclose that failure. The shared feeds do not enforce a separate experimental channel: they now advertise 0.7.5 for Baby and Fab4.

The recovered development Radio was not redeployed and remains on its restored 0.7.3 runtime.

## Published artifacts

| Target | Download | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Baby | [StandaloneBase-0.7.5-baby.zip](http://49.12.198.91/sbstandalonebase/StandaloneBase-0.7.5-baby.zip) | 4461688 | 56382ffbdda80bb4165d6996b83eb641a37382ff6270c868a80350260615abd0 |
| Fab4 | [StandaloneBase-0.7.5-fab4.zip](http://49.12.198.91/sbstandalonebase/StandaloneBase-0.7.5-fab4.zip) | 4357977 | bddc42906eddd6e3db556f24190a50ea3c358b3a6bed5b81c88113eb4d274276 |

SHA-1 values used by Applet Installer are in [the release manifest](../packaging/releases/0.7.5.json). Archives were installed before the catalogs advertised them. The existing 0.7.3 ZIPs remain unchanged and available. Standalone Radio remains 0.9.20; its published ZIP bytes were preserved.

## Updated catalogs

- Base `sbstandalonebase/extensions.xml`, public `sbstandalonebase/config.json`, and the public release manifest.
- Shared `squeezebox/extensions.xml` and its SHA-1 sidecar. The Radio-only `sbstandalone/extensions.xml` and its sidecar were verified; its entries remain unchanged.
- Bootstrap host `config.json`; its container was restarted and the mounted configuration verified.
- Repository root `config.json`, generated local bootstrap/dist catalogs, per-target local XML, and frozen `packaging/releases/0.7.5*` records. Historical release snapshots were retained.

Existing Spotify/Qobuz entries were preserved. Standalone Radio Baby/Fab4 metadata was retained, and the RG35XX H 0.9.20 entry was preserved in bootstrap and included in public config. No Base entry was added for RG35XX H.

## Verification and rollback

Both target packages passed inventory, architecture, static ABI, Lua/source matching, manifest and QEMU checks. The four publication/rollback tests passed. Downloaded public ZIPs match SHA-1 and SHA-256, public XML matches staged bytes, and all server XML SHA-1 sidecars match. Bootstrap `jiveapplets` responses advertise Base 0.7.5 and Radio 0.9.20 for Baby/Fab4 requests using 7.7.2, 7.7.3 and 9.0.2 version strings. These are distribution checks; they do not establish player runtime safety.

The deployment script checked live-file hashes before writes and retained rollback material at `/var/backups/standalonebase/0.7.5-20261117-072408` on the server. Original 0.7.3 archive hashes and unrelated catalog entries were checked again after publication. Private staging and verification evidence is under `.local/release075/publish/` and contains no files intended for public packaging.

Hardware failure/recovery details remain in [the recovery record](RELEASE_0.7.5_RECOVERY.md). Source/distribution review limitations in [the release report](RELEASE_0.7.5.md) remain unresolved by publication.
