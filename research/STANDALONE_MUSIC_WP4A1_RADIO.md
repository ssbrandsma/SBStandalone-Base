# WP4a.1 Radio adapter

Current source: `C:/Projects/squeezebox/applet/StandaloneRadio/StreamPlayer.lua` and new `RadioOwnership.lua`. The evidence source-line matrix indexes start/resolver, redirect/HLS, watchdog, recovery, keepalive, stop, hook installation and ownership methods.

Explicit `createOwnership(options)`/`configureOwnership(gate)` must run before this instance installs legacy wrappers. Attaching over a playing/hooked legacy instance is refused. No startup path calls these functions.

Yield increments station generation, retains selection, clears desiredStation, stops reconnect/watchdog/audio-recovery/power-keepalive timers and cancels playlist/metadata work. Start, resume, recovery, watchdog and keepalive require a fresh gate; `_isCurrent` also checks ownership. Resolver/probe/HLS callbacks retain existing generation/token checks. The fence stops the shared decoder only when Radio actually owned playback: an idle/released Radio fault cannot stop a newer owner.

Direct producer proof requires closed Playback.stream, no pending station and no reconnect timer. HLS sessions require an explicit qualified asynchronous session-stop driver; absent driver refuses takeover. Closing an HTTP reader is not fabricated as an HLS producer ACK.

Opt-in tracking records exact originals/wrappers for `_audg`, `_strm`, `_cont`, `_aude`, `_streamHttpHeaders`, `_streamDisconnect` and SlimProto.send. Restore verifies every slot before overwriting anything and refuses foreign-wrapper conflicts. Legacy source-chooser interception remains only in disabled-feature mode. External LMS/source selection outside this four-owner model is an explicit qualification gate.

After producer stop and hook restoration, the qualified asynchronous decoder observer must confirm stop before native ACK. A fresh grant re-enables existing timers and restores station selection, but requires fresh play intent. Old callbacks cannot reinstall hooks or resume the old generation. Existing ICY/Now Playing, volume/output preferences and disabled-feature standby behavior remain unchanged.

Tests use actual Radio/profile methods against native arbitration: direct-stream release/reacquire, stale starts, missing HLS proof, standby hold/release/resume and wrapper restoration. All23 existing Radio Lua suites pass, including HLS/podcast/legacy module coverage. Actual buffering/watchdog/standby audio timing and HLS native shutdown remain unqualified.

Wrappers capture their binding epoch and reject retained old function pointers even after the same application receives a fresh grant. Radio permits only its explicit in-progress disconnect cleanup through an old wrapper; stale headers/disconnect/control callbacks otherwise do not call stock decoder methods. Actual stale-pointer tests pass after reacquisition.
