# WP4a.4 — Recovery and migration
Qualification date: 2026-11-06. Target: owner-designated Fab4 at 192.168.1.244 only.
This package stops at database/scanning. No audio playback, application handoff,
Jive/watchdog restart, reboot, remount, firmware change or active Base replacement.
Raw local receipts: `.local/wp4a4/`; source/artifact SHA-256 receipts are
`source-provenance.json`, `artifacts.json`, and `staging.json`.

Startup validates committed slots before trusting a generation. One invalid slot
and one valid survivor permit diagnostic read-only open with an explicit degraded
flag. Serving refuses a damaged store. Normal scan publication refuses to replace
corruption silently. Both invalid slots, unsupported schema, foreign SQL source,
missing required indexes or incomplete-only files refuse; no empty-library reset.

An interrupted `library.pending` is not a committed library. On the next explicit
publication it is renamed into one of two bounded `library.interrupted.N` files.
Existing bytes are preserved. If both diagnostic slots are occupied, mutation
refuses until an operator archives them. `recover` explicitly validates the
surviving database, quarantines damaged committed bytes and publishes a sequence
above observed header hints. This makes a corrected generation observable to the
serving refresh provider. Recovery does not scan the music tree.

`backup` validates and writes a second closed copy. `restore` selects the previous
valid generation and publishes it as a new current sequence. It cannot recover
unwritten data, and refuses when no earlier committed copy exists. Rebuild is
explicit and requires a new empty dedicated directory, preserving the old store.

Migration is a transaction over the existing v1/v2 schema to v3; validation must
pass before commit/publication. A migration failure leaves USB committed bytes
unchanged. Unsupported higher schema never triggers downgrade. Existing WP1
migration/journal tests and new snapshot lifecycle tests cover these paths.

Fault evidence is deterministic host/QEMU simulation: partial pending writes,
checksum corruption, foreign SQL media, stale generation, cancellation, failure
and reopen. It is not real power-cut/unplug qualification of the USB filesystem.
