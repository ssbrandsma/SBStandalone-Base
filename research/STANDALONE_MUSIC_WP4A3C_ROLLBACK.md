# Rollback and preservation

No runtime replacement, Jive/service restart or persistent installation occurred in WP4a.3c; no hardware rollback was necessary. The existing installed state and process identities were preserved throughout read-only observation. The only device-side transient artifact was a verified private-RAM helper, which was removed.

New safeguards apply to later deployments: refuse an active executable target before any plan mutation; preserve old immutable generation files; refuse updates to an existing generation artifact; verify independently pinned rollback receipt and payloads; never use old-path byte copying as evidence that an already-running process has changed back. Rollback of changed processes requires a qualified service transition to a preserved generation, keeping watchdog/TinySC supervision and verifying fresh identities/listeners afterward.

The legacy copier now also refuses active targets. Its installation wrapper checks all targets before persistent staging/replacement, preventing partial Lua changes followed by late active-binary refusal. A file already matching its original hash/mode/ownership is left intact during file rollback. Unknown revisions still refuse.

Host tests cover interrupted copies, after-rename failures, unknown revisions, missing/corrupt rollback artifacts, repeated generation requests and bounded coordinator recovery. These tests do not establish a physical rollback generation for current Base: its old live bytes remain unreadable. No alternative executable is silently substituted and no lower evidence level is grandfathered.

The WP4a.3b backup and consumed REFUSED attempt remain untouched for diagnosis. A new maintenance/baseline plan must account for the volatile RAM library; a reboot or raw stop was not performed or proposed as an automatic recovery action.
