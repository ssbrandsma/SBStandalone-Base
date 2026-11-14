# Extended Storage dynamic layout qualification

Date: 2026-11-14. Release refresh: 0.7.2, as explicitly requested. No physical flash mutation.
Publication details and artifact hashes are recorded in packaging/releases/0.7.2.json.

## Original rejection and implementation

The original `scripts/storage-setup.sh` required a firmware-specific stock size table,
including 106 LEBs / 13,676,544 bytes for `cramfs_bak`, and required
`available + bad == 653` on 9.0.1. The reported device instead has 98 LEBs /
12,644,352 bytes and `660 + 1 == 661`. Those eight LEBs account exactly for the
extra capacity. The original script, extracted from Git HEAD, rejects the complete
reported regression fixture; the corrected script accepts it.

`native/sbstorage/dynamic_layout.h`, included by `sb_storage_helper.c`, is now the
read-only inventory and allocation authority. `validate-layout` is used by native
compatibility/prepare checks, shell eligibility, analysis, and the initializer.
`validate-target` supplies the shared existing-sbdata metadata checks used by
status, recovery, repair, mounting and boot activation. The old duplicate shell
stock tables and fixed free/bad totals were removed, along with the native
primary-first-only geometry validator. Existing external status keys and high-level
reasons are retained; `DYNAMIC_LAYOUT_COMPATIBLE`, `INITIALIZATION_DETAIL`,
`LAYOUT_SIGNATURE` and `MODULE_RUNTIME_CHECK` add precise context.

The firmware identity is the bounded first line of `/etc/squeezeos.version`.
Real SqueezeOS files append builder/date/source information. Missing, malformed,
negative, overlong or overflowing mandatory numeric metadata fails closed.
Invalid/unread values are not reported as measured zeroes by the native planner.

## Supported size policy and strict constraints

New allocation remains restricted to Baby, ARM926/ARMv5, kernel
`2.6.26.8-rt16`, and qualified firmware `7.7.2 r9663`, `7.7.3 r16676`, or
`9.0.1 r17084`. Both primary-first and backup-first complete A/B pairs are supported on
7.7.3 and 9.0.1; the 7.7.2 ordering remains unchanged; arbitrary permutations, extra UBI devices, extra volumes,
duplicate/missing/wrong names and occupied target identities are rejected.

All stock volumes must be dynamic, alignment 1, usable LEB size 129024,
noncorrupt and not being updated. Both kernel roles stay at 23 LEBs and production
UBIFS stays at 82. Running CramFS stays at the qualified firmware size (106 for
7.7.x, 113 for 9.0.1). There is no independent evidence here to broaden those
boot-critical running-volume constraints.

Backup CramFS uses its discovered positive capacity rather than an allowlist of
98 and 106. Its allocation must not exceed the qualified running CramFS capacity.
A read-only CramFS superblock check establishes that the declared image length
fits the allocation, has the supported little-endian magic/signature/feature mask,
and has a directory root inode. Replacement headroom must allow the backup to
grow to the running CramFS allocation after creating sbdata. This bounds the new
size policy by the filesystem format, current qualified firmware, actual volume
mapping, and maintenance capacity rather than invented image-size thresholds.
It does not authenticate firmware contents or certify that every file in a backup
is bootable. The dynamic-volume health flags never established that guarantee.
No backup filesystem is mounted during validation.

The validator reads the existing MTD character device, preferring the legacy
`/dev/mtd/Nro` then `/dev/mtdNro` forms, with read-only opens and checked MTD
major/minor identity. It never creates device nodes. Rooted fixtures may use
regular files. The physical UBI partition must be mtd1, size `0x07ec0000`,
erase size `0x20000`; UBI's `mtd_num` must be 1. All stock gluebi mappings are
cross-checked against actual name, capacity and logical erase size. Logical maps
are never counted as additional physical flash. RedBoot geometry stays fixed.

## Kernel accounting evidence

Inspected primary source: the locally reconstructed Logitech Linux 2.6.26 tree
from SqueezeOS source revision `bad080aecfec8226a4c1699b29d32cbba4ba396b`, using
the existing `tools/kernel/prepare-logitech-kernel.sh` workflow. Relevant sources:

- `drivers/mtd/ubi/build.c`, `dev_attribute_show`: total means good PEB count;
  available is `avail_pebs`; `reserved_for_bad` is `beb_rsvd_pebs`; `mtd_num`
  identifies the attached physical MTD device.
