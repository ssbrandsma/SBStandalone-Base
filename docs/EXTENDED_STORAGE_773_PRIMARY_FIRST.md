# Extended Storage: primary-first 7.7.3 layout

## Observed layout and cause

Read-only inspection of the owner-designated Radio at `192.168.1.78` found
firmware `7.7.3 r16676`, kernel `2.6.26.8-rt16`, and the following healthy
dynamic UBI0 volumes:

| ID | Name | Reserved LEBs | Data bytes |
|---:|---|---:|---:|
| 0 | kernel | 23 | 2967552 |
| 1 | cramfs | 106 | 13676544 |
| 2 | ubifs | 82 | 10579968 |
| 3 | kernel_bak | 23 | 2967552 |
| 4 | cramfs_bak | 106 | 13676544 |

All five have alignment 1, usable LEB size 129024, corruption flag 0 and update
marker 0. Controller measurements were 1014 total eraseblocks, 660 available,
zero bad blocks, ten reserved for bad blocks and minimum I/O size 2048. There
is no sbdata volume and no extended-storage or applet bind mount.

The boot command line selects `root=/dev/mtdblock:cramfs`; the device node and
MTD inventory identify the named active filesystem. Writable storage remains
`ubi0:ubifs` at `/mnt/storage`, on volume 2. The firmware rcS supports an optional
rcS.local hook. No bootloader, firmware image or factory boot code was changed.

The published checker expected the complete opposite firmware-pair order for
7.7.3, so it reported `FAIL exact_virgin_layout`. The native helper separately
reported `compatible=false`, `reason=missing_production_volume`.

## Implementation

- `scripts/storage-setup.sh` recognizes both complete 7.7.3 orders. The new
  profile keeps the original kernel/CramFS sizes; it is not the 9.0.1 profile
  with its larger active CramFS. Diagnostic output identifies the expected
  `PROFILE=7.7.3-primary-first` and reports field-level failures.
- `native/sbstorage/sb_storage_helper.c` recognizes the new order only for
  exact firmware `7.7.3 r16676`. It checks all five stock volumes' dynamic
  type, alignment, reserved LEBs, byte capacity, usable LEB size and health.
  Inventory matching now compares complete ID/name tokens: `kernel_bak`
  cannot satisfy a requirement for `kernel`.
- The native check remains read-only preliminary compatibility information.
  The shell initializer independently rechecks controller geometry, exact
  free/bad block accounting, absence of extra volumes, module/image identity,
  mount state and all existing destructive-operation gates before creation.
- The existing 7.7.2 and 9.0.1 profiles are retained. The newly measured order
  is not generalized to other firmware builds or arbitrary permutations.

## Initialization, recovery and boot audit

Only the allowlisted new volume is created: `ubimkvol /dev/ubi0 -n 5 -N sbdata
-S 521 -t dynamic`. The image writer targets only `/dev/ubi0_5`, after checking
its exact name, type, geometry and health. Initialization never renames,
resizes, deletes or writes the stock firmware volumes. Production ubifs stays
at ID 2. Existing migration, retained boot support and reboot-required handling
are unchanged.

Recovery validates existing ID 5 independently of which firmware pair has
the lower IDs. Boot activation uses `ubi0:sbdata` and the module's fixed ID-5
allowlist. It never creates or formats storage. No change to storage-boot.sh,
the authorized module or the packaged UBIFS image is needed.

## Validation

- All 12 relevant storage/recovery/library-storage host suites passed before
  and after implementation. New cases exercise both supported orders and
  reject unknown firmware, mixed pairs, duplicate backup names, missing
  fields/volumes, extra volumes, incorrect geometry, corruption and update
  markers. Refused initialization never reaches either mocked UBI writer.
- The primary-first mocked transaction covers creation of ID 5, a failed
  image update, retry without a second volume creation, migration, and two
  boot activations. Hashes of all files representing stock volumes 0..4 stay
  unchanged. Boot does not repeat formatting or disturb production storage.
