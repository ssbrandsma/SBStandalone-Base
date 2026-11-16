# Candidate artifact manifest

The machine-readable authority is
`build/releases/upnp-0.3.0-rc1/release-manifest.json`; `SHA256SUMS` covers packages,
target XML, per-package metadata, source archives, build inputs and evidence.
Every ZIP member has a size, mode and SHA-256 in the release manifest. Native
files are compared byte-for-byte with the fresh build outputs and checked as
static soft-float ARM EABI5 for their target. Base and network Base are also
probed under the target QEMU CPU; the UPnP core executes on both CPUs.

Packages use the normal flat applet structure and names:

```text
build/releases/upnp-0.3.0-rc1/StandaloneBase-0.7.4-rc1-baby.zip
build/releases/upnp-0.3.0-rc1/StandaloneBase-0.7.4-rc1-fab4.zip
```

Both carry Base 0.7.4-rc1 / sbupnp 0.3.0, the complete network native overlay,
Lua transaction adapter, target/resource compatibility metadata and notices.
Fab4 omits the Baby sbubifs module and empty UBIFS image. The embedded default
catalog reports the candidate version and omits historical Base self-update
entries. Other existing applet entries are preserved. Persistent owner settings
are not overwritten by the stock applet transaction. Relative target XML URLs
are review metadata until a trusted distribution location is deliberately set;
no public upload or release endpoint was created.

`sources/source-inventory.json` lists exact source revisions and archive hashes.
Committed sources are exported with `git archive`; private worktrees, logs and
credentials are not source inputs. The Base export excludes historical binary
artifacts, including the module/image and mkfs host binaries, while retaining
their available build scripts and patches. Supplied curl/wolfSSL sources retain
configuration but omit object/library/build executables. Public upstream crypto
test fixtures are source data, not owner credentials. Consumed build inputs
retain actual SDK options, caches, flags and static-library/compiler hashes.
Read the license audit: this is not a claim that the missing Baby module source
has been recovered. Original proxy ownership was clarified by the owner and
recorded under Base's MIT terms, with wolfSSL's separate terms retained.

Expanded member bytes and approximate 4 KiB file allocations are reported for
each ZIP. The manifest supplies conservative same-size upgrade and previous-slot
planning bounds. Add actual old applet size, other applets, directory/filesystem
metadata and free-space reserve; compressed ZIP bytes alone are not installation
capacity. Tree renames do not duplicate the old tree, and the streaming adapter
does not retain a second archive on the device. Retained rollback slots consume
real space and are never silently deleted.

The consolidated report supplies final byte counts, checksums and source commits.
All artifacts remain held for the documented distribution and physical gates.
Fab4's builder no longer requires Baby-only artifacts: a separate source-tree
packaging check with no `artifacts/` directory reproduces all 80 final installed
file bytes and modes. The original final ZIP was retained unchanged.
