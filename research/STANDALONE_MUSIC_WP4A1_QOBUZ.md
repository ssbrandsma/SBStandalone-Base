# WP4a.1 Qobuz adapter

Sources: `C:/Projects/QoBuz/applet/QobuzConnect/QobuzPlayback.lua`, `QobuzService.lua`, `QobuzConnectApplet.lua`, new `QobuzOwnership.lua`. Source-line matrix is in the evidence index; daemon sources were inspected read-only.

The opt-in gate precedes MP3/FLAC start, pause/resume, recovery and gain changes. Yield increments playback/service generations, stops250ms recovery and lifecycle timers, clears desiredRunning/restartAfterStop and disables command polling. HTTP command callbacks capture the binding epoch at submission and discard after yield/reacquire or changed gate.

Producer stop requires the same qualified asynchronous driver contract as Spotify: matching operation plus producer_stopped/restart_fenced. Existing stop(false), lifecycleState or daemon/PID disappearance is not proof. **No qualified production driver currently exists; default takeover remains unavailable.**

After producer ACK, restore `_streamHttpHeaders` only if it is still this wrapper or the known original, clear wrapper/original records and own playback references, then observe shared decoder stop. A foreign wrapper causes explicit refusal without overwriting newer code. Fresh grant permits one new wrapper and restarts the existing recovery timer; original headers are captured once and wrappers do not stack.

Legacy mode retains existing SlimProto disconnect behavior. Explicit ownership mode retains the qualified Base connection on Qobuz start, keeping the owner reachable for arbitration. An already-disconnected player needs a qualified reconnect callback and fresh native tuple; failed reconnect stays quarantined. HTTP200/HELO alone cannot release the decoder. No automatic reconnect loop is introduced.

Both existing Qobuz Lua suites pass. Real-method/profile integration covers Music?Qobuz, MP3 and FLAC, recovery/start/resume/gain fencing, exact header restoration, foreign-wrapper refusal and stale operation callbacks. Daemon/cloud shutdown, actual reconnect races and analogue/S/PDIF behavior remain qualification gates.

Wrappers capture their binding epoch and reject retained old function pointers even after the same application receives a fresh grant. Radio permits only its explicit in-progress disconnect cleanup through an old wrapper; stale headers/disconnect/control callbacks otherwise do not call stock decoder methods. Actual stale-pointer tests pass after reacquisition.