- `drivers/mtd/ubi/vtbl.c`, `init_volumes`: dynamic `used_bytes` equals reserved
  PEB count times usable LEB size. The internal layout volume reserves two PEBs.
  Static volumes have different semantics and are rejected by this policy.
- `drivers/mtd/ubi/wl.c`: `WL_RESERVED_PEBS` is 1.
- `drivers/mtd/ubi/eba.c`: `EBA_RESERVED_PEBS` is 1; the bad-block reserve is
  deducted separately from allocation capacity.
- `include/linux/cramfs_fs.h` and `fs/cramfs/inode.c`: on-disk length, signature,
  feature mask and root inode are filesystem metadata, not a fixed 106-LEB limit.

For this qualified kernel the invariants are:

```
good + bad = physical UBI partition size / physical erase size = 1014
sum(all actual volume reservations) + available + reserved_for_bad + 2 + 1 + 1 = good
```

The reserve of 10 bad-block PEBs remains required by the existing qualified Baby
policy. The difference 14 is therefore derived as 10 + 2 + 1 + 1 for this policy;
it is not a universal UBI constant. No bad blocks are counted twice and none of
the internal reservations is subtracted again from `avail_eraseblocks`.

## Allocation and image qualification

The minimum of 600 is the pre-creation free-capacity requirement. The existing
fixed 521-LEB target and minimum remaining 79 LEBs are preserved. Allocation is
qualified against the live available count; it does not consume every free LEB.
An additional check preserves backup replacement headroom within the remaining
capacity. No new generic margin or volume expansion policy was introduced.

Reported fixture: stock sum 339, good 1013, available 660, bad 1, internal 14.
`339 + 660 + 14 == 1013`. The plan is ID 5, name sbdata, dynamic, 521 LEBs,
67,221,504 bytes; 139 allocation LEBs remain. Current raw free capacity is
85,155,840 bytes (81.21 MiB), not measured usable UBIFS space.

The unchanged pinned image is generated by mkfs.ubifs 1.3 at mtd-utils commit
`bb56df1e1a84304ec4a14169e4cdc41116ed4256`:

```
mkfs.ubifs -r EMPTY_DIR -m 2048 -e 129024 -c 521 -x lzo -o sbdata-empty.ubifs
```

Its inspected superblock is w4/r0, LZO, min I/O 2048, LEB 129024, initial
leb_cnt 14 and max_leb_cnt 521. Initial mounting can grow this filesystem within
that maximum. Image verification pins both the complete SHA-256 and byte length;
no incompatible image was generated. Creation and destructive reinitialization
retain the 521-LEB policy. Existing target metadata can qualify allocations
14..521 LEBs for nondestructive reuse; kernel mounting still validates the actual
filesystem. Larger targets and allocations too small for the bundled image are
not newly qualified. No healthy filesystem is automatically rewritten.

## Additional 9.0.1 A/B-swapped feedback

Follow-up feedback identified a gap in the first 0.7.2 candidate: the shared
validator still limited 9.0.1 to primary-first order. It now recognizes the entire
backup-first pair (`0=kernel_bak`, `1=cramfs_bak`, `3=kernel`, `4=cramfs`) using
the same role-based policy as primary-first. Running CramFS remains exactly 113
LEBs on 9.0.1 whichever slot holds it; backup capacity is discovered and checked
against its own mapped MTD and CramFS header. Production UBIFS remains at ID 2.
Mixed pairs and arbitrary role/ID assignments remain rejected. The native
compatibility prefilter now agrees with the authoritative validator for both
9.0.1 orders; boot and shell initialization do not depend on the active pair's IDs.

The new `baby_9_0_1_r17084_ab_swapped_113lebs.json` fixture has two 113-LEB
CramFS volumes. With the unchanged 23/82/23 kernel/UBIFS roles, the stock sum is
354; `354 + 645 + (10 + 2 + 1 + 1) == 1013` good PEBs, with one bad PEB making
1014 physical PEBs. This explains the quoted free-space figure without a new
free-space fingerprint. The 521-LEB plan leaves 124 free LEBs. The bad PEB is not
allocatable capacity and must not be added to the reported 645 free LEBs.
The complete real inventory was not supplied with that feedback: both 113-LEB
capacities in this synthetic fixture are an accounting-consistent interpretation,
not a claim that the forum user's flash contents were captured or validated.

