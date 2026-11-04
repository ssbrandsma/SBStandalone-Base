# WP4a.1 hook, decoder and reconnect policy

`OwnershipHooks.lua` records target/key/original/wrapper. Installation is idempotent for a live owned wrapper. Restoration verifies every slot, then restores in reverse order. A foreign function causes refusal without overwrite; a restored tracker cannot install again. Radio creates a fresh tracker after successful cleanup. No shared firmware/ALSA rewrite is made.

| Owner | Hook policy | Restart fence |
| --- | --- | --- |
| Radio | Six Playback methods and SlimProto.send; legacy chooser only when feature disabled. | Station generation, resolver/probe token, gate, stopped watchdog/recovery/keepalive. |
| Spotify | No shared wrappers; clear own Playback reference after producer ACK. | Service operation, startup epoch, remote marker consumption and gate. |
| Qobuz | Original header function retained once; exact restoration required. | Playback/service generation, stopped recovery/polling and HTTP callback epoch. |
| Music | Explicit injected-hook release; native resource/controller remains authoritative. | Owner/media/connection/epoch identity, RESET ACK, one stream per connection. |

Installed reference `jive/audio/Playback.lua:stopInternal` calls queued decode:stop, source-off, stream disconnect/flush and proxy cleanup. Upstream `src/squeezeplay/src/audio/decode/decode.c:decode_stop_handler` clears decoder/audio states under the audio lock and ends output. The API return and STMf do not prove completion. Source paths/hashes/lines are indexed separately.

`OwnershipDecoder.stop` requires explicitly qualified firmware, rejects capture/stop exceptions and samples through one-shot Jive timers. It requires decodeState=0, audioState=0, outputFull=0 and no Playback.stream on two distinct25ms turns. Missing fields, running/nonempty state, cancellation, changed binding epoch or deadline refuse. Default timeout2s, cap5s. **This does not measure analogue silence or establish that ALSA kernel buffers are empty.** Unknown firmware stays unavailable.

HELO/detach supplies real connection generations. Disconnect revokes admission but retains a potentially active owner. Fresh connection requires explicit cleanup/release/reacquire; no old event is relabelled with a new epoch. Music retains one stream per connection, with successful end requiring STMs+STMd+STMu. Intent is retained according to WP4, but failure cannot grant the next owner. Timeouts fail rather than retrying indefinitely.

Alarm holds compose with authenticated adapter standby holds. `setStandby(true)` holds admission and reconciles current-owner release; clearing it does not auto-play. Metadata lease expiry, process disappearance, HTTP200, HELO, returned stop or input flush never supply physical proof. Actual alarms, sleep, output timing and external source selection require hardware qualification.

Wrappers capture their binding epoch and reject retained old function pointers even after the same application receives a fresh grant. Radio permits only its explicit in-progress disconnect cleanup through an old wrapper; stale headers/disconnect/control callbacks otherwise do not call stock decoder methods. Actual stale-pointer tests pass after reacquisition.
