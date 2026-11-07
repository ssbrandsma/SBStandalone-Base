# Production bootstrap

Implemented locally and host-tested. Final hardware activation was refused; the files were rolled back.

The explicit Fab4-only RAM marker `/var/run/standalonebase/music-idle-test.enabled` selects `sbmusic-session`. The applet then supervises the existing proxy and one once-only session, rather than launching separate competing Base/web services. The session creates an inherited AF_UNIX/SOCK_SEQPACKET pair, closes unrelated inherited descriptors, starts one `sbbase-music` and one `sbwebserver-music`, and stops its peer if a child exits. There is no automatic session retry and no new public port. The explicit qualification payload contains load priority 10; normal packaging does not include that priority override.

Playback construction precedes Lua registration, which remains a distinct authenticated admission step. A real RESET/ACK establishes IPC before readiness. Scanner/SQLite/browse, validated no-atime source roots, media generations, TinySC STOPPED_VERIFIED and the private ownership token retain their existing contracts. Qualification uses only `/dev/standalone-music-test/library-wp4a3b.db`; existing databases are preserved. The runtime refuses an existing destination, arbitrary database names and unapproved roots. Music stays disabled without explicit flags/marker.

Startup failures do not advertise MP3, grant ownership, expose a capability or emit strm-s. Review private build and production-bootstrap logs for the exact tested binaries. Successful host construction/fixture observations are software evidence only.

## Final hardware outcome

The files were installed, then restored without starting the session. Native coordinator preflight refused the live Base executable hash before stopping Jive. Thus this startup path is locally implemented and software-tested, but neither activated nor production-qualified on Fab4. Zero production children/queues/resources were created on the device. Existing browse runtime remains unchanged.