- Baby ARMv5TE and Fab4 ARMv6 helper builds passed `-Werror` and static ELF
  dependency checks. Both passed the native helper fixture suite under QEMU.
- The helper suite also passed AddressSanitizer and UndefinedBehaviorSanitizer.
- Exact candidates were uploaded to a private RAM directory on `.78` and
  invoked only with `check`. Both returned success: native `compatible=true`,
  shell `STATUS=AVAILABLE`, `INITIALIZATION_SUPPORTED=1`. The diagnostic report
  selected the new profile and contained no failed checks. Module and image
  identity checks passed against the installed artifacts.
- Before/after inventories confirmed identical UBI metadata, mounts, modules,
  installed component hashes, boot ID and main Jive/watchdog identities. A
  transient watchdog child is excluded from daemon-identity comparison.
  Candidate files were removed from RAM afterward.

Reproduce the host checks with `ctest --test-dir BUILD --output-on-failure -R
'storage|sbdata|sbubifs'`. The native fixture also accepts a runner, for example
`sh tests/storage/test_storage.sh ARM_HELPER qemu-arm`.

Private observations, build logs, sanitizer/QEMU results and device receipts
are under `.local/storage-178/`.

## Delivery boundary

The support is implemented in source and qualified read-only on the actual
device. During that RAM-only qualification, persistent installed components were not replaced. The published
0.7.1 packages and catalogs were not modified. No physical initialization,
mount, module load, formatting, migration or reboot was performed. Successful
real initialization and reboot persistence for this particular Radio remain
unverified and require a separately scoped deployment/activation step.


## Subsequent owner-authorized deployment

The owner subsequently requested installation on `192.168.1.78`. Backed up the
original `storage-setup.sh` and `sb-storage-helper` bytes, ownership and modes
locally, then installed only those two tested components using staged files
and atomic renames. Replacement and post-install qualification held the
existing storage-operation lock; the transaction restores both originals on
failure. No Jive reload is needed because StorageManager executes these files
for each fresh check.

Installed files were read back and matched SHA-256:

- storage-setup.sh: `1da16760ce6e9e0cc082119cda593a3d6f1b89792292441eae89d61410e04bcd`
- sb-storage-helper: `c87bc3dcbabc124ced8dec190c84bb09eeac59cb2345347f3f50718d32b6d173`

The installed native check reports compatibility. The installed shell check
reports `STATUS=AVAILABLE`, `INITIALIZATION_SUPPORTED=1`, and a clean
primary-first diagnostic profile. Before/after snapshots confirm unchanged
UBI metadata, mounts, loaded modules, boot ID and main Jive/watchdog identities.
No initialization, module load, storage mount or reboot was performed. Existing
settings, boot helper, firmware, module and image were not replaced. The public
0.7.1 archives remain unchanged; this is a targeted development deployment.

The original files and verified receipt are in
`.local/storage-178/deploy-20261113-205059/`. Reopening Standalone Base > Extended
Storage should now expose Initialize Storage. Physical initialization and boot
persistence remain for the owner's test.


## Owner initialization confirmation before 0.7.2

After the owner reported that the deployment worked, a read-only follow-up
confirmed real initialization completed on `.78`: volume 5 is dynamic sbdata
with 521 reserved LEBs and 67221504 bytes, corruption/update flags are zero,
and it is mounted as sbubifs at /mnt/sbdata. The operation log records
successful image writing, driver loading, mounting, migration (646 files and
85 directories matched), retained boot support installation and EXIT_CODE=0.

The boot ID was still unchanged and REBOOT_REQUIRED=1 remained set. Therefore
real initialization is confirmed, but post-reboot applet bind activation and
persistence have not been independently verified. This follow-up issued only
read commands; the owner performed initialization. Release 0.7.2 includes the
same tested storage helper and setup script. Evidence is private under
`.local/release-072/owner-device-followup.txt`.
