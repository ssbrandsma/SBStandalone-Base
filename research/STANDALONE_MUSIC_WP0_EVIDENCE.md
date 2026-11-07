# Standalone Music WP0 Evidence Index

Base HEAD before edits: `0a4b50e836446bdaf03804fe1a5477a139742243`. Inspection date 2026-11-01. Current working-tree hashes below distinguish implementation from Phase 0 historical evidence. No source copies from LMS are introduced as runtime dependencies.

See [Phase 0 evidence](STANDALONE_MUSIC_PHASE0_EVIDENCE.md) for installed TinySC/firmware and sibling app provenance; [protocol fixtures](STANDALONE_MUSIC_WP0_PROTOCOL_FIXTURES.md) distinguish source reconstruction from host observations/synthetic errors. The fixture media are generated CC0 data. The [machine-readable WP0 ledger](STANDALONE_MUSIC_WP0_SOURCES.json) contains exact line text and SHA256 for each file below.

## native/common/music_contracts.h

SHA256: `dfc03df67f5dcb116ac3a84e993285857fd981103bfdc6f5528769ebc942e500`

- Line 7: `#define SB_MUSIC_ENABLED 0`
- Line 115: `int sb_music_source_equal(const sb_music_source_id *, const sb_music_source_id *);`
- Line 116: `int sb_music_media_current(const sb_music_media *, const sb_music_media *);`
- Line 117: `int sb_music_yield_ack(sb_music_ownership *, uint64_t request_id,`
- Line 119: `int sb_music_can_commit_owner(const sb_music_ownership *, uint64_t now_ms);`
- Line 120: `int sb_music_stream_event(sb_music_stream_guard *, const sb_music_stream_id *, sb_music_event);`
- Line 121: `int sb_music_queue_update_valid(const sb_music_queue_snapshot *, const sb_music_queue_update *);`
- Line 122: `int sb_music_query_valid(const sb_music_query *);`
- Line 123: `int sb_music_resource_current(const sb_music_resource *, const sb_music_media *,`
- Line 125: `int sb_music_range_valid(uint64_t size, const sb_music_byte_range *);`

## native/common/music_contracts.c

SHA256: `caa3b17ec99d83d667744d8ecc4af86ccf7d0128e4d21d3f13d28fe9f575354e`

- Line 4: `int sb_music_source_equal(const sb_music_source_id *a, const sb_music_source_id *b)`
- Line 9: `int sb_music_media_current(const sb_music_media *a, const sb_music_media *b)`
- Line 14: `int sb_music_yield_ack(sb_music_ownership *o, uint64_t request, uint64_t generation,`
- Line 25: `int sb_music_can_commit_owner(const sb_music_ownership *o, uint64_t now)`
- Line 31: `static int stream_equal(const sb_music_stream_id *a, const sb_music_stream_id *b)`
- Line 38: `int sb_music_stream_event(sb_music_stream_guard *s, const sb_music_stream_id *id, sb_music_event event)`
- Line 51: `int sb_music_queue_update_valid(const sb_music_queue_snapshot *s, const sb_music_queue_update *u)`
- Line 58: `int sb_music_query_valid(const sb_music_query *q)`
- Line 65: `int sb_music_resource_current(const sb_music_resource *r, const sb_music_media *m,`
- Line 77: `int sb_music_range_valid(uint64_t size, const sb_music_byte_range *r)`

## native/sbbase/include/lms_command.h

SHA256: `4f54a783d64c0025887bbbaffb805bc000a13852b7437da4baf1f1c9fa131cf6`

- Line 16: `int sb_lms_json_valid(const char *, size_t);`
- Line 17: `sb_command_result sb_lms_command_parse(const char *, size_t, sb_lms_command *);`
- Line 18: `const char *sb_lms_tag(const sb_lms_command *, const char *);`
- Line 19: `const char *sb_lms_command_error(sb_command_result);`

## native/sbbase/src/lms_command.c

SHA256: `922a8ec6b07f32d6c990b5f6b38bdbc55a2413bb9dcd5fd3234b37195f339e4c`

- Line 7: `static void space(cursor *c) { while (c->p < c->end && *c->p && strchr(" \t\r\n", *c->p)) c->p++; }`
- Line 8: `static int hex(unsigned char c) { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1; }`
- Line 9: `static int unicode(cursor *c)`
- Line 15: `static int string(cursor *c, int key)`
- Line 43: `static int value(cursor *c, unsigned depth)`
- Line 78: `int sb_lms_json_valid(const char *json, size_t length)`
- Line 85: `const char *sb_lms_tag(const sb_lms_command *cmd,const char *key)`
- Line 91: `sb_command_result sb_lms_command_parse(const char *json,size_t length,sb_lms_command *cmd)`
- Line 116: `const char *sb_lms_command_error(sb_command_result r)`

