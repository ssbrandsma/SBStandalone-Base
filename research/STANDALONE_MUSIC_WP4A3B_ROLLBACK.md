# Rollback

The final physical deployment was rolled back after coordinator refusal; see the actual file-only rollback result below. Host tests execute the generated recovery script against the real native copier in an isolated filesystem and verify partial-copy rollback, unknown revision refusal, current role/listener receipts and the recovery wrapper's once-only behavior.

The fresh current-file snapshot preserves ownership, permissions, hashes and timestamps of all affected files and audits the retained executables/configuration. The candidate stages every original and the complete recovery before the first replacement, uses expected-current-hash atomic copies, and restores only recognized revisions on file-copy failure. Unknown changes are never overwritten.

Coordinator recovery first quiesces the recorded Jive/services under watchdog supervision, restores the exact saved browse Music/helper/Lua/app sources, removes the qualification load priority and retains the qualified maintenance primitives and TinySC quarantine. The saved browse ELF accepts only library.db. Its once-only RAM recovery wrapper therefore renames the closed original library.db to library-before-wp4a3b-recovery.db (without deleting either older backup) and reconstructs the same known 11-track album into library.db. The qualification DB library-wp4a3b.db is separate and retained for diagnosis. No database is written to internal flash.

Recovery is a single bounded attempt, not an automatic retry of playback. Failure leaves explicit supervised FAILED_HELD; it must never be bypassed with raw kills/reboot or fake UI evidence. External constructor fences stay latched for their Jive incarnation; restoring ordinary external application behavior requires the qualified recovery path, not an unqualified handoff.

## Actual file-only rollback

The native coordinator refused before any Jive stop/handoff. After confirming the unchanged Jive/Base/watchdog identities, the empty qualification marker/token directory were removed. A verified RAM copy of the native helper restored every affected original/absence via existing expected-hash atomic primitives. The on-disk helper/actions/load-priority files that were originally absent were removed using the manifest. The RAM helper was then removed. TinySC policy/guard files and existing database files were not changed.

All original target hashes, modes and ownership verified; original library checksum and current process identities are unchanged. Five browse categories return the expected data. Neither the coordinator's Jive recovery nor its once-only DB reconstruction was needed or run. The staged persistent recovery is retained, as are the complete local original/candidate/receipt logs. The consumed REFUSED hardware attempt is preserved; no automatic retry is permitted.
