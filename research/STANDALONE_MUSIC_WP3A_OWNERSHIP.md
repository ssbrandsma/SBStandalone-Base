# WP3a physical playback ownership

2026-11-02. [Native contracts and implementation](../native/sbbase/include/slim_controller.h) extend the existing WP0 sb_music_ownership guards without adding another owner enum or metadata lease. [Report ownership matrix](STANDALONE_MUSIC_WP3A_REPORT.md) records the source review before implementation.

## Protocol

One caller-owned sb_physical_owner, no heap/thread/timer/listener. NONE/RADIO/SPOTIFY/QOBUZ/MUSIC remain the WP0 owners. Each non-NONE owner registers an explicit adapter incarnation. Snapshots expose current/requested owner, generation, connection, request ID/deadline, yield state and revision. A pending REQUESTED snapshot is the adapter's pull notification; a future asynchronous bridge must carry its exact request/owner/connection tuple and preserve incarnation identity. There is no production IPC/REST endpoint or implicit app registration.

1. Attach a strictly newer connection. No-owner acquisition requires an explicit trusted idle baseline; HELO alone is insufficient.
2. Request a registered owner with a strictly increasing request ID and timeout 1..10000 ms. Only one yield can be pending.
3. Current owner remains recorded; requester gains no decoder permission. During yield, normal control admission is denied; old Music may only issue cleanup stop/flush through owned_command.
4. Adapter stops producer, cancels auto-resume/retry tasks, releases/restores hooks, and confirms decoder/output quiescence. All three physical proof flags must be true, in addition to accepted=true, to commit.
5. Validate adapter incarnation, previous owner, generation, connection, request and live deadline using WP0 guards. Advance generation and publish new owner exactly once. Duplicate/stale/late replies fail.
6. Explicit release requires the same physical facts and current tuple. It increments generation, records NONE and permits a subsequent no-owner acquisition. Repeated release is rejected rather than acknowledging a stale caller.

Refusal, partial proof, timeout, requester crash or suspend cancels admission without granting the requester. Previous owner identity remains known; ordinary control admission stays closed after refusal/timeout until explicit reconciliation/release/new successful request. No forced takeover or lease-expiry shortcut exists. Caller tick handles deadlines without blocking. Crash of current owner revokes generation and leaves its physical state quarantined. A newly registered incarnation may perform explicit cleanup/release; an old callback cannot claim that proof. Recovery is not automatic silence inference.

Disconnect revokes pending handoff/control admission but retains the potentially active old owner. Reconnect uses a new connection/generation and requires current owner's explicit physical release before a new owner can acquire; confirmed_idle on reconnect cannot erase a recorded owner. Alarm/sleep admission hold rejects new takeovers and cancels pending yield without changing existing firmware playback. These are modeled policies, not replacements for current alarm or standby behavior.

## Existing application compatibility adapters

Sibling sources were inspected read-only. No active takeover path or applet is rewritten. Mock adapter registration/acknowledgement exercises all twelve directed transitions between the four real owners, including Music in both directions. This proves arbitration rules, not real producer shutdown.

| Application | Required future adapter work |
|---|---|
| Radio | StreamPlayer.stop clears ownsPlayback and cancels stream work, but explicit generation-guarded yield must also prevent desired-station/watchdog/standby restart. Existing wrappers fall through when unowned. Source chooser intentionally excludes loopback, so choosing local Base cannot serve as acknowledgement. Confirm cleanup and output stop before reply. |
| Spotify | Stop engine/bridge and Playback atomically against remote start callbacks, preserve local volume/sequence handling, acknowledge after producer and decoder release. Current metadata client advertises limited control capabilities; no extra control support is inferred. |
| Qobuz | Fence daemon callbacks/recovery timer, stop producer/decoder, restore _streamHttpHeaders wrapper, reattach SlimProto to Base and acknowledge fresh connection/quiescence. Current stop clears playback but does not restore that header hook or prove SlimProto restoration. |
| Music | Use the same manager registration/request/ack/release protocol. Check sb_owner_controls before prepare/control and use owned_prepare/owned_command. Finish physical cleanup before acknowledgement, invalidate queued resource and output generations. |

Metadata ownership, UI command ownership, HTTP producer ownership and physical decoder ownership remain distinct. np_manager lease claim/update/release/expiry still manages existing UI/metadata only. Its 32-message mailbox stays an external application control queue, not a music playlist. App positions/metadata continue through their existing publishers; native STAT handling does not override them.

Stock LocalPlayer volumeLocal, sequence handling, Playback timers, source routing, capture paths, decoder PCM and ALSA/SPDIF/analogue output remain unchanged. Source review covers standby/reconnect/alarm/sleep interactions; sound, exact drain timing, external daemon restart races, firmware reconnect and actual hardware routing require separately authorized qualification. No device-level compatibility claim is made.

WP4 must supply an asynchronous adapter bridge, trustworthy incarnation/cancellation boundaries, positive physical proof and connection correlation before enabling playback. WP3b may implement disabled browse integration independently, but must not treat a browse selection or Now Playing lease as permission to start audio.