## native/sbbase/src/main.c

SHA256: `5c9d251945a5f07a1012537e1ebae3ff3a2b4c6e1fe4853947da7f81bdba2a07`

- Line 54: `#define SEND_WAIT_MS 2000`
- Line 67: `static int player_connected;`
- Line 68: `static int test_nowplaying;`
- Line 89: `static size_t catalog_entry_count;`
- Line 96: `static int`
- Line 103: `static void stop(int sig)`
- Line 113: `static void reload(int sig)`
- Line 124: `static int nonblock(int fd)`
- Line 136: `static int listener(int type, unsigned port)`
- Line 165: `static void close_client(client *c)`
- Line 183: `static int send_data(client *c, const char *data, size_t size)`
- Line 201: `r = poll(&pfd, 1, SEND_WAIT_MS);`
- Line 226: `static void`
- Line 253: `static void stream_start(client *c, const char *body)`
- Line 278: `static void stream_heartbeat(client *c)`
- Line 289: `static void stream_event(client *c, const char *body)`
- Line 304: `static void player_status_json(char *out, size_t cap)`
- Line 320: `static int split_playerstatus_path(const char *path,`

## native/sbbase/src/nowplaying.c

SHA256: `30eb6c674b92043d9a014c71b33f34fc1e7fc901349b647255b474d82e54a54b`

- Line 43: `static int`
- Line 62: `static void reset_status(np_manager *m)`
- Line 93: `void np_destroy(np_manager *m)`
- Line 131: `int np_expire(np_manager *m)`
- Line 147: `static int alloc_token(char out[NP_TOKEN_MAX])`
- Line 171: `static int parse_json(const char *js, size_t len, jtok *t, unsigned cap)`
- Line 263: `static int eq(const char *j, const jtok *t, const char *s)`
- Line 277: `static int subtree(jtok *t, int n, int i)`
- Line 295: `static int member(const char *j, jtok *t, int n, int obj, const char *key)`
- Line 316: `static int hex4(const char *s)`
- Line 342: `static size_t put_utf8(char *o, size_t cap, size_t n, unsigned v)`
- Line 377: `static int string_value(const char *j, const jtok *t, char *o, size_t cap)`
- Line 439: `static int boolean(const char *j, const jtok *t, int *d)`
- Line 462: `static int number(const char *j, const jtok *t, uint64_t *d)`
- Line 484: `static int valid_utf8(const char *s)`
- Line 516: `static size_t esc(char *o, size_t cap, const char *s)`
- Line 548: `static const char *state_name(np_state s)`
- Line 562: `static const char *lms_mode(np_state s)`

## native/sbbase/CMakeLists.txt

SHA256: `60e91d7063e569f34f92eb93382992f4becb13d2b4ac8e9f2e0da6947bb246d7`

- Line 11: `option(SB_ENABLE_MUSIC "Reserved Standalone Music activation (unavailable in WP0)" OFF)`
- Line 12: `if(SB_ENABLE_MUSIC)`

## scripts/build-arm.sh

SHA256: `803e8f686e9e90dc24118c39ad4dc587f9eabd8a07081a079f2169a9e936f04b`


## tests/CMakeLists.txt

SHA256: `43c19e066b1523a09903bb434500d4ed49ca445eb6f35d48e2c26e49e2ac6c92`


## tests/music/benchmark_base.py

SHA256: `3b30eb6a784d4225b215bf24567cad42a149d5af12edd6602091b90d1c587b72`

- Line 15: `def run(binary, seconds=2, count=100):`

## tests/music/benchmark_parsers.py

SHA256: `f7b2a432b0dd5655a3e2471ab269efa81f3233fdf01d49082595ec427c47e4fb`

- Line 10: `def run(directory, platform, iterations):`

## tests/music/build_parser_spike.sh

SHA256: `afadb7f8250aed6d4612a6dfa7c704dd8ea647056258a7b8008b69538daf8f8d`


## tests/music/cross_contracts.sh

SHA256: `cd0e984a100ca8383101368c7bd7b9a990ca33fb6b76d32e35088b39e51053e0`


