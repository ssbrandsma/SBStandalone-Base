# WP4a.2c hardware smoke test

**Deployed on disk, not yet activated in the running Jive instance.**

Existing Jive predated file replacement and was not unloaded/reloaded/restarted.
The reviewed deploy workflow has no supported live refresh; applet free() stops
native services and is therefore unsuitable under these restrictions. No new
registration/version/initialization evidence exists. Do not interpret old healthy
services or identical Meta source as proof that new Lua initialized.

| Check | Observation |
|---|---|
| Disk deployment | Three changed/new files; twenty selected hashes/modes verified |
| Base GET /health | HTTP 200, status ok, version 0.6.7 |
| Proxy GET /health | HTTP 200 |
| Web root GET / | HTTP 200 |
| Existing Now Playing | Inactive/stopped in all five samples |
| CometD | Handshake, serverstatus and players requests, disconnect successful |
| Config/native/firmware/other app audit | 67 original file records unchanged |
| TinySC | Absent throughout sampled process lists |
| Arbitration | Opt-in/latch absent; controller/coordinator not installed |
| Lifecycle HTTP | 404 on unchanged old Base; not initialized/qualified |
| New applet loading | Not activated, no runtime qualification claimed |
| Main menu/Base/Radio Browser/Spotify menus | Owner confirmed all still work after deployment; existing running UI |
| Radio/Spotify metadata and settings | Existing files preserved |
| Qobuz | Not installed; no installation or activation |
| Music/decoder/playback/scan | None performed |
| Restart/reboot/watchdog modification | None performed |

Serverstatus returns the old native server's model baby/Radio metadata despite
the verified Fab4 board. This is an inherited native 0.6.7 limitation; native
services were not replaced to correct it. A players read returns an empty item
list while serverstatus reports the existing local player; record behavior rather
than claim full LMS compatibility. Native configuration and existing settings
remain unchanged. No station or connect engine was started for these checks.

Five snapshots span 60.14 s. Boot UUID unchanged:
9162ba64-822a-46e1-8ab5-2ea3c6a66740. PID/start identities stable, no duplicate Base
helper, no unexpected exit/reset. Stable observed resource ranges:

| Process | PID/start | RSS KiB | FDs | CPU tick delta |
|---|---|---|---|---|
| jive | 3192/55166 | 33236-33236 | 50-50 | 543 |
| watchdog | 639/923 | 1748-1748 | 2-2 | 2 |
| sbproxy | 3282/56496 | 892-892 | 16-16 | 0 |
| sbbase | 3297/56611 | 340-340 | 19-19 | 3 |
| sbwebserver | 3312/56733 | 436-436 | 16-16 | 7 |

CPU figures are /proc process user+system clock-tick deltas over this window,
not guaranteed CPU budgets or a before/after performance comparison. RSS/FDs
are whole existing processes, not incremental overhead of the inactive new code.
Free RAM ranged 15580-15628 KiB, with substantial cache. USB sda/sda1 counters
remained six completed writes / 29 sectors. This is block-counter evidence, not
a complete file-content hash of the USB stick. No USB file operation was issued.

The preserved firmware watchdog retained FD1 /dev/watchdog and normal PID
supervision. No timing, feeder, keepalive slot or configuration change. The
experimental WP4a.2b preflight was not invoked; its unresolved gate was not bypassed.

Errors/limits: an initial /jsonrpc.js probe returned 404 because Base serves
CometD instead; the device BusyBox wget request probe returned 400 and -S is
unsupported. Correct HTTP framing through existing SSH direct-tcpip channels
then passed supported CometD and GET checks. No new listening socket was created.
Existing pre-deployment network timeout/crashlog resubmission lines remain;
no new Lua exception or watchdog reset was identified in captured post-copy logs.
Log windows and a one-minute process observation cannot prove indefinite stability.

The next hardware work is a separately scoped safe activation/compatibility plan,
not an automatic Jive restart, ownership/decoder probe or music playback test.

Owner follow-up: all requested menus still work; Standalone Music is not visible.
That absence is expected: this work did not install the experimental Music runtime,
enable Music, initialize a library or add/activate its browsing integration.
No further activation or playback action was taken in response to this observation.
