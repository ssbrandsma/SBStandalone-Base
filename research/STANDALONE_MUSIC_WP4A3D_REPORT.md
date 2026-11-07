# WP4a.3d — baseline recovery admission

Status: **INCOMPLETE — no replacement attempted**. The old Base remains LEVEL 3.
The owner authorization is understood; no further permission is needed to implement
the requested mechanism. The current implementation and rollback do not yet meet
the admission requirements. This report does not declare a Base-only transition
technically impossible.

## Findings

The Fab4 at 192.168.1.244 was observed using the established strict-known-host SSH
transport. Firmware is 7.7.3 r16676 and the boot UUID is still
`9162ba64-822a-46e1-8ab5-2ea3c6a66740`. Base 3933/start 17600372, Jive
3192/start 55166, watchdog 639/start 923, proxy 3282/start 56496 and web
3312/start 56733 remain unchanged over observation.

Base health is OK; TinySC reports fresh STOPPED_VERIFIED/count zero; Now Playing
is inactive. Original-protocol browsing returns artists/albums/tracks/genres/folder
counts 1/1/11/1/11. The active 122880-byte RAM database has schema 3, integrity
`ok`, media generation 1 and its previous SHA-256:
`7ef9c34cf2e63fe53904af50eb8ff21d3c02f259fa25b342af671f08f314d006`.
USB write counters remain six writes and 29 sectors for both sda and sda1.

## Admission blockers

1. The existing browse startup **rejects an existing database**, requires a scan,
   creates the database and runs the scanner before constructing the browse worker.
   This is true of the preserved source revision and current runtime source.
   Restarting it unchanged cannot preserve browsing from the existing library.
2. The previous rollback reconstructs the library using a scan. WP4a.3d explicitly
   forbids this. Its installed core fallback does not provide Music browsing.
   Consequently there is no demonstrated, compatible no-scan rollback artifact.
3. The installed applet's stop function is private and its exposed `free()` route
   stops all three services. Its monitor automatically retries failed service
   launches. No individually addressable Base maintenance/fence operation is
   registered. Simply changing Lua on disk does not change the already running VM.
   A tested native adapter/launcher would be necessary to coordinate its existing
   supervisor without Jive restart; none was implemented or qualified here.
4. The existing native restart coordinator stops/replaces Jive and transfers the
   watched PID. It is outside this assignment's permitted transition topology.

The instruction in §11 applies: **“If reliable rollback cannot be demonstrated
from local tests and the target topology, do not start the replacement.”** Therefore
the old process was not stopped. This is a technical admission failure, not an
approval rejection, an executable-hash exception or a request to relax scope.

## Work actually completed

Reviewed the required prior reports and relevant actual startup, applet, session,
maintenance, identity, generation and rollback sources. Collected fresh read-only
hardware and stock browse evidence; inspected a host-only immutable SQLite copy.
Added the eight WP4a.3d documents. Ran the relevant existing regression subset;
see TEST_RESULTS for the actual result. No production source changes, new recovery
coordinator, browse-only executable, qualified generation or rollback artifact were
created. These remain required implementation work, not completed deliverables.

There was no device upload, installation, signal, service restart, Jive restart,
watchdog modification, scan, database write, playback or reboot. Physical UI
responsiveness and current semaphore values were not independently qualified.

## Remaining work within WP4a.3d

Implement and test an explicit browse-only, read-only existing-library reopen mode;
build and independently qualify candidate and rollback artifacts; implement the
single-attempt Base-only supervisor adapter and detached maintenance coordinator;
exercise the requested failure matrix; run host/sanitizer/Baby/Fab4/QEMU checks;
then repeat complete target qualification before the one authorized transition.
Do not resume playback qualification from this partial result.

Companion reports: RECOVERY_DESIGN, IDENTITY, DEPLOYMENT, HARDWARE, TEST_RESULTS,
ROLLBACK and EVIDENCE under the same `STANDALONE_MUSIC_WP4A3D_` prefix. Private
evidence is `.local/wp4a3d/`.
