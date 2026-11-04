# WP3a SlimProto controller

2026-11-02. Implementation: [API](../native/sbbase/include/slim_controller.h), [controller](../native/sbbase/src/slim_controller.c), existing [framing](../native/sbbase/src/protocol.c). Source-derived fixtures are reconstructed data, not captured device traffic. Installed workstation firmware and LMS 7.7.3 copies are protocol/behavior references only; no code/runtime dependency is added.

## Typed input

Client framing stays opcode[4], big-endian u32 payload length, payload, with the existing 65536-byte payload ceiling. Fragmented frames remain buffered by Base; coalesced frames are consumed in order. Semantic parsing returns 1 for valid supported input, 0 for unknown opcode, -1 for malformed input, leaving the destination unchanged on errors. All methods run on one caller-owned event loop, without heap allocation, sockets, threads or SQL.

| Message | Supported layout and validation |
|---|---|
| HELO | SqueezePlay 36-byte prefix plus <=1024 printable ASCII capability bytes; also exactly 20-byte legacy prefix. Device/revision at 0/1, MAC 2..7, UUID 8..23, flags 24..25, received high/low 26..33, locale 34..35. Legacy omits UUID and shifts subsequent fields by 16. Nonzero device, nonzero unicast MAC, uppercase two-letter locale required. Reconnect flag 0x4000 retained. Unsupported 21..35-byte variants fail closed. |
| Capabilities | Entire bounded string preserved. Exact mp3/flc/ogg tokens enable only their corresponding controller codec; other tokens confer no codec support. Model <=31 and Firmware <=63 bytes; contradictory duplicate recognized fields rejected. No inference from model/device ID. |
| STAT | Exactly 51 bytes (installed SqueezePlay), 53 (error_code), or 57 (historical error_code plus four trailing bytes). Event at 0, reserved at 4..6, decode size/full at 7/11, received u64 at 15, signal u16 at 23, jiffies at 25, output size/full at 29/33, seconds at 37, voltage at 41, elapsed milliseconds at 43, echoed timestamp at 47, optional error_code at 51. Big-endian throughout. Fullness cannot exceed size. Milliseconds are authoritative; legacy shorter STAT versions are not accepted by this controller. Optional error_code is retained by the parser but no undocumented mapping is invented. |
| RESP | <=4096 bytes, complete CRLF-terminated HTTP/1.0 or HTTP/1.1 header block, numeric status 100..599, no NUL/control injection, no body or second header block. Local zero-offset stream controller accepts 200 only. Redirects/ranges/ICY are outside this future Music subset; existing Radio HTTP handling stays separate. |
| DSCO | Exactly one byte, reasons 0 normal FIN, 1 local reset, 2 remote reset, 3 unreachable, 4 timeout. Unknown reasons rejected. FIN records input EOF, never audible completion; nonzero reason produces one failed terminal result. |

Base now uses typed HELO validation before player attachment and closes invalid/repeated HELO connections. Three old host tests previously sent invented eight-byte HELO payloads (one with device zero); they now send the real 36-byte prefix and capabilities. Wire framer acceptance of zero payloads remains unchanged, and existing WP0 framing tests still test that layer separately. Serverstatus uses validated baby/fab4 model capability rather than labeling Touch as Radio; unknown model becomes unknown/SqueezePlay. Pre-HELO bootstrap uses the compiled platform. Existing firmware display version and bootstrap connected semantics are retained compatibility fields, not new firmware capability evidence.

## Outgoing serializers

All server frames reuse big-endian u16 length including opcode, opcode[4], payload. Capacity errors return zero. Payloads are explicitly initialized; no caller URL/header or arbitrary address is accepted.

| Family | Native subset |
|---|---|
| strm-s | 24-byte fixed prefix plus generated HTTP/1.0 GET /music/stream/<64 lowercase hex token>; explicit 127.0.0.1, validated nonzero u16 port, m/f/o codec, autostart 1, bounded encoded/output thresholds, zero transition/flags/slaves/replay gain. Token generation/authorization and actual endpoint belong to WP4. |
| strm-p/u/q/f/t | Same 24-byte prefix. Pause milliseconds/resume firmware jiffies/heartbeat timestamp use big-endian u32 at offset 14. q/f require zero argument. Existing production heartbeat bytes remain unchanged and now use ordered output handling. No new production start/stop/pause/resume command is sent. |
| audg | 22-byte NNCCNNN: legacy gains left/right shifted right 9, zero fixed/preamp bytes, exact bounded 16.16 gains <=65536 and sequence u32. Firmware local curve/digital-volume preference remain authoritative; serializer is fixture-only. |
| aude | Two identical boolean bytes, matching LMS output-enable form. Installed SqueezePlay reads the first enable field. No output routing change is activated. |
| cont | Seven zero bytes: no ICY interval, loop or GUID. Container-specific continuation is outside the subset. |
| codc | Not implemented: installed SlimProto.lua has no codc handler; mode and PCM fields arrive in strm. No unsupported command is transmitted. |