`install_enabled=false` describes the permanently disabled legacy native
`sb-storage-helper install` command, not the applet's current initialization gate.
`StorageManager.lua` starts `storage-setup.sh initialize`; that explicit transaction
already executes the guarded ubimkvol/ubiupdatevol procedure when all prerequisites
pass. Enabling a second native write path would bypass this architecture. The
native output now adds `install_scope=legacy_native_command` and
`initialization_frontend=storage-setup.sh`, and its refusal message names the real
workflow. A regression proves that the native install remains disabled while the
explicit shell frontend reaches only the mocked ID-5 sbdata creation call for the
645-free-LEB layout. Neither diagnostics nor checking this flag enables writes.

Additional cases cover backup-first 98/106/113 capacities, 599/600/601 boundaries,
strict running-volume size, mixed roles and inconsistent accounting. The original
98-LEB primary-first fixture remains eligible. A forum tester should first run the
updated read-only analysis; physical first-time initialization still needs its
separate device-specific approval and prerequisite verification.

## Module, readiness, boot and recovery

The unchanged authorized module is ELF32 little-endian ARM, with vermagic
`2.6.26.8-rt16 preempt mod_unload modversions ARMv5`, 149 required symbol CRC
records and the isolated `sbubifs` filesystem. The exact 199119-byte file is
SHA-256 pinned. This is combined with the existing qualified hardware/kernel/
firmware allowlist, not just the kernel release. The reconstructed kernel enables
CONFIG_MODVERSIONS. Production UBIFS is never unloaded or replaced.

Read-only checks do not insert the module. `MODULE_RUNTIME_CHECK` explicitly
reports deferred activation or an already registered filesystem. Runtime loading
and symbol-version rejection remain inside the existing authorized initialization
procedure. Eligibility is not a claim that runtime insertion or boot persistence
has been exercised on a virgin physical device. Modified kernels retaining stock
version labels are outside this qualification; there is no new cryptographic live
kernel attestation mechanism.

A missing driver or retained boot helper is normal before first initialization.
The initializer checks the existing rcS.local hook, pins module and image, creates
only sbdata, writes its image, loads sbubifs, mounts it, copies/verifies applets,
and retains the existing boot hooks. Status remains separate from actual ACTIVE
mount/bind state. Boot activation does not create or format volumes. Existing
native multi-UBIFS activation/repair behavior remains separate from legacy loading.

Existing storage can be healthy/active even when `/dev/ubi0_5` is absent: sysfs
and verified name-based mounts identify the volume. Destructive operations still
require its device node. Controller and target character-device major/minor
numbers must match their authoritative UBI sysfs `dev` values; stale nodes pointing
at another volume are rejected before a writer can run. Reinitialization eligibility additionally requires the
full live layout validation, exact creation-size policy, module/image checks,
unmounted target and applet tree, no pending reboot and no operation lock. Existing
confirmation flags, UI data-loss warnings and recovery transaction backups remain.
Recovery continues to validate the target through the shared validator and uses
read-only mounting, staging, a retained previous applet tree and existing rollback
handling. It never reallocates firmware volumes. Upgraded existing installations
can use target-only nondestructive recovery/activation without pretending to be a
qualified virgin firmware profile.

## Mutation boundary and interrupted operations

Status, native planning and analysis read metadata and file contents. The shell
checker only refreshes its bounded diagnostic files/syslog in RAM; it never
creates/updates/removes/resizes a volume, loads a module, mounts a filesystem,
changes boot configuration, or installs a bind mount.

The existing mkdir operation lock serializes initialization/reinitialization.
Before the sole create call, the initializer re-reads the platform, complete
inventory, health, MTD mappings, accounting, mounts and capacity plan under the
lock, compares the fingerprint, and re-pins module and image. The only create
command remains `ubimkvol /dev/ubi0 -n 5 -N sbdata -S 521 -t dynamic`.
After creation it verifies target identity/type/capacity/health, the exact change
in free capacity, the full layout, and every stock ID/name/size before reaching
`ubiupdatevol`. Mount state and image identity are checked again before update.

No stock volume is resized, removed, renamed or written; the physical UBI
partition, attachment, MTD table, RedBoot and production filesystem are untouched.
There is no raw NAND write or erase path. ID/name collision is rejected by the
live inventory and UBI's exclusive-ID creation semantics. The helper lock cannot
serialize unrelated privileged firmware tools; such concurrent maintenance must
not be started during initialization. Independent external mutations between
individual sysfs reads and the ioctl remain an inherent interface limitation.