## tests/music/make_corpus.py

SHA256: `4823bbd80b2eb6bac1544564bfad4b75d24640f287941c1fa04c8e1e7136fda7`

- Line 12: `def sync(n):`
- Line 16: `def generate(output, ffmpeg):`

## tests/music/parser_hls.c

SHA256: `3e3f2d7bd59ee49d0a79594e87c898525be5da81f9db8cb2dcef85a19d8c8922`

- Line 10: `static void emit(void *unused,const hls_metadata *m){(void)unused;output=*m;events++;}`
- Line 11: `int main(int argc,char **argv)`

## tests/music/parser_taglib.cpp

SHA256: `bbaa3d49fcc6767d4028b8076f7ac01625c144627521b7cc269cbcf1531748f0`

- Line 21: `static size_t calls, live, peak;`
- Line 23: `void *operator new(size_t n) {`
- Line 27: `void operator delete(void *p) noexcept { if(p){Header *h=static_cast<Header *>(p)-1;live-=h->bytes;std::free(h);} }`
- Line 28: `void *operator new[](size_t n){return ::operator new(n);}`
- Line 29: `void operator delete[](void *p) noexcept {::operator delete(p);}`
- Line 30: `void operator delete(void *p,size_t) noexcept {::operator delete(p);}`
- Line 31: `void operator delete[](void *p,size_t) noexcept {::operator delete(p);}`
- Line 32: `static void quoted(const std::string &s) {`
- Line 35: `int main(int argc,char **argv) {`

## tests/music/test_commands.c

SHA256: `337ab6274eb5d6de7896bc992ccceb3e85ab5e42eb0837e1a246b8ec7e4a3f43`

- Line 5: `static sb_command_result parse(const char *args,sb_lms_command *c)`
- Line 10: `int main(int argc,char **argv)`

## tests/music/test_contracts.c

SHA256: `4de7d38d53b69f527cbe586f9582844e3a0b592bb6d82154ed14035318998d87`

- Line 7: `int main(void)`
- Line 10: `assert(!SB_MUSIC_ENABLED);`

## tests/music/test_dispatch_http.py

SHA256: `f959528df2a8526f6ae7f33339c914c46856fbf031419fefe1ad93559e5913ca`

- Line 13: `def envelope(args, rid=1):`

## tests/music/test_fd_contract.py

SHA256: `17fdd8f9901035a81010eb486ea311cb9cddd52b2ee0938a7148795073f03cd3`


## tests/music/test_wire_fixtures.py

SHA256: `7b845b1834da4a0ef7f3106ed8888a2c3be2ba4d784a1fc9a88ca45c9f4b56c7`


## .local/fab4-music-research/device/usr/share/jive/jive/net/SlimProto.lua

SHA256: `84797e840d274bc23384048655eb6084817a5b0eab3150d9f1aafa1dc34c4c00`

- Line 81: `function _ipstring(ip)`
- Line 340: `function __init(self, jnt, heloPacket)`
- Line 413: `function getServerIp(self)`
- Line 419: `function getId(self)`
- Line 426: `function connectIp(self, serverip, slimserverip)`
- Line 456: `function connect(self, server)`
- Line 473: `function _connectToAddr(self, serverip)`
- Line 478: `function connectTask(self, serverip)`
- Line 619: `function _setGeekmode( self, geekmode)`
- Line 624: `function setGeekmodeTask( self, geekmode)`
- Line 635: `function _blastIR( self, irstr)`
- Line 640: `function blastIRTask( self, irstr)`
- Line 649: `function disconnect(self)`
- Line 662: `function isConnected(self)`
- Line 667: `function hasConnectionFailed(self)`
- Line 672: `function statusPacketCallback(self, callback)`
- Line 678: `function capability(self, key, value)`
- Line 687: `function subscribe(self, opcode, subscription)`

## Build and measurement provenance

Raw reproducible logs are ignored workstation artifacts under `.local/wp0/`: baseline-build.log, baseline-tests.log, baseline-sizes.json, baseline-performance.json, build-final.log, final-tests.log, build-arm-final.log, cross-contracts.log, proxy-host-probe.log, music-on-probe.log, taglib-*-final.log and parsers-*.json. Selected outcomes are preserved in the committed-intent research measurement artifact; no credentials/device logs are added. TagLib archive SHA256 is pinned in tests/music/build_parser_spike.sh. Source/formats/license comparison uses that downloaded upstream v1.13.1 snapshot, not an assumed current API.
