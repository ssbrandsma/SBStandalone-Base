# Original failure reconstruction

Reviewed WP4a.3b REPORT/RESTART/ROLLBACK/HARDWARE/EVIDENCE, WP4a.3 PLAN, WP4a.2b WATCHDOG, WP4a.2a WATCHDOG and the actual helper/coordinator/watchdog/copier, deployment receipt and current packaging implementations. The watchdog reports' earlier one-registration assumption is historical; the current two-allocation-registration rule was retained.

## Established sequence

1. Known browse runtime: PID 3933/start 17600372, launched from `/usr/share/jive/applets/StandaloneBase/sbbase-music` with the existing browse-only/scan-at-startup arguments. This phase did not repeat that scan.
2. WP4a.3b copied a candidate into a separate temporary inode through descriptor-relative operations, fsynced and atomically renamed it over the canonical path. The running process was never overwritten in place or restarted.
3. Source-bound installation/recovery checks passed, but native coordinator preflight failed at the first Base `verify-exe`. Its status remained REFUSED before stop/handoff.
4. The original file bytes were restored by file-only rollback. The existing process/boot/watchdog stayed alive. Current read-only evidence still finds its old mapped inode and an unreadable proc executable.

The original/restored installed file is 932644 bytes, SHA-256 `682cff02825f5f9b6e56f9787eea4931cc8d82a77bb7a488dfcbd1bca03ad90c`. The running process maps executable inode 1992 on 00:0b. Current merged pathname inode is 2069. The writable branch listing shows inode 2119 for its physical file; that is a different filesystem namespace and is not equated directly with a UnionFS inode.

## Failure stages

| Observation | Result |
| --- | --- |
| PID/start and live process | Valid and unchanged |
| readlink `/proc/3933/exe` | Succeeds, expected canonical pathname; errno 0 |
| open `/proc/3933/exe` | Fails: ENOENT (2) |
| Read/hash of that descriptor | Not attempted; no descriptor, zero bytes hashed |
| Open/read/hash current installed pathname | Succeeds; matches restored receipt |
| Active executable SHA relationship | Not proven (LEVEL 3) |
| Proxy/web live descriptor/hash/mapping | LEVEL 1 |

This is not a permission error, a readlink failure or a completed hash mismatch. Restored pathname content is not proof of the mapped process's bytes.

## What is not established

No retained pre-replacement live FD/stat witness exists. The first observed direct failure is after WP4a.3b installation; that does not prove the exact first operation at which proc access became invalid. Whether the old physical inode was unlinked versus hidden by UnionFS-specific dentry/copy-up behavior cannot be read back from the inaccessible descriptor. There are no current `.wh.*` entries in the inspected app directory, which does not prove no historical whiteout occurred. No replacement was repeated to reproduce the kernel behavior.

Kernel is 2.6.26.8-rt16-332-g5849bfa; root is `unionfs dirs=/mnt/storage=rw:/=ro`. Proc stat/status/cmdline/maps/mountinfo/fd and exe readlink were collected. The shell `stat` utility is absent; native fstat is available only where a descriptor can be opened. These limitations are explicit rather than collapsed into a generic UnionFS explanation.
