# WP4a.1b / WP4a.2 ? hardware qualification failed; recovery performed

2026-11-03. The native scanner/database chain ran successfully on Fab4. Native MP3/FLAC playback and the production ownership bridge were **not qualified**. During the following Jive decoder-probe restart the Touch unexpectedly rebooted; the RAM-dependent startup guard disappeared and stock TinySC started. This violated the TinySC/no-USB-write qualification conditions. Further Music qualification was stopped and recovery was performed.

The restart procedure used the ordinary `squeezeplay stop` path without watchdog keepalive. This was an inadequate recovery prerequisite. A watchdog-related reset is suspected, not proven as the sole cause. The guard also needed to fail closed across a full reboot, which it did not. Neither problem is hidden behind passing host tests.

## Hardware work completed

The user replaced the Wi-Fi NIC with USB storage and approved a disposable <=8MiB RAM database. After the user explicitly authorized stopping active Spotify, the installed app's **Stop Spotify Connect** menu was used. Its UI reported stopped and supervisor/librespot/Ogg bridge disappeared. This is an application stop observation, not qualification of an application-to-Music ownership handoff.

Before copying the native scanner candidate, private-RAM atomic replacement/read-back/restoration and unmount/rmdir cleanup were exercised. An initial cleanup script running from the mount held it busy; executing the same procedure through the host-fed shell corrected this and was validated before the candidate ran. Legacy Dropbear sometimes omitted SSH exit status; commands were verified with explicit completion markers.

The scan-only production Base and metadata helper were copied to the private8MiB tmpfs, hashes checked, and explicitly scanned only the11-MP3 album folder. Result:15 files visited,11 indexed tracks,11 distinct track IDs,0 errors,3684ms scanner time,144KiB reported parser-helper peak RSS. SQLite database122880bytes, integrity_check=ok. The scan-only path exited before any protocol listener/controller/decoder construction. Its capability was removed. RAM cleanup completed; old Jive/Base services stayed running during this scan.

## Failed decoder probe and recovery

Four temporary Meta guards were backed up, hash checked and replaced atomically in the applet overlay: Base, Radio, SpotifyConnect and SqueezeCenter. No factory firmware partition was written. The probe was observational and issued no ownership ACK or Music grant. No usable decoder result survived the unexpected full reboot. The RAM marker vanished; original TinySC startup became enabled and `slimserver.pl`/`gdresized.pl` were observed. They were stopped through the stock service and confirmed absent. `.Squeezebox/cache` was newly observed on USB, empty in the bounded listing. It was **not deleted**. No claim is made that the entire USB metadata state is unchanged.

Recovery held the watchdog alive, used an unconditional temporary TinySC guard for recovery startup, restored the other original Meta files, and restarted known-good helpers/Jive in a clean sequence. Original Base's startup port check otherwise rejects helpers left running from an earlier Jive incarnation. After that incarnation loaded its guarded SqueezeCenter Meta, the original SqueezeCenter file was restored last. All four original Meta hashes, all three original native-service hashes and the Base configuration hash matched the backups. The real remote UI works again. Normal watchdog PID was restored; TinySC and Music test processes/mounts are absent in final verification. No Music feature remains enabled.

Current Jive retains the TinySC guard in memory, while original disk files are restored. **A future ordinary reboot with USB attached can again invoke the stock TinySC startup policy.** Do not repeat Music qualification until watchdog handling and a reboot-safe fail-closed TinySC policy have been qualified.

Standalone Radio recovery was exercised using NPO Radio1: the real UI browsed cached Radio Browser stations; logs show MP3 decoder start, HTTP200, ICY metadata and active Now Playing. Audible output remains unverified; the owner checked after the test had already been stopped. Custom stations showed temporarily unavailable during one UI check; their configuration hash was unchanged. Spotify playback was confirmed by the user before stopping it, not retested after recovery. Qobuz, volume/output switching and standby were not qualified.

## Local implementation

