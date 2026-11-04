# WP4a.1 Fab4 hardware qualification and rollback

**PREPARED, NOT EXECUTED. Explicit user approval is required before connecting to a physical device, deployment, service changes or playback tests. Previous device credentials do not authorize this phase.** Baby follows separately after Fab4 results.

## Entry gates

Review source/evidence; complete qualified Spotify engine/bridge and Qobuz daemon asynchronous stop/restart-fence drivers. Qualify HLS session stop or leave HLS unavailable. Qualify the exact firmware stop observer and explicitly retain its ALSA/analogue limits. Assemble disabled production Music root/library/resource/controller bindings to the same owner manager. Do not deploy fixture proof endpoints/test DBs as production. Build/hash candidate and known-good rollback packages. Obtain approval for a named device, hashes, outputs, bounded window and recovery procedure. No boot-script change or automatic activation.

## Approved preflight

Record model and exact firmware build/hash; Base/app versions/configurations; current audio output/volume/digital-volume settings; local player/server and SlimProto connection; available memory/CPU; producers and storage generation. Verify independent SSH recovery and keep a second recovery session. Back up binaries/configuration/server selection without publishing credentials; verify rollback hashes and free space. Use small legally reusable MP3/FLAC and disposable qualified media. Confirm TinySC remains disabled.

Provision a private volatile owner directory0700, token leaf absent; launch only in the approved manual window. Read token after Base readiness; check0600, rotation and rejection of LAN/wrong-token callers. Redact capabilities from evidence. Do not enable at boot, alter ALSA/SPDIF, start scanning on insertion or perform safe eject.

## Initial tests, external apps idle

1. Register all adapters and inspect incarnation/generation/connection. Obtain independent explicit idle proofs; failure stops the session.
2. Verify fresh native HELO and separate decoder proof. HELO alone is insufficient.
3. Play a small MP3; verify HTTP bytes/RESP/STMs and actual sound. Stop; observe token RESET ACK and decoder stop.
4. Repeat with FLAC and short sequential queue. Test pause/resume/next/previous, duplicate terminal events and fresh connections. Measure gaps; no gapless/seek expectation.
5. Repeat on analogue and optical S/PDIF at conservative volume with existing output preferences. Record sound, clipping/underruns, metadata and local/remote volume.

## Handoffs and failures

Radio?Music?Radio, Spotify?Music?Spotify, Qobuz?Music?Qobuz. Include Radio buffering/watchdog/standby and qualified HLS refresh; Spotify remote start, delayed stop, native reconnect; Qobuz MP3/FLAC recovery/header restoration/disconnect. Confirm producer restart fencing, exact hooks, stale callbacks and final native grant.

Exercise refusal/timeout/concurrent requests/bridge loss/reconnect failure, standby/resume, sleep and a controlled alarm. Failure must not authorize the requester. External LMS/source chooser behavior outside the four-owner model is separately gated. Record unexpected transitions and stop; never force takeover or infer release.

Sample CPU/RSS, Jive responsiveness, socket/FD counts, bridge/stop/ACK/reconnect latency, queue gap and output underruns against idle/existing-app baselines. Approved unexpected removal uses disposable media only: tokens revoke, no automatic replay after reinsertion. This is not safe eject. Stop on conflicting audio, stale producer restart, missing proof, hook conflict, descriptor growth or lost recovery.

## Rollback

Hold new admission, cancel intents and request current-owner cleanup. Revoke Music resources and await verified decoder release. Restore only wrappers still owned by the adapter; never overwrite a foreign newer hook. Disable explicit feature construction and close clients; stopping Base alone is not silence proof.

Restore known-good builds/configuration and reconnect the local player to known-good Base. Verify original Radio/Spotify/Qobuz behavior, volume and outputs. If hook/decoder state cannot be reconciled, keep ownership quarantined and use the pre-approved Jive restart/device recovery procedure to rebuild known-good state. Remove a stale token only after confirming its process stopped; never reuse across incarnations. Restore original disabled state, verify TinySC remains disabled and retain sanitized evidence.

**WP4a.2 execution is not ready until entry gates are met and the user approves the named hardware session.**
