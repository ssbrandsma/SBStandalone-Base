# WP4a.1 evidence and reproduction

- [Report](STANDALONE_MUSIC_WP4A1_REPORT.md)
- [Bridge](STANDALONE_MUSIC_WP4A1_BRIDGE.md)
- [Radio](STANDALONE_MUSIC_WP4A1_RADIO.md)
- [Spotify](STANDALONE_MUSIC_WP4A1_SPOTIFY.md)
- [Qobuz](STANDALONE_MUSIC_WP4A1_QOBUZ.md)
- [Hooks/firmware/reconnect](STANDALONE_MUSIC_WP4A1_HOOK_LIFECYCLE.md)
- [Tests and performance](STANDALONE_MUSIC_WP4A1_TEST_RESULTS.md)
- [Unexecuted hardware and rollback plan](STANDALONE_MUSIC_WP4A1_HARDWARE_PLAN.md)
- [Portable measurements](STANDALONE_MUSIC_WP4A1_MEASUREMENTS.json)
- [Source/reference/artifact hashes, source lines and transcripts](STANDALONE_MUSIC_WP4A1_SOURCE_INDEX.json)

Current source is authoritative. Required WP3a/WP4 reports and actual firmware/application/native producer sources were reviewed. Prior reports and pre-existing uncommitted changes were retained. The input/final ledgers record hashes and the lifecycle map records actual source lines rather than assumed installation paths.

`ownership_bridge.c`/`ownership_http.inc` implement the production protocol, independently of test-only positive-proof routes. Normal Base links it but constructs it only with the explicit token argument. New Lua entry points have no startup callers. No scanner/DB/music menu/TinySC/new listener is activated. The Music feature gate stays OFF.

Reproduce native builds with `sh scripts/build-ownership-arm.sh all`; playback compatibility with `sh scripts/build-playback-arm.sh all`. Final playback recompilation reused unchanged WP1/WP2/WP6a archives and rebuilt all seven current binaries per platform. Static manifests record soft-float architecture flags, GCC8.4 and hashes. QEMU uses ARM926 for Baby, ARM1136 for Fab4. Both actual production-auth routes and protocol fixtures are exercised.

Host CMake uses BUILD_SBPROXY=OFF only because fresh CURL development detection still fails. Set SB_TEST_FIRMWARE_ADAPTERS=ON and sibling source cache paths SB_RADIO_SOURCE/SB_SPOTIFY_SOURCE/SB_QOBUZ_SOURCE to include all95 tests. Development-only Lupa must be importable by the configured Python; its2.8 wheel was unpacked privately, not shipped or installed system-wide. Existing Python/Lua/QEMU remain test tools, not new product dependencies.

Network tests run sequentially on existing Base fixture ports. The existing private host sync wrapper avoids a known global WSL filesystem-sync stall without changing production sync behavior. Sanitizer builds use address/undefined checks, leak detection and halt-on-error; exact instrumented/uninstrumented scope is in the test report. Existing output partial-send shim runs only inside test executable wrappers.

The Lua/native integration loads actual app/profile methods and uses the same C protocol core over a bounded test-driver transport. Instrumented producer/decoder dependencies do not establish native daemon termination or actual sound. Production HTTP authentication is tested separately, while WP4 tests exercise actual descriptor resources/RESET readiness. Host scanner/helper prepare HTTP media fixtures under QEMU; that does not qualify ARM scanner throughput.

Private `.local/wp4a1/` files are work artifacts. Relevant raw observations, successful transcripts and manifests are embedded in the portable ledgers. No new runtime library/decoder, ALSA/output rewrite, public port or LMS/TinySC/Perl dependency was introduced. Native producer binaries were not modified or run.

Unavailable producer-stop drivers, unknown firmware silence semantics and the disabled production Music bootstrap are explicit activation gates. No hardware, deployment, next work package, commit, push or tag was executed. WP4a.2 hardware execution requires the documented entry gates and explicit user approval.