Creating sbdata changes flash allocation; writing its image destroys previous
sbdata contents when explicitly reinitializing. These are authorized CLI/UI
operations, never side effects of installing the validator. If interrupted after
creation, the target is treated as existing storage, not deleted or silently
recreated. Existing explicit recovery/reinitialization paths and applet backups
remain available. Boot failures retain the existing fail-open stock applet path.
This task did not test a real interrupted flash transaction or a reboot.

## Demonstrated tests and hardware evidence

- 15 selected storage/platform/installer/library suites pass, including the new
  dynamic fixture suite (157 cases), existing initialization, reinitialization,
  boot, firmware recovery, updater planning and Lua state/menu/manager coverage.
- Four deployment cases prove default immutability, exact-version replacement
  authorization, archive backups, and restoration of matching archive/catalog
  bytes after restart failure. Together with the selected regressions, 22 host
  suites pass.
- Six additional firmware-input/bridge, Native Base backpressure/partial-write,
  applet deployment/permission and lifecycle suites pass. The Base socket tests
  require sequential execution because they share a port; the deployment fixture
  requires root ownership. Initial parallel/non-root failures were rerun with
  those existing environmental prerequisites and passed.
- The native fixture suite and dynamic suite pass AddressSanitizer and
  UndefinedBehaviorSanitizer. Positive layouts update all dependent metadata;
  boundaries include 599/600/601, image capacity limits, backup bounds and
  firmware replacement headroom. Negative and mutation tests intercept forbidden
  operations and stop before an image write if a stock volume changes.
- Original/reference 106 and reported 98 backup allocations pass; other bounded
  CramFS allocations and varying bad-block counts pass without size allowlisting.
  The reported fixture includes real pinned module/image files and synthetic
  CramFS format evidence plus mtd_num=1. Those extra prerequisites were not in the
  user's original log, so the log alone is not asserted to prove eligibility.
- Changed helper builds pass -Werror for static ARMv5TE Baby and ARMv6 Fab4, with
  QEMU native-fixture passes and ELF/no-dynamic-dependency checks. The release refresh builds the complete Baby/Fab4 packages from Git HEAD plus
  the scoped storage/release changes in an isolated source tree. Unrelated
  working-tree experiments are excluded.
- The separately authorized development Radio was identified from its stored
  device identity and examined with RAM-staged candidates. It runs 7.7.3 r16676,
  primary-first stock volumes, backup CramFS 106 LEBs, good 1014/bad 0, and an
  already active 521-LEB sbdata with 139 free LEBs. Native layout and compatibility
  checks pass; shell status is ACTIVE, with initialization/reinitialization
  disabled. Before/after UBI metadata, MTD table, mounts, modules, boot hashes and
  installed component hashes are identical. Candidate RAM files were removed.
  No module load/unload, mount/bind, reboot or flash write was performed.

Private receipts, raw hardware inventory, original/corrected fixture diagnostics,
image inspection and module metadata are under `.local/dynamic-layout-evidence/`.
Sanitizer and CTest logs are under `.local/dynamic-*.log`. The physical Radio does
not have the reported 98-LEB layout; that variant is covered synthetically.

## Release and any later physical initialization

Release packages and public-URL repository XML are under `dist/`. The final
archive hashes are recorded in `packaging/releases/0.7.2.json`; normal static
ELF files and build manifests are under `build-arm/{baby,fab4}/`. Full isolated
build and publication receipts are under `.local/release-072-dynamic/`.
Applet/helper/package versions are 0.7.2.
The original qualification used local review catalogs only. The subsequently
authorized 0.7.2 refresh updates the live/public catalogs and retains rollback
backups of the previous same-version archives and metadata. Radio flash and
installed applets are not modified by server publication. Module, image and dependency versions remain
unchanged. No automatic storage migration is added.

The authorized Radio already has active storage, so there is no appropriate
first-time initialization experiment to perform there. A future experiment needs
separate approval for an identified virgin development Radio and a fresh live
plan. For a device matching the supplied fixture, the proposed plan is ID 5 /
sbdata / dynamic / 521 LEBs / 67,221,504 bytes, 139 free LEBs remaining, plus
UBI's already retained 10+2+1+1 internal reservations. Stock volumes would be
snapshotted and preserved, pinned module/image and rcS.local support rechecked,
and post-creation identities verified before image update. Boot recovery follows
the existing fail-open and retained-backup procedures. No automatic volume removal
is offered as rollback; deleting or formatting sbdata would require a separately
reviewed, explicitly authorized data-loss operation. Reboot validation needs its
own qualified procedure and authorization. No authorization for the forum user's
device is inferred from the synthetic fixture.
