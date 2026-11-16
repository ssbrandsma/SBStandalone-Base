# Standalone UPnP release candidate finalization

Base **0.7.4-rc1**, sbupnp **0.3.0**, 2026-11-15. The production code and host
qualification are delivered. Public Baby distribution remains held on one
identified source-compliance gap: the exact prepared vendor kernel source and
configuration for the existing authorized sbubifs module could not be recovered
from the failed original WSL filesystem. The owner clarified that original
SBproxy is project-developed Base code; its MIT notice is now explicit. The
dated CA bundle's upstream checksum and MPL 2.0 notice are verified and supplied.
Fab4 has no remaining identified source-inventory gap from this audit.

Neither target has been physically installed or qualified in this assignment.
Controlled physical acceptance must follow the documented firmware, storage,
backup and independent recovery gates. Do not describe either ZIP as a proven
Linux 2.6/audible production release. Nothing was publicly published.

## Completed implementation and qualification

1. Original Jive playlist: actual `status FROM QUANTITY menu:menu` contract,
   authoritative shared local/network queue, total count and paginated rows,
   zero-based active index, stable entry/context/revision, Play/Remove/Move/Clear,
   stale-action rejection and natural next. The 137-item production chain and
   original DB.lua test pass. Mixed local/network identities and edits pass native
   queue tests. Original cover/artwork/navigation remain; no Jive file was patched.
   Spotify presents Now Playing once per session and preserves later playlist
   focus. Spotify/Qobuz expose actual current metadata only where their existing
   clients provide no usable full queue; Radio shows the current station. No
   fabricated queue, decoder or ownership manager was introduced.
2. Fresh Base regression: **133/133 passed, no failures or skips**, plus twenty
   consecutive backlog repetitions. Historical environment/port collisions,
   missing unzip/Lupa, outdated reconnect assumptions, incomplete HTTP test reads
   and a too-small status test buffer were diagnosed and corrected without
   weakening process identity, physical idle or destructive-operation guards.
   Ecosystem results: **29 Radio Lua suites, 32 Spotify tests, two Qobuz suites**.
3. Actual installer: private stage/journal, same-stream archive hash, original
   native Jive ZIP/SHA1/LTN12 integration, syntax/static-ELF/IPC/target gates,
   immutable tree rename, preserved old executable inode, rollback/recovery and
   configuration boundaries. Ten transaction tests and four original-module
   integration runs pass. Both final ZIPs pass the production verifier, wrong
   target/wire rejection, commit, XML SHA1 and version/catalog consistency checks.
   Legacy full SSH deployment is blocked for network packages; backed-up applet-only
   bootstrap is separate from normal stock full-package activation.
4. Dependency audit and available source bundles: exact pupnp 22.2.2 revision
   `fac0494691434c35243131f66a0b27ea0c29f700`, BSD author notices, Base/web/Mongoose,
   curl/wolfSSL, musl/GCC exception, raster codecs, SQLite, yxml, uPlot and CA
   inventory. Matching committed application sources, pinned permissive sources,
   supplied curl/wolfSSL sources and consumed build inputs accompany the ZIPs.
   The SqueezeOS patch-family recovery bundle is explicitly not verified source
   for the authorized Baby module. This distinction is the remaining license hold.
5. Fresh Baby/Fab4 native builds, versioned normal-layout packages, target XML,
   per-file SHA-256/modes, source/evidence hashes, footprint and owner guides.
   Ten executables per ZIP match fresh build bytes and static soft-float EABI5
   target attributes. ARM926/ARM1136 QEMU probes, actual storage hash/CRC paths
   and UPnP core checks pass. ASan/UBSan with leak detection passes the complete
   SDK/service/Base/web chain, with zero child exit codes and graceful shutdown.

## Remaining physical and source boundaries

The only shipped audio path is the original SqueezePlay engine. Firmware peer
events, decoder idle attestation, renderer backend state and network delivery
are explicitly simulated where stated. No physical player was contacted; no
package installed; no physical original Jive UI observed; no audible result or
Linux 2.6 runtime compatibility independently confirmed.

