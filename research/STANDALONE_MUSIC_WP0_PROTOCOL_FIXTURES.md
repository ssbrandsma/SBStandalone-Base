# Standalone Music WP0 Protocol Fixtures

All new fixtures are reconstructed reference data or synthetic errors, never runtime captures. [protocol.json](../tests/music/fixtures/protocol.json) includes classification, installed-source paths, source hash and line anchors. Phase 0 source ledger remains historical and unchanged; the WP0 evidence index records current source revisions separately.

## Executed fixture coverage

| Area | Classification | Fixture/test and assertion |
|---|---|---|
| HELO | Source-derived expected behavior | Installed `jive/net/SlimProto.lua:108`; device/revision/MAC/UUID/reconnect bits/counters/locale/capabilities; 54-byte example including capabilities, 36-byte fixed prefix |
| STAT | Source-derived expected behavior | Same file:167; 51-byte SqueezePlay payload with STMs and encoded buffer/time fields |
| RESP / DSCO | Source-derived expected behavior | HTTP header bytes / disconnect reason wrapped in player frame; decoder/disconnect semantics remain future controller work |
| strm-s | Source-derived expected behavior | Same file:271; server frame length/opcode, 24-byte command fields, codec, port/IP and HTTP header; resource path is an illustrative future Base path |
| Fragmentation/coalescing | Synthetic boundary/error case | Every prefix of each complete client frame returns incomplete; concatenated frames decode one by one |
| Payload/frame limits | Synthetic boundary/error case | >65536 payload rejected; generic framer accepts a zero-byte HELO/STAT payload; semantic minimum enforcement is explicitly pending WP3a |
| Stream epoch and terminal replay | Synthetic boundary/error case | test_contracts rejects wrong epoch, repeated EOF, events after terminal result and stale owner/connection acknowledgement |
| Player disconnect/reconnect | Existing host-harness behavior plus synthetic generation cases | Existing comet connection/server-switch tests and contract connection-generation rejection; no physical player run |
| Typed arguments | Synthetic boundary/error case | Duplicate/bad tag keys, nested arguments, excessive bytes/count, missing commas, trailing commas, leading-zero numbers, invalid Unicode/NUL, surrogate pair, oversized message |
| HTTP dispatch | Existing behavior regression plus synthetic errors | Existing date/installer/menu replies remain successful; unknown/unsupported commands false; argument words cannot select play/menu/status; mixed batches stay isolated |
| CometD lifecycle | Existing host-harness behavior | Existing handshake/connect/reconnect/disconnect, subscriptions/cancellation, Now Playing status and metadata, firmware upgrade/install, alarm/menu tests rerun unchanged |
| My Music nodes | Source-derived expected behavior | BrowseLibrary getJiveMenu:604, Jive myMusicMenu:2337; id/node/text/weight/actions.go |
| Artists/albums/tracks/genres/folders | Source-derived expected behavior | BrowseLibrary:1013/1288/1487/1166/1677 plus XMLBrowser _makeAction:1614; reduced count/offset/item_loop/base actions |
| Play/add | Source-derived expected behavior | playlistcontrol cmd:load/add; commonVariables -> commonParams, play nextWindow nowPlaying, add preserves browse window |
| Player playlist fields | Source-derived expected behavior | Control/Queries.pm statusQuery:2992; mode/time/duration/index/count/loop/repeat/shuffle |

`test_wire_fixtures.py` exercises existing native protocol.c via a temporary host shared library and validates source-derived menu action merging. `test_dispatch_http.py` exercises the real host sbbase; it explicitly verifies future handlers are unsupported and no My Music node or /music stream route is exposed. It does not serve the future golden responses. No native playback controller is activated.

## Typed dispatch compatibility policy

`sb_lms_command_parse` is allocation-free and reuses `np_lms_arguments` for decoding. An independent bounded grammar pass rejects malformed JSON first. Message limit 16384 bytes, at most 24 command/argument slots, at most 511 decoded bytes per slot, player 63 bytes. Batch grammar validation permits the existing 131072-byte HTTP body limit, depth 16, 4096 values and 32 keys per object. Duplicate JSON keys, escaped/non-ASCII protocol keys and NUL are rejected; UTF-8 values and valid escaped surrogate pairs are supported. Protocol keys are literal ASCII; this is a deliberate bounded protocol subset, not a general JSON document library.

Each command name is matched exactly. Arguments and tagged parameters are decoded explicitly; tags require nonempty ASCII alphanumeric/underscore/dot/hyphen keys, duplicate keys are rejected, values can contain colons and Unicode. Existing handler-specific validators still own value ranges and required argument arity. Playlist support is honestly limited to the existing relative index next/previous path: absolute index, repeat/shuffle/edit operations are unsupported. JSON-RPC and text CLI endpoints remain absent; WP0 does not implement them.

Known implemented command families are enumerated in lms_command.c. `jiveblankcommand` remains an explicit compatibility no-op because existing alarm UI emits it. Future music/library/native-control commands return `unsupported_command`, other names `unknown_command`, structurally invalid requests `malformed_command`, and oversized per-command input `command_too_large`. Comet errors preserve the ack/reply-channel shape with successful false and a data error. Invalid outer JSON returns HTTP 400 with malformed_json; oversized HTTP payload retains existing 413 behavior. Existing handler-level alarm/sleep/query errors retain their current response semantics.

Envelope/subscription connection machinery and legacy reply-channel extraction are deliberately retained rather than broadly replaced. Strict whole-body JSON validation precedes dispatch, and command selection no longer scans arbitrary argument/reply strings. A later envelope refactor still needs exact typed channel/ID extraction and bounded outbound queues; do not assume this WP0 layer is a complete general-purpose Bayeux implementation. Unknown commands formerly receiving empty success now receive deterministic errors, as requested. Disabled music adds no route, menu or capability.

## Remaining reference/device gates

The following are specifications for later tests, not passed hardware cases: real Fab4/Baby HELO capability negotiation, short semantic STAT rejection, HTTP failure reason mapping, contextual EOF versus audible drain, actual output restart/underrun behavior, physical reconnect correlation and every application handoff. Stock UI rendering/pagination/search/input behavior needs a controlled Jive session. Reduced fixture IDs/text are invented; optional LMS fields and exact type quirks need extension against installed source before enabling those handlers.

Run `ctest --test-dir build-dual-tests -R music --output-on-failure` after the documented build. See [test results](STANDALONE_MUSIC_WP0_TEST_RESULTS.md) for baseline/regression totals and host-versus-device limits.