Added an opt-in native session build (`SB_BUILD_MUSIC_SESSION=ON`, default OFF): `sbbase_music`, `sbwebserver_music` and `sbmusic-session`. The production Base uses the production owner manager and authenticated bridge; it excludes mock adapters, fixture databases, fake Now Playing activation and fixture proof HTTP handlers. Shared playback glue is reused by fixture and production targets.

The explicit runtime validates a WP6a USB scope and generations, requires a fresh database on the exact private <=8MiB tmpfs, uses WP1 DELETE/FULL policy and WP2 scanner/helper, and refuses internal-flash fallback. Scanning is explicitly requested, never insertion-triggered. `--music-scan-only` exits before listeners/decoder construction. It is preparation for a bounded non-audio hardware scan, not an executed scan result.

Relative reads inherit an opt-in O_NOATIME policy, including the scanner root directory, without changing the USB mount. A failed O_NOATIME setup refuses activation. Production resource descriptors become CLOEXEC before parser-helper forks. Media validation runs off the protocol loop; revocation holds admission and invalidates browse/playback without automatic rescan/reinsertion. Radio/Spotify/Qobuz takeovers remain unavailable in this candidate.

Fixed connection adoption to avoid a duplicate owner-manager connect when attaching native playback. Music reconnect requires a fresh connection, all three independent release flags and the native resource proof callback. It cannot use HELO or process absence as release proof. Native process absence is diagnostic only.

The session launcher uses a private descriptor channel, propagates shutdown and does not restart failing children. The production web entry rejects unknown/fixture options and requires its private IPC channel. Normal binaries remain disabled for Music and no startup caller was added.

## Outcome

| Requested result | Actual result |
| --- | --- |
| Native MP3 / FLAC heard | Neither tested; no native playback stream started |
| Jive browses native music library | Not tested; old stock My Music item is not proof |
| Production ownership bridge on Touch | Not activated for playback or qualified |
| Qualified application handoffs | None |
| Native USB scanning / SQLite ingestion | Passed on Fab4,11 MP3 tracks |
| Reconnect / queue gap | Not measured on hardware |
| Stability | One unexpected reboot during decoder-probe restart; TinySC startup observed |
| Rollback | Required and performed; original file hashes/services/UI recovered |

All96 host tests passed (425.99s). Production entry refusal checks passed under Baby/arm926 and Fab4/arm1136 QEMU. Both ARM variants built static EABI5 binaries. A privileged host private-mount test indexed two MP3/FLAC fixtures, checked SQLite integrity and capability removal. These results do not override the failed hardware gate. New-runtime ASan/full positive playback qualification was not performed.

Stop after this failed Fab4 qualification. No Baby deployment, permanent Music activation or later work package was started. Next action is a dedicated review/qualification of reboot-safe TinySC inhibition and watchdog-safe restart/recovery; then repeat the real decoder/ownership proof before MP3 audio testing.

[Preflight](STANDALONE_MUSIC_WP4A2_PREFLIGHT.md), [ownership](STANDALONE_MUSIC_WP4A2_OWNERSHIP.md), [audio](STANDALONE_MUSIC_WP4A2_AUDIO_TESTS.md), [performance](STANDALONE_MUSIC_WP4A2_PERFORMANCE.md), [regression](STANDALONE_MUSIC_WP4A2_REGRESSION.md), [rollback](STANDALONE_MUSIC_WP4A2_ROLLBACK.md), [evidence](STANDALONE_MUSIC_WP4A2_MEASUREMENTS.json).

Final recovery observation: NPO Radio1 was stopped through the existing long-pause/Stop action. Final Base metadata is inactive/stopped; native services and Jive remain running, TinySC/Music are absent, and the test RAM volume is absent. The temporary authenticated remote session was logged out and its local cookie removed. The owner replied after the stop that nothing was playing and Now Playing was empty; this matches the stopped end state and does not confirm sound during the earlier sample. Radio audible recovery remains unverified.