The stock installer success path calls the original firmware `reboot` service.
Host tests do not prove watchdog ordering, loaded Lua identity or post-boot
supervision. The recovery API refuses committed rollback without independent
quiescence proof; host tests label that proof as simulated. Owner recovery must
be qualified using the existing firmware lifecycle and backups. Lua close/rename
does not establish power-loss/fsync durability. No new restart, init, flash or
storage-setup operation was executed. Existing settings, favorites, credentials,
library/media and renderer preferences are outside applet transaction trees.

The current network profile requires exact qualified firmware 7.7.3 r16676 and
the existing Fab4 TinySC arbitration gate. Insufficient space must fail safely.
A fresh Baby without enough existing writable/Extended Storage capacity is not
a qualified automatic migration case; this task does not initialize storage.

## Final packages and footprint

Directory: `C:\Projects\SBupnpdlna\build\releases\upnp-0.3.0-rc1`.

| Target | ZIP bytes | Expanded member bytes | Approximate 4 KiB file allocation |
| --- | ---: | ---: | ---: |
| Baby | 3,738,603 | 8,073,495 | 8,273,920 |
| Fab4 | 3,636,073 | 6,027,908 | 6,225,920 |

```text
cc5c140502f7db1bffc2008093c5d6d083838424f83911ff6e45b1aeaf288281  StandaloneBase-0.7.4-rc1-baby.zip
ad125e7b3c0510291cbd76920a21a9c6cd2c9553bb8d7c05e412313ae26c8ae4  StandaloneBase-0.7.4-rc1-fab4.zip
```

Conservative same-size upgrade planning, including an archive allowance, is
20,286,443 Baby bytes / 16,087,913 Fab4 bytes. With a previous same-size rollback
slot retained: 28,560,363 / 22,313,833 bytes. Add actual old-tree differences,
other applets, filesystem metadata and reserve. These are planning bounds,
not a claim about the player's current free capacity.

Native Base reports 0.7.4-rc1. Unchanged proxy/storage component protocol versions
remain their existing versions (for example proxy 0.7.3); the packaged files
are freshly compiled and verified, not copied historical executables.

## Evidence and source control

`release-manifest.json` and `SHA256SUMS` are the artifact authority. The `sources/`
inventory supplies exact archive paths, revisions and hashes. The `evidence/`
directory preserves the complete final regression, sanitizer, original Jive,
original installer, package/QEMU, ecosystem, resource and soak results. The final
soak and allocator interpretation are recorded in the regression report and its
raw JSON, including actual duration and completed cycles.

The final soak completed **1800.178 seconds**, **5116 Browse/Search pairs** and
**44 native/renderer cycles**, with zero exit status. Sampled post-cycle RSS was
2748–10552 KiB (9516 KiB at the end), eight descriptors, 14–18 threads, and 8.38
daemon CPU seconds. The separate 302.424-second single-arena diagnostic and
clean sanitizer teardown support allocator retention as a contributor to host
RSS growth; no production allocator configuration was changed.

Base changes are on branch `upnp-rc-finalization`, isolated worktree
`C:\Projects\SBupnpdlna\.local\rc-finalization`, final source commit
`db4c56f451014d8bef43c9ad766b6ecd70a951bf`. The unrelated active Base/VPN checkout
was preserved. Spotify focus commit is
`a8b90a16a1432e120f69ebecff9a69175920a710`. UPnP's final source commit is recorded
in the machine manifest and source inventory, avoiding a self-referential report
hash. Qobuz's pre-existing unrelated changes and Radio source were preserved.

Focused reports: [playlist](STANDALONE_UPNP_JIVE_PLAYLIST.md),
[regressions/resources](STANDALONE_UPNP_REGRESSION_QUALIFICATION.md),
[installer/recovery](STANDALONE_UPNP_INSTALLER_QUALIFICATION.md),
[license audit](STANDALONE_UPNP_LICENSE_AUDIT.md),
[artifact manifest](STANDALONE_UPNP_RELEASE_MANIFEST.md), and
[Baby/Fab4 owner acceptance](STANDALONE_UPNP_PHYSICAL_ACCEPTANCE.md).

The final source packaging check also rebuilds Fab4 without any Baby module or
UBIFS image available. All 80 installed file bytes and modes match the immutable
Fab4 ZIP; only archive timestamps are outside that comparison. Thus Fab4 source
packaging does not depend on the unresolved Baby-only binary inputs.
