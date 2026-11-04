# WP4a.1 Spotify adapter

Sources: `C:/Projects/SBSpotifyConnect/applet/SpotifyPlayback.lua`, `SpotifyService.lua`, `SpotifyConnectApplet.lua`, new `SpotifyOwnership.lua`. Current source-line matrix is in the evidence index. Existing native engine/bridge/supervisor code was inspected read-only.

The opt-in gate precedes Ogg decoder/HTTP start, pause/resume and remote volume/gain changes. Existing local volume curve and zero/mute behavior remain unchanged. Service yield increments operation, clears desiredRunning and stops start/stop/health timers. Old lifecycle callbacks fail their operation check. The watcher consumes old stream/state markers while unowned without starting audio, changing gain or publishing new playback intent. Deferred startup callbacks check binding epoch.

The ownership path does not invoke existing shell helpers on the Jive loop and does not trust lifecycleState=`stopped`. Existing timeout cleanup can remove runtime/PID files after a kill request without a correlated producer shutdown acknowledgement.

A qualified asynchronous `service.ownershipDriver:stop(operation, callback)` must stop engine/bridge, fence remote/native restarts and return a table with matching operation plus `producer_stopped=true` and `restart_fenced=true` only after verified cleanup. Process disappearance, HTTP200, returned stop and metadata lease state are insufficient. **No production driver currently meets that contract; default takeover remains unavailable.** Discovery/native services are not changed automatically.

After producer ACK, the adapter drops its own Playback reference; Spotify installs no shared wrappers. The independently captured local Playback is stopped and the qualified decoder observer must confirm before ACK. A fresh native grant permits new service/playback intent. Old markers/timers cannot resume an earlier incarnation.

All25 existing Spotify tests pass. New tests load actual Playback/Service/profile methods and native protocol for Music?Spotify, delayed controlled producer shutdown, missing-driver refusal, gain/resume/start denial, timeout and late callbacks. Instrumented callbacks are not actual librespot/supervisor termination evidence. Complete/review native producer acknowledgement and qualify discovery/reconnection/volume before activation.
