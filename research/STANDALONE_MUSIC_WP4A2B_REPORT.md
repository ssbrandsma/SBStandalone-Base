# WP4a.2b report

Date: 2026-11-04. **Incomplete: hardware preflight blocked qualification.**

TinySC lifecycle arbitration, Music admission checks, firmware overlay generation,
a detached watchdog restart coordinator, bounded recovery and an owner rollback
command are implemented. Host checks pass. They are not installed on the Touch
and must not be described as production qualified.

The Fab4 accepted the RAM helper, but restart preflight refused current Jive/IPC
keepalive evidence. Device changes stopped as required. No overlay files were
replaced, no services stopped, no Jive restart or reboot attempted, and no Music
scan/playback started there. The RAM helper was removed; original manifest hashes
and metadata still match.

| Area | Result |
|---|---|
| TinySC detection, stock stop, suppression, crash quarantine | Host passed; hardware not exercised |
| Music admission/revocation | Integrated; synthetic host regression only |
| Host CTest | 102/102 passed, 227.28 s |
| ASan/UBSan | Lifecycle 18, copy 13, coordinator 15; final rollback 6 passed |
| Firmware Lua | 19 guarded callbacks; heartbeat and syntax checks passed |
| Baby/Fab4 | Static ARM builds and emulated checks passed |
| Fab4 installation | Refused during RAM preflight, before persistent payload/replacement |
| Fab4 restart / reboot | Not attempted / not attempted |
| Radio / Spotify | Existing services/audited metadata unchanged; menus not qualified |
| Qobuz | Absent; not installed for this task |
| Rollback | Native host restore at every boundary passed; live rollback not exercised |

Host detection median 4.83 ms; verified absence 105 ms; synthetic three-service
shutdown/verification 206 ms. Worker fixture RSS 1792 KiB, two threads, six FDs.
These are host whole-process values, not ARM predictions or incremental Base
memory overhead. Details are in the test report.

The revised owner policy is respected: early firmware TinySC writes and factory
overlay loss are accepted. No `/linuxrc`, bootloader, watchdog configuration or
factory partition changes. Music stays disabled by default.

Actual Fab4 restart/reboot, operational arbitration, menus and live recovery remain
unqualified. Next: read-only firmware IPC/keepalive investigation and a reviewed
correction to the preflight model. Do not retry device changes blindly. Native
MP3/FLAC playback qualification must not resume yet. Stop at WP4a.2b.

## Documentation

- [Lifecycle and policy](STANDALONE_MUSIC_WP4A2B_LIFECYCLE.md)
- [Watchdog and blocker](STANDALONE_MUSIC_WP4A2B_WATCHDOG.md)
- [Implementation](STANDALONE_MUSIC_WP4A2B_IMPLEMENTATION.md)
- [Tests, builds and performance](STANDALONE_MUSIC_WP4A2B_TEST_RESULTS.md)
- [Hardware](STANDALONE_MUSIC_WP4A2B_HARDWARE.md)
- [Rollback](STANDALONE_MUSIC_WP4A2B_ROLLBACK.md)
- [Evidence](STANDALONE_MUSIC_WP4A2B_EVIDENCE.md)
