# WP4a.4 — Persistent USB storage policy
Qualification date: 2026-11-06. Target: owner-designated Fab4 at 192.168.1.244 only.
This package stops at database/scanning. No audio playback, application handoff,
Jive/watchdog restart, reboot, remount, firmware change or active Base replacement.
Raw local receipts: `.local/wp4a4/`; source/artifact SHA-256 receipts are
`source-provenance.json`, `artifacts.json`, and `staging.json`.

## Supported alternative to live SQLite on old FAT

The physical USB is vfat on firmware Linux 2.6.26.8-rt16. Reliable live SQLite
journaling/locking on that stack remains unqualified. The implemented alternative
stores two **closed, checksummed SQLite snapshots** in a dedicated USB directory.
SQLite itself works on a validated private tmpfs copy. USB therefore never holds
an active WAL/rollback journal. This is persistent storage, with an explicit RAM
working-set limit; it does not provide an unlimited library or SQLite-on-FAT proof.

Configurable store leaf defaults to `.standalone-music`; allowed leaves start
with that prefix, are <64 bytes, contain no separators/traversal/control bytes,
and live directly inside the selected validated mount. Root descriptor checks,
O_NOFOLLOW, directory identity and generation checks protect every operation.
Unknown directory contents, symlinks, foreign ownership, writable-by-other modes
or conflicting directory flock refuse operation. No arbitrary absolute DB path
from a remote caller is accepted. All persistent writes stay in the dedicated
directory; temporary RAM copies are explicit. Music/cache contents are untouched.

`library.0.snapshot` and `library.1.snapshot` alternate. Each has a 192-byte,
NUL-padded header:

```
SBMUSIC1 SEQUENCE PAYLOAD_BYTES SHA256 SOURCE_HEX
```

The payload is the closed schema-3 SQLite file. Length/overflow, private regular
file/nlink, SHA-256, source identity, SQLite structure and internal media rows are
checked before serving. Hashing/copying uses 8 KiB buffers. Publication writes
exclusive `library.pending`, fsyncs, renames the inactive slot, then fsyncs the
directory. The previous committed slot remains available. A genuinely new scan
may initially have one committed slot; `backup` creates the second copy explicitly.

Persistent source ID is the first 16 bytes of SHA-256(UUID+NUL+hardware_partition+
NUL). Old Fab4 udev lacks by-id, so sysfs USB serial/vendor/product plus partition
number supplement UUID. Filesystem labels, sdX names and USB port location are
not stable identity. Actual selected device: UUID `1643-62E9`, hardware partition
`usb:18a5:0245:12440000000002DF:partition:1`.

Requested 0700 directory/0600 files are checked against the effective FAT mount
permissions; FAT does not implement Unix per-file mode storage. No chmod/remount
of unrelated USB content occurs. O_NOATIME reads prevent source access-time
writes. Free USB/RAM capacity is checked; insufficient budget fails explicitly.
RAM requires tmpfs and at least requested DB limit plus 4 MiB free; publication
checks room for the staged snapshot. Diagnostics consume bounded additional
space, so operators must account for retained recovery copies.

## Removal/reinsertion and serving

`music_snapshot_view` refreshes through the existing database worker. It uses
fresh mount evidence and exact compound identity, stages an inactive RAM file,
and reopens the one serving connection. No-change header hints avoid full copies;
new generations require full checksum/SQL validation. Removal/foreign identity
marks the cached source offline while preserving files. Reinsertion binds a fresh
generation without scanning. Busy writer or RAM limit retains the prior view;
corruption fails serving closed with bounded retries. Failed validation reopens
the prior connection. Process-visible revision stays monotonic across restores.
This behavior is host/QEMU tested; no physical unplug was performed.

Fsync success and retained generations improve crash recovery; they cannot
guarantee durability against dishonest USB controllers or whole-volume FAT damage.
No physical power-loss qualification was performed or claimed.
