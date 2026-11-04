# WP4a.2d rollback and retained session

Music library remains active in the approved disposable RAM filesystem for Jive
browsing. It has not been torn down during that session. No full runtime rollback
was needed or executed after the successful scan. Availability is not proof that
the entire live hardware rollback path has been exercised.

Exact owner cleanup/restore command from repository root, trusted SSH/password env:

```text
python .local/wp4a2d/session.py rollback
```

Required artifacts: runtime-backup/originals.tar and manifest.json, candidate/plan.json
and candidate source files, all under .local/wp4a2d. The backup binds target .244,
boot/process state, original SHA-256/mode/UID/GID/mtime and original absence of six
new paths. The three archived originals are old sbbase, SqueezeCenter applet and
public TinySC init script. Hashes and the immutable archive are verified before
copy. Exact nine-file rollback was tested on a temporary host tree at every
interrupted installation boundary, including native executable/service modes.
Unknown revisions and symlinks are refused. Shared atomic-copy fault tests passed.

The restore sequence first puts the original Base entry back on disk so the
existing supervisor cannot relaunch the test wrapper. It disables RAM admission,
stops only the verified current native Base child, removes policy, and invokes
the qualified TinySC controller release only after its lease/native Music work
has ended. The existing supervisor launches original Base 0.6.7. Jive/watchdog,
proxy/web identities must remain unchanged and old /health must verify. Only then
are remaining original files restored/new known paths removed and hashes/modes
verified. The owned tmpfs is unmounted only with matching recorded mount identity;
no force unmount or recursive USB removal. If busy/changed, cleanup stops.

No Jive restart, watchdog handoff, killall, decoder takeover or TinySC intentional
start is part of rollback. Normal stock behavior may return after release, as
owner-approved. The earlier maintenance coordinator is not installed or used.

The initial 1777 refusal caused core fallback before DB/scan, not a corrupt DB.
The reviewed private-mode correction verified DATABASE_ABSENT and unchanged empty
mount before chmod/rearming. One actual scan then succeeded; no existing library
was deleted/recreated. A later crash/relaunch does not automatically rescan: the
one-shot marker sends the supervisor to ordinary Base instead.

File rollback does not unload cached Jive Lua or guarantee removal of cached My
Music menus. The current VM was not patched/reloaded in this task. If exact UI
restoration needs a restart, report that dependency rather than using an
unqualified restart. Physical full teardown/recovery remains untested; current
service health, backup hashes and host file recovery are the evidence available.
