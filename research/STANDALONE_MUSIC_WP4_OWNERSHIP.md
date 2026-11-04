# WP4 physical ownership and application compatibility

Physical ownership uses the existing WP3a sb_physical_owner manager. Metadata leases remain separate. All release acknowledgements require the exact adapter incarnation, request ID, owner generation, connection and live deadline, plus producer_stopped, hooks_released and decoder_quiescent. No expired/released metadata lease, HELO, HTTP200, STMf or absent process by itself supplies that proof.

## Runtime responsibility / handoff matrix

| Owner/path | Decoder/output and control owner | Producer / metadata | Required release work |
| --- | --- | --- | --- |
| Radio StreamPlayer | original Playback/decode; wrappers around _strm/_cont/_aude/_audg and headers; firmware volumeLocal remains authoritative | ICY/HLS/resolver/watchdog/reconnect tasks; existing NP publisher | Fence station generation and every restart/standby/watchdog callback, stop producer, relinquish hooks and prove output stopped. stop() alone retains desiredStation and header wrapper context. |
| SpotifyPlayback / SpotifyService | direct decode Ogg bridge and Playback pause/resume; explicit local gain curve | native engine/bridge plus remote event polling; separate metadata client | Fence service operation/remote starts, await observed producer stop, release Playback references/hooks and prove quiescence. A stop request return is not producer termination. |
| QobuzPlayback / QobuzService | direct m/f decoder; disconnects SlimProto; wraps _streamHttpHeaders;250ms recovery timer | daemon/native proxy, generation callbacks and Now Playing | Fence daemon/recovery callbacks, await producer stop, restore originalHeaders, restore a fresh Base SlimProto connection and positively acknowledge hooks/decoder. Existing stop() clears playback but does not restore the header hook. |
| Native Music | WP3a owned_prepare/owned_command; one stream per connection; no gain/routing rewrite | existing sbwebserver descriptor capability; native STAT-confirmed NP publisher | Revoke resources and await RESET ACK, stop q, receive positive adapter decoder/hook proof, then release/ack through WP3a. Kernel read completion and safe eject remain separate. |
| Jive LocalPlayer | shared Playback and native decode/ALSA; source routing, volume/sequence and output preferences | stock player lifecycle/reconnect | Do not replace ALSA, infer silence from connection changes or force a source chooser takeover. A fresh HELO still needs a trusted quiescence boundary. |
| Now Playing leases | metadata/UI only, no decoder authority | existing120s leases and bounded app control mailbox | Claim/update/release/expiry cannot commit physical ownership. Native publisher is called only after independent owner admission. |

Read-only current-source references: `C:/Projects/squeezebox/applet/StandaloneRadio/StreamPlayer.lua` (stop915 and wrapper installation around650), `C:/Projects/SBSpotifyConnect/applet/SpotifyPlayback.lua` (start18, stop62), SpotifyService.lua, `C:/Projects/QoBuz/applet/QobuzConnect/QobuzPlayback.lua` (start61 and stop), QobuzService.lua, and collected firmware LocalPlayer/Playback/Player/SlimProto. Source hashes and exact files are retained in the evidence index. No sibling repository or installed app was modified.

## Implemented adapter seam

[MusicOwnership.lua](../applet/MusicOwnership.lua) is opt-in and is not loaded by startup. It requires injected real restart fencing, asynchronous producer-stop, hook-release and positive decoder/output probe callbacks plus a trusted nonblocking bridge. Missing functions mean unavailable, never successful no-ops. Request/incarnation/generation/connection IDs stay canonical64-bit strings. Only a matching fresh grant permits canStart; stale/repeated requests, duplicate callbacks, cancellation, timeout and shutdown cannot acknowledge a new owner. There is one pending yield and no internal timer/service. The caller drives tick using its existing scheduler.

The native fixture bridge is explicitly compiled only in sbbase_music_test and requires `--test-music-mock-adapters` plus disposable database/root markers and private IPC. Its positive-proof routes are factories for deterministic tests, not a production ownership API. These mocks do not prove existing Lua callbacks, daemon teardown or audible output are correct.

## Gates and failure behavior

The real application bindings, authenticated asynchronous production bridge/supervision, decoder/output probe and producer restart fencing still require application/hardware qualification before activation. This package deliberately does not call existing stop() methods and fabricate proof flags. Radio loopback/source-chooser behavior and Qobuz hook/SlimProto restoration remain concrete integration gates.

Requests rejected before confirmed release leave the committed queue and external playback path untouched. Cancellation/refusal/timeout grants no new decoder permission; partial cleanup may require explicit reconciliation. A committed unrelated owner change advances the controller context and rejects old Comet actions. Reconnects retain ownership quarantine until fresh proof. Owner crash or unconfirmed destruction never becomes an idle baseline. Alarm admission holds cancel takeovers and stop/revoke affected local Music; existing firmware alarm/sleep paths and external-app commands are preserved.

Actual Baby/Fab4 audio, old-kernel removal/drain timing, standby, physical volume, analogue/SPDIF and daemon races are untested by these host mocks. They are unresolved activation gates, not successful hardware handoffs.
