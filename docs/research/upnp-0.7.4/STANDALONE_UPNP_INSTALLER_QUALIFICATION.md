# Installer and recovery qualification

Base 0.7.4-rc1 extends the actual stock SetupAppletInstaller through InstallerCompat
and InstallerTransaction. The stock installer originally removed live applets
before downloading, downloaded twice, and then called the firmware `reboot`
service. The adapter now hashes the same ZIP stream it extracts into a private
stage. Only verified complete applets are committed. The stock success path and
firmware lifecycle are retained; no live Jive restart command was added.

Stage path is `APPLETDIR/.standalone-install/{new,old}` with a protected journal.
Archive bounds, traversal/symlink/special-file rejection, duplicate detection,
Lua syntax, ARM static ELF and network compatibility gates precede replacement.
The network manifest requires target Baby/Fab4, resource wire 3, UPnP IPC 1,
ownership wire 1, queue limit 256 and renderer disabled by default. Existing
runtime negotiation independently rejects incompatible resource capabilities.

Whole applet trees are renamed; mapped native executables are not overwritten.
The receipt records PREPARED, COMMITTING, COMMITTED_ON_DISK and ROLLED_BACK.
Old trees remain available independently of the new services. Hash validation
refuses rollback over changed applets or corrupted backups. Interrupted renames
are recovered from actual tree identity, including a rename before journal update.
Committed rollback requires an independent quiescence proof; tests explicitly
label that proof as simulated. The module never fabricates it from metadata.

The real-filesystem tests cover successful staging, checksum failure, simulated
disk-full, multi-applet commit failure, a running old executable's inode/hash,
interrupted journal recovery, corrupt backup, changed destination and state A-F
configuration preservation. Final ZIP validation invokes this same production
verifier on both complete extracted packages and rejects wrong IPC/target values.
Ten transaction tests also verify uninstall preserves unrelated applets and an
independent rollback tree. Four additional integration runs use the original
Jive C zipfilter/SHA1 and original LTN12 from commit
`cca1bba4abeb3fef2b7c4a48a4e4da8fc255666d`: both complete target ZIPs are delivered
in 1301-byte chunks through the actual adapter, and checksum/disk-full faults
retain the old tree and block stock activation. Network transport and platform
identity are simulated; ZIP decompression, stream hashing and filesystem commit
are real. These reference modules are test inputs, not product dependencies.
An older completed transaction is archived to `.standalone-previous`; another
install fails closed when both rollback slots are occupied. No backup is silently
deleted. Budget space for two old applet trees plus the new tree and ZIP.

## Activation and owner installation boundary

An old Base VM still has the old adapter loaded. Copying new Lua files alone does
not activate this transaction implementation. The qualified `deploy_dist.py
--applet-only` path can stage the Lua bootstrap with verified backups and no
native replacement or restart. Baby requires its explicit `--applet-only-target
baby` profile and exact board/7.7.3 r16676 checks; Fab4 uses its existing profile.
Do not use the legacy full deployment path to claim a qualified live upgrade.
The owner must complete a normal firmware lifecycle before using the new adapter
for a full-package update. Installation must use the target-specific private
catalog and its SHA1, never a manually selected mismatching target ZIP.

The stock completion calls `appletManager:callService("reboot")` after two seconds.
This is the observed source contract; it is not proof of the precise physical
firmware shutdown ordering. Loaded Lua version, actual service supervision,
post-boot IPC and watchdog behavior remain hardware acceptance gates. Startup
uses the existing Base supervisor and its bounded fallback to ordinary Base/web
after repeated network-profile failures. No new service supervisor was added.

Settings, Radio favorites, Connect credentials, library/media, storage mounts and
renderer preferences remain outside transaction applet trees and are preserved
by the host state fixtures. The installer never initializes sbdata or flash.
No erase, storage initialization, physical install or Jive restart was performed.

**Remaining safety limitation:** an independently executable owner recovery
procedure for a failed post-boot candidate, with verified physical quiescence,
has not been qualified on either device. Do not treat the host recovery API or a
`COMMITTED_ON_DISK` receipt as post-boot health proof. Keep verified old packages
and an independent recovery route before any physical trial. Power-loss durability
is not claimed: the Lua API provides close/atomic rename, not fsync guarantees.
These gates and the license hold prevent an unconditional release-ready claim.

## Reproducing the original native module check

Export the following paths from the reference commit into `.local/jive-native`:
`src/luamd5`, `src/luazipfilter/src/luazipfilter.c`, and
`src/luasocket-2.0.2/src/ltn12.lua`. With Lua 5.1 development headers and zlib:

```sh
cd .local/jive-native
mkdir -p modules
cc -fPIC -shared -I/usr/include/lua5.1 src/luazipfilter/src/luazipfilter.c -lz -o modules/zipfilter.so
cc -fPIC -shared -DUSE_SHA1_SKALIBS -I/usr/include/lua5.1 -Isrc/luamd5/libstdcrypto src/luamd5/lsha1.c src/luamd5/libstdcrypto/sha1_*.c src/luamd5/libstdcrypto/uint32_*.c -o modules/sha1.so
```

From the UPnP root, invoke `scripts/verify-stock-installer.py` with Base source,
the host `restart_probe_fixture`, `.local/jive-native`, and the release directory.
The verifier uses Lupa's Lua 5.1 runtime and exposes its API to the original C
modules. No network connection to a physical player occurs.
