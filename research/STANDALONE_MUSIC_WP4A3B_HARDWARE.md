# Fab4 hardware

Target: 192.168.1.244 only. Strict existing known-host verification; no .245 access. Read-only identity, process/service state, application source snapshots and current-file backup have been collected. The later file installation was rolled back after the coordinator refused; no Jive restart or playback occurred.

Reconfirmed firmware 7.7.3 r16676 and boot 9162ba64-822a-46e1-8ab5-2ea3c6a66740; Jive PID 3192 and browse Base PID 3933. TinySC reports fresh operational STOPPED_VERIFIED/count zero. Now Playing is inactive/stopped; the owner separately confirmed idle. Native Spotify/Qobuz producer absence passed read-only preflight. The existing USB mount remains the qualified test source; the 8 MiB RAM database mount shows 384 KiB used and 7808 KiB available. Overlay free space was 6112 KiB.

The actual installed Radio StreamPlayer and Spotify Playback/Service sources do not provide the required fence API. Their exact originals are preserved in the new snapshot and included in the reversible candidate. Qobuz remains absent. Fresh watchdog, listener-inode, running executable, DB-integrity and media-generation checks are required immediately before any replacement.

The attempted transition was subsequently refused at live executable provenance; final verified rollback and browse results follow below. Historical IDs and empty Now Playing did not substitute for decoder admission.

## Final observed state after file rollback

All 24 payload files were restored to exact original hashes/modes/ownership or original absence. Same boot and Jive 3192/start 55166, Base 3933/start 17600372, proxy 3282/start 56496, web 3312/start 56733 and feeder 639/start 923; no sbmusic-session. Native watchdog preflight still passes. TinySC remains STOPPED_VERIFIED/count zero and Now Playing remains inactive/stopped.

Read-only original-protocol browse confirms Artists/Albums/Tracks/Genres/Music Folder counts 1/1/11/1/11 with track playback disabled. Original library SHA-256 remains 7ef9c34cf2e63fe53904af50eb8ff21d3c02f259fa25b342af671f08f314d006. All three original RAM DB files remain; usage is 384 KiB. USB write counters are unchanged (sda and sda1: six writes, 29 sectors). The physical screen was not independently rechecked. Installed originals were restored; production Music ownership/resource IPC is not active.

The persistent verified staging backup is retained for evidence; 3516 KiB overlay space remains. No factory partition, device firmware, USB files or unrelated device was modified.
