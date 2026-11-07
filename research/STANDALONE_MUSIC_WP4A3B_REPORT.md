# WP4a.3b ? Production playback integration

Final status (6 November 2026): **partial completion; Gate B blocked, safely rolled back.**

| Gate | Outcome |
| --- | --- |
| A ? local production integration and software validation | Passed within the documented host/QEMU/sanitizer scope |
| B ? physical activation | Refused by the native coordinator before Jive stop or watchdog handoff |
| C ? one physical MP3 | Not entered; zero decoder attempts |

The production bootstrap, authenticated Lua Music binding, separate trusted initial-idle protocol, actual private resource IPC, MP3-only single-attempt policy and current maintenance roles/listeners/live executable receipts were implemented locally. Standalone Music remains opt-in and disabled by default. Only the StandaloneBase repository was edited in this phase; the existing fence-capable Radio/Spotify sources were reused in the qualification payload, temporarily installed and then restored. Qobuz was not installed.

Host validation: 110 full CTests passed and one Lupa check skipped in WSL, then passed separately with its required Windows runtime. Focused follow-ups cover final admission, startup, shutdown, recovery and storage-boundary changes. Eight real Lua/native composition cases, Radio regressions, all 25 Spotify tests, both ARM builds, ARM/QEMU production HTTP/bootstrap/library/scanner checks and practical sanitizer checks pass. The libcurl-dependent proxy-host suite remains excluded. The sanitized parser helper cannot run under its production address-space cap; sanitized playback used the ordinary bounded helper without relaxing limits. See TEST_RESULTS for exact scope and failed/corrected preliminary runs.

## Physical result and exact blocker

The actual Fab4 is 192.168.1.244, firmware 7.7.3 r16676. Strict existing known-host SSH was used; .245 was never accessed. The owner confirmed idle. Fresh read-only checks verified the historical boot, Jive 3192/start 55166, Base 3933/start 17600372, proxy 3282, web 3312 and watchdog 639/start 923. Native preflight passed the two Jive semaphore registrations, actual watchdog descriptor and unique current listener owners.

All 24 candidate files were installed with current-file hash checks and complete staged recovery. The initial local restart preparation then stopped on an obsolete generic 4 MiB free-space rule; no coordinator/marker/Jive action occurred in that check. The phase-specific post-install reserve was corrected to 1428 KiB (largest atomic replacement/rollback plus 512 KiB), boundary-tested and requalified; the payload files did not change.

Exactly one native coordinator attempt followed. It returned `REFUSED`, actor PID 26832/start 21598315, reason `preflight failed; no Jive stop or handoff`. Read-only action traces locate the failure at:

```
verify-exe 3933 17600372 682cff02825f5f9b6e56f9787eea4931cc8d82a77bb7a488dfcbd1bca03ad90c
```

After on-disk replacement, opening `/proc/3933/exe` on the actual old kernel/UnionFS returned `ENOENT`, although PID/start, its executable symlink and mapped executable remained present. The mapped inode was 1992. Proxy/web live images were readable and matched their before-install hashes. Therefore the required live Base hash could not be positively verified. The coordinator correctly refused; its executable proof was neither bypassed nor converted into a filename-only check. This target-filesystem behavior was not reproduced by the Linux host fixture.

No second coordinator attempt, Jive restart, reboot, decoder start or external-app handoff occurred. The trial marker and empty token directory were removed. The tested native file-only rollback restored every affected original (or original absence), with hashes, permissions and ownership verified. It did not release TinySC quarantine or replace running processes.

## Final device state

- Same boot, Jive/Base/proxy/web/watchdog PID/start identities; no session supervisor or duplicate listeners.
- Base health `ok`, version 0.7.0; TinySC fresh operational `STOPPED_VERIFIED`, count zero.
- Original 122880-byte RAM library hash unchanged: `7ef9c34cf2e63fe53904af50eb8ff21d3c02f259fa25b342af671f08f314d006`.
- Native read-only browse confirms Artists 1, Albums 1, Tracks 11, Genres 1, Music Folder 11. Track playback actions remain disabled. Physical display was not rechecked in this phase.
- Existing two older RAM database backups remain present; neither qualification nor recovery DB was created.
- USB sda/sda1 counters remain 6 writes / 29 sectors written. No new USB writes were observed.
- Production Lua ownership and resource IPC are **not active on Fab4**; the prior browse-only stack is restored.
- No physical MP3, HTTP/decoder event sequence, audible output or Music stop/release is claimed.

Actual post-rollback RSS: Jive 33236 KiB, Base 2480 KiB, proxy 896 KiB, web 452 KiB. Streaming CPU/latency/underrun measurements are unavailable because playback was blocked. The verified persistent staging backup is retained at `/mnt/storage/standalonebase/wp4a3b-backup`; overlay free space is 3516 KiB and RAM library usage remains 384/8192 KiB.

## Recommended next bounded assignment

Qualify executable provenance specifically on this UnionFS kernel before another activation attempt. Investigate capturing and retaining a verified original executable descriptor before replacement, binding it to the exact PID/start and mapped inode/device throughout the transition, and testing refusal/rollback on that filesystem. Do not remove live executable verification or rely only on a path/name. The current hardware attempt is consumed; any subsequent activation requires a new explicit assignment and fresh current/recovery receipts. Do not proceed to another audio format or handoff package.

## Documentation

The companion files share the `research/STANDALONE_MUSIC_WP4A3B_` prefix: PRODUCTION_BOOTSTRAP, OWNERSHIP, RESOURCE_IPC, RESTART, HARDWARE, AUDIO, TEST_RESULTS, PERFORMANCE, ROLLBACK and EVIDENCE. Detailed private logs and complete before/candidate/restored manifests are under `.local/wp4a3b/`. Previous phase evidence is untouched.

## Source review and reused components

Reviewed WP4A3 REPORT/PLAN and private evidence; WP4A2B lifecycle and WP4A2D browse reports; WP4A1 BRIDGE/RADIO/SPOTIFY/QOBUZ/HOOK_LIFECYCLE; WP4 OWNERSHIP/SLIMPROTO/HTTP_STREAMING; WP3B_PROTOCOL and WP6a storage contracts. Exact current implementations were inspected in `C:/Projects/SBStandaloneBase/StandaloneBase`, `C:/Projects/squeezebox`, `C:/Projects/SBSpotifyConnect`, `C:/Projects/QoBuz` and `C:/Projects/Squeezify/upstream/squeezeplay/src/squeezeplay`. Existing native owner/controller/queue/registry/database workers, Jive async transport/timers, real application ownership methods and maintenance transport/copier were reused. No new audio engine or network service was introduced.