Byte-for-byte independent struct.pack goldens and checked-in [transport.json](../tests/music/fixtures/transport.json) cover each serializer, including endian boundaries and buffer limits.

## Event meaning and state

States: DISCONNECTED, CONNECTED, IDLE, PREPARING, CONNECTING_STREAM, BUFFERING, PLAYING, PAUSED, STOPPING, DRAINING, STOPPED, ERROR. Snapshots expose connection and owner generation, WP0 full stream identity, independent WP0 event flags, HTTP status, input reason, confirmed elapsed milliseconds, error, revision and terminal notification counter. Future integrations poll/copy snapshots and publish coalesced changes; no callback flood or queue advancement exists here.

| Event | Context and result |
|---|---|
| STMc / STMe | Input connect/establish; connected flag and CONNECTING_STREAM -> BUFFERING. Neither confirms audio. |
| STMh / STMl | End of headers / buffer full; buffering progress, not audible start. HTTP success still needs RESP. No synchronized start is claimed. |
| STMs | First audible track-start confirmation, accepted after HTTP 200. One WP0 audible event; duplicates cannot start/advance again. |
| STMp / STMr | Confirm requested pause/resume only. Unsolicited or stale confirmations cannot mutate play state. |
| STMd | Decoder-ready/no more encoded data; separate readiness flag, DRAINING, finite drain deadline. It is not audible end. |
| STMu | Audible completion only after decoder-ready AND a confirmed audible start; one drained terminal event. Without start, report protocol failure rather than successful completion. |
| STMf | Input flushed/closed, NOT proof output stopped. strm-s itself emits an initial flush, accepted before connect/response. During STOPPING it only reports input progress; explicit trusted quiescence acknowledgement is required to complete stop. Unexpected active flush fails correlation. |
| STMn | Unsupported decoder format, one failed terminal result. |
| STMo | Recoverable output underrun -> BUFFERING. No automatic decoder resume/retry is implemented. |
| STMt | Monotonic confirmed elapsed observation after audible start, with firmware jiffies ordering and wrap-aware stale rejection. No host-time position extrapolation is added. |

Prepare/start/control acknowledgements and decoder drain use 10-second caller-driven deadlines. Missing STMs, missing stop acknowledgement, or stalled drain fail once. PLAYING/PAUSED idle has no autonomous background timer. Caller invokes tick with trusted monotonic milliseconds. Player detach reports failure for an unfinished stream and revokes connection admission; it never reports successful end. Error terminal and stop/drain terminal are distinct, so a future queue can decide its policy without double advancement.

## Correlation boundary

SlimProto carries no owner/media/stream epoch. The native event API's WP0 identity scopes an already associated callback; it is not decoded from STAT. Inventing an epoch when a packet arrives on a reused socket would not reject old firmware events.

The conservative implementation therefore allows **one stream per attached connection**, with strictly increasing connection and stream generations and explicit trusted producer/hook/decoder quiescence before prepare. A second prepare on the same connection is refused even after a clean terminal event. Reconnect clears pending status/HTTP/clock state, requires new ownership/quiescence confirmation, and never auto-resumes. Old connection/owner/stream callbacks are rejected. Old physical decoder state can survive a socket reconnect: adapter acknowledgement, not HELO alone, must clear it before admission. This is deliberately unsuitable for gapless operation. Safe same-connection multi-track fences remain a WP4 integration gate, not a solved wire property.

Production Base validates HELO and preserves its heartbeat/server-switch path, but never constructs this transport/ownership controller. Typed STAT/RESP/DSCO and start serializers are exercised through native tests and QEMU, not wired into existing app playback. Future Music uses owned_prepare/owned_command plus scoped output; low-level deterministic transition methods are for the trusted integration layer/tests, never public untrusted commands.
