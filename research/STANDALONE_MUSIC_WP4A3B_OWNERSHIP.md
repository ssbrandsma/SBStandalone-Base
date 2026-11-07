# Physical ownership

Implemented locally and host-tested; no physical decoder claim; activation was refused.

`MusicProduction` composes `OwnershipRuntime`, `NativeMusicOwnership`, `OwnershipBinding`, `OwnershipClient` and `OwnershipDecoder` through the applet lifecycle. Stock Playback function identities and SlimProto.send are captured by value before producer modules load. `MusicIdleFence` refuses previously loaded Radio/Spotify modules, installs constructor fences for the qualified current sources, invalidates delayed starts and verifies tracked instances remain quiescent. Unknown hooks are never overwritten. Teardown is idempotent and keeps external fences latched for that Jive incarnation.

`admit-idle` is a distinct authenticated Music-only operation, never a synthetic Radio/Spotify/Qobuz ACK. It requires the current registered incarnation, native connection and ownership generation, no owner/request/alarm/hold, resource readiness and positively observed decoder stop on two scheduled turns with empty output FIFO/closed stream and unchanged hooks/fences. The external-unavailable mask is 14. Native process absence is 12 (Spotify/Qobuz only); Radio lives inside Jive and is proved through its actual application fence. Process absence, HELO, elapsed time and empty metadata alone cannot admit playback.

Missing IPC/bridge, existing producer, foreign hooks, malformed proof, stale generations and missing decoder quiescence fail closed. Unqualified external handoffs remain disabled. Spotify asynchronous correlated producer stop is not claimed qualified; Radio-HLS release remains unqualified. Qobuz remains absent and is not installed. One MP3 LOAD is accepted per qualified runtime; albums, add/insert, second LOAD and queue-index restarts are refused.

After native cleanup/RESET acknowledgement, Music release still requires independent firmware decoder evidence. Host Lua tests instrument the decoder and explicitly do not qualify physical audio or analogue silence.

## Final hardware outcome

The Lua factory was never loaded in a replacement Jive. The authenticated Music adapter was not registered on Fab4 and physical idle admission/grant/release was not attempted. The RAM activation marker and empty token directory were removed following coordinator refusal. The prior Radio/Spotify files were restored exactly. Existing Jive wrappers were never forcibly replaced. Host instrumentation remains software-only evidence.
