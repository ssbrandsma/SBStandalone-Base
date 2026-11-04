# WP3a evidence and source index

2026-11-02. Current Base source is authoritative. [Source hashes and line anchors](STANDALONE_MUSIC_WP3A_SOURCES.json) identify implementation/build/test files and reviewed sibling/firmware/LMS references. [Measurements](STANDALONE_MUSIC_WP3A_MEASUREMENTS.json) preserves exact host observations, build artifacts and log hashes. Historical Phase 0/WP0/WP1/WP6a/WP2 reports/indexes remain unchanged snapshots. No new device connection was made.

## Source-derived protocol and application evidence

| Source | Evidence used |
|---|---|
| Existing native/sbbase/src/protocol.c | Player opcode/u32 framing, server u16 framing, payload limits and discovery; reused unchanged |
| Existing main.c snapshot, nowplaying.c/.h, alarms.c, sleep.c | Blocking send loop and unchecked heartbeat, Comet lifecycle/close semantics, external metadata/control queue, alarm/sleep and server-switch behavior |
| Installed jive/net/SlimProto.lua | HELO at 108, STAT 167, DSCO 219, aude/audg 227/233, strm 271, cont 294; exact offsets and capability construction |
| Installed jive/audio/Playback.lua | Decoder-ready 320 versus audible drain 354/start 381, input flush/disconnect 740..782, headers/reconnect 824..848, strm 852..1015, pause/continuation/output enable/gain and local volume curve |
| Installed jive/slim/LocalPlayer.lua | Playback/SlimProto construction, device model identity, volumeLocal/sequence, local pause/stop and power behavior |
| Installed LMS 7.7.3 Slim/Networking/Slimproto.pm | RESP 555, DSCO 581, STAT 681, HELO 974; reason semantics, multiple STAT layouts and reconnect information |
| LMS Slim/Player/Squeezebox.pm, Squeezebox2.pm | strm fixed 24-byte packing/interval variants; audg NNCCNNN with sequence; aude two-byte enable |
| Current Radio StreamPlayer.lua and StandaloneBaseNowPlaying.lua | Direct decoder/proxy, ICY/HLS, ownsPlayback/hooks, generation/retry/stop, explicit source chooser handoff excludes loopback; metadata lease is distinct |
| Current SpotifyPlayback.lua, SpotifyStandaloneNowPlaying.lua and native/bridge/bridge.rs | Direct Ogg decoder, local gain/sequence, stop/pause/resume, honest false external control flags, separate loopback HTTP Ogg producer |
| Current QobuzPlayback.lua and QobuzNowPlaying.lua | SlimProto disconnect, MP3/FLAC direct decoder/proxy, header hook, recovery monitor, producer generation, metadata report/release |

Private installed sources are previously collected workstation copies under .local/fab4-music-research/device, not runtime dependencies or new captures. Existing upstream comparison/firmware ledgers supply provenance. They are used for offsets/behavior only, without introducing Perl/LMS/firmware implementation dependencies. Sibling files are inspected read-only; their active behavior is not changed or claimed to be physically qualified.

## Implemented integration

- slim_controller.h/.c: bounded typed parser, validated serializers, WP0 stream guard, deterministic single-player transitions, generation guards, explicit ownership lifecycle and owned Music admission.
- output_queue.h/.c and main.c: nonblocking ordered bounded TCP output, poll writable readiness, deadline/overflow cleanup, close-after-drain, typed HELO and truthful negotiated model. Existing heartbeat bytes, discovery, server identity and external app command routing are retained.
- tests/music/test_transport.c and test_transport_wire.py: native parser/state/ownership/socket tests, original WP0 incoming fixtures, independent outgoing goldens and small host performance loop.
- tests/comet/test_backpressure.py and socket_fault.c: real disposable Base process with stopped/reading clients, complete HTTP chunks, partial writes, ordering, subscribe/reconnect/FD behavior and instrumented run. Fault shim is host-test-only.
- scripts/build-transport-arm.sh, Base CMake/build-arm.sh: existing architecture/SDK/static link flow, isolated test/archive builds, no new packaged helper or product activation.

No SQLite/scanner/storage access, HTTP audio route, playlist, Jive browsing, TinySC action or additional public port is introduced. No dependency download is required for protocol fixtures/tests. The already installed development-only clang-format tool is used only to format changed code; no runtime/parser library is added.

## Raw local artifacts and limits

Ignored .local/wp3a contains the task-input/source snapshot, pre-change main.c and binary, baseline/initial/final/retest CTest logs, host/ARM build logs, QEMU tests, sanitizer output, network/controller/client-layout JSON, ELF inspection, proxy configure failure, exact private reproduction scripts and host fixture sync wrapper. The distributed measurement ledger preserves selected values/hashes; raw workstation executables/logs are not packaged.

The initial conventional suite's storage fixture global sync hang is preserved explicitly. The private host wrapper performs real targeted sync of the disposable /tmp filesystem and does not modify production scripts or assert removable-media durability. Uninterruptible WSL mount I/O is a host environment limitation, not a controller result.

No hardware sound/performance, real physical handoff, TinySC shutdown/startup, device reset/reboot, removable medium modification, service deployment, commit, push or release is claimed. Hardware activation requires separately authorized adapter/firmware qualification. Stop after WP3a.
