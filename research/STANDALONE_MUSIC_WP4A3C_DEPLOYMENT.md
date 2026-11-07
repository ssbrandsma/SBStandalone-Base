# Deployment generations and readiness

`music_generation.py` is an offline utility, not an installer or an activation command. It creates a fresh generation directory only; repeated writes to an existing output refuse. Each manifest pins build ID, source revision and source content hashes, architecture, static ARM file SHA/size/mode/root metadata, roles, Jive applet compatibility, listener/supervisor topology, previous generation and independently pinned rollback receipt SHA.

The generation ID is the SHA-256 of canonical manifest JSON. Expected executable paths derive from `/usr/share/jive/applets/StandaloneBase/generations/<64-hex-ID>/<fixed-name>`, avoiding a circular manifest hash. Verification rechecks the expected ID, source provenance, exact path set, bounded receipt/payload sizes, static ARM ELF constraints, file hashes and rollback receipt/artifacts. Architecture metadata must come from the qualified target build; the reused ELF validator checks static ARM, not full CPU instruction semantics. Fixture receipts do not qualify hardware.

Native fixed-role recognition and the checked copier understand canonical generation paths. Existing generation files cannot be overwritten. A current same-generation Base/web/session set must have real session parentage and listener ownership; mixed generations or a generation proxy substituted for the retained proxy refuse. Legacy mutable plans are prechecked for every active target before any persistent staging or Lua replacement. File-only rollback skips already-restored original bytes/metadata and cannot overwrite an active executable to pretend that process state was restored.

## Conditional transition plan - not executed

After positive live-byte qualification, stage only new immutable artifacts, verify complete provenance and recovery before changing applet selection, retain the old generation, then use the unchanged watchdog-safe coordinator and post-transition UI/IPC/identity checks. Never retire an old artifact before its process has exited and the new generation is qualified. Rollback selects the preserved generation and uses a supervised transition where processes changed; it is not an in-place binary copy.

Current hardware qualification failed for Base. Therefore no fresh activation bundle, generation-selection applet payload or deployment commands are issued. All utility outputs are `activation_qualified:false`. Phase J was not entered. Do not reuse WP4a.3b's consumed receipt or restart command. `readiness.json` explicitly records no prepared candidate and no authorization to resume activation.

A separately authorized maintenance entry/cold start could establish a new verifiable baseline from a checked immutable artifact. It must include the volatile RAM library and watchdog/TinySC preservation plan. It was neither performed nor declared safe by this read-only phase.
