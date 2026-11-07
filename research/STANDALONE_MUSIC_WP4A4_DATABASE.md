# WP4a.4 — Native database lifecycle
Qualification date: 2026-11-06. Target: owner-designated Fab4 at 192.168.1.244 only.
This package stops at database/scanning. No audio playback, application handoff,
Jive/watchdog restart, reboot, remount, firmware change or active Base replacement.
Raw local receipts: `.local/wp4a4/`; source/artifact SHA-256 receipts are
`source-provenance.json`, `artifacts.json`, and `staging.json`.

## Implemented contracts

The existing WP1 serialized database worker remains the single owner of each
SQLite connection. Creation is explicit; normal production `--music-open`
opens a compatible existing library without constructing a scanner. A missing,
foreign, newer-schema or structurally invalid database is an error, never an
instruction to erase/rebuild. The default application remains Music-disabled.

Schema remains version 3. Opening checks required tables, nine named indexes,
library metadata and SQLite quick-check; diagnostic tooling additionally runs
full integrity-check. Version 1→2→3 migrations execute in one transaction through
validation, with rollback on failure. Read-only serving refuses databases that
need migration: `migrate` is an explicit operator operation. No downgrade exists.
`single_source` rejects foreign/multiple SQL media rows even in a checksum-valid
snapshot envelope. Reopening preserves track, album, contributor and folder IDs.

`SB_ML_CLOSE` is serialized after accepted work; cancellation/progress deadlines
bound SQL execution. Shutdown rejects further submissions, closes the worker and
SQLite, then releases storage descriptors. No new database work runs on the Base
protocol loop. Kernel-blocked removable-storage I/O is not a proven hard-deadline
operation; no forced cancellation of arbitrary kernel I/O is claimed.

`maximum_bytes` sets SQLite's page limit. The standalone CLI accepts 1–64 MiB;
production runtime defaults to 32 MiB. Temporary scan membership uses RAM and
must be budgeted in addition to this database limit. There is no internal-flash
fallback or old fixed 8 MiB production limit.

## Explicit native commands

```
sbmusic-library MODE MOUNT STORE_LEAF METADATA_HELPER MAX_DB_MIB
```

| Mode | Behavior |
|---|---|
| `probe` | Validate exact mount and compound identity; no database writes |
| `init` | Create a new empty schema; refuses an existing library |
| `open`, `integrity` | Read-only load, validate, query and close; no scanner |
| `scan` | Explicit complete recursive initial/incremental scan |
| `scan-detached` | Same scan with bounded private-RAM job receipts |
| `migrate` | Explicit transactional schema migration, then publish |
| `backup` | Validate and publish a second closed copy |
| `restore` | Validate the previous generation and publish it as current |
| `recover` | Explicit surviving-copy recovery preserving damaged bytes |
| `rebuild` | Scan into a new dedicated directory; never reset an existing one |

The old baseline RAM catalogue is separately supported read-only with its
explicit source ID. It is never silently reassociated with another USB medium.
