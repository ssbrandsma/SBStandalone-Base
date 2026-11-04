# Standalone Music Phase 0 Evidence Index

Inspection date: 2026-11-01. Base HEAD: `0a4b50e836446bdaf03804fe1a5477a139742243`. Read-only source inspection; no new device experiment.

All line anchors refer to the inspected working tree, not future implementation. B = Base; R/S/Q = sibling application; F = copied installed firmware; L = installed TinySC reference. Firmware paths below are workstation copies: strip `.local/fab4-music-research/device` to recover the device path. Raw copies are ignored/private; the earlier [firmware ledger](../docs/research/FAB4_LOCAL_MUSIC_EVIDENCE.md) provides provenance. External app source is not included in this repository.

Confirmed = directly visible implementation; partial = bounded subset; unverified = no evidence/test; proposed = design only. Absence findings refer to inspected routing/build/dispatch scope, not every historical branch.

## B01

`native/sbbase/src/main.c`

SHA256: `795fe1ef27482e92d4c166b21196d1d7bc7693b9af5f0128ac8feb23eb64f743`

- Line 47: `#define CLIENTS 32`
- Line 52: `#define SEND_WAIT_MS 2000`
- Line 57: `time_t keepalive;`
- Line 79: `static firmware_subscription firmware_subscriptions[CLIENTS];`
- Line 199: `r = poll(&pfd, 1, SEND_WAIT_MS);`
- Line 243: `c->keepalive = time(NULL);`
- Line 251: `static void stream_start(client *c, const char *body)`
- Line 269: `c->keepalive = time(NULL);`
- Line 349: `for (i = 0; i < CLIENTS; i++)`
- Line 359: `static void publish_playerstatus(void)`
- Line 404: `static void register_playerstatus(const char *path)`
- Line 542: `static void unregister_playerstatus(const char *body)`
- Line 960: `static int switch_server(const char *start, const char *end)`
- Line 1043: `request_responses(const char *body, const sb_config *cfg, char *out, size_t cap)`
- Line 1074: `switched = switch_server(p, next);`
- Line 1094: `publish_playerstatus();`
- Line 1102: `publish_playerstatus();`
- Line 1113: `register_playerstatus(reply);`
- Line 1265: `static void handle_http(client *c, const sb_config *cfg)`
- Line 1530: `request_responses(body, cfg, response, sizeof response);`
- Line 1550: `static void keepalive(client *c)`
- Line 1567: `static void handle_slim(client *c)`
- Line 1645: `int main(int argc, char **argv)`
- Line 1884: `handle_slim(&cs[i]);`
- Line 1886: `handle_http(&cs[i], &cfg);`

## B02

`native/sbbase/src/nowplaying.c`

SHA256: `da2a98418b7e87a41935c7595a5b8a7994fd6f10a06be71ee7fb8fb688a9c20d`

- Line 62: `static void reset_status(np_manager *m)`
- Line 131: `int np_expire(np_manager *m)`
- Line 138: `reset_status(m);`
- Line 607: `int np_playerstatus_json(np_manager *m, char *out, size_t cap)`
- Line 953: `int np_queue_command(np_manager *m, const char *command, int64_t value)`
- Line 1010: `int np_api(np_manager *m,`
- Line 1059: `reset_status(m);`
- Line 1589: `static int collect_fields(const np_status *s, const char *tags, np_field *f)`
- Line 1873: `int np_lms_query_json(`
- Line 1947: `if (np_playerstatus_json(m, out, cap) < 0)`
- Line 1960: `? collect_fields(s, tags, fields)`

## B03

`native/sbbase/include/nowplaying.h`

SHA256: `7de072f858667d6edac17234efd99ff43662de81b24669e387c794081509384c`

- Line 13: `#define NP_COMMANDS_MAX 32`
- Line 34: `} np_capabilities;`
- Line 60: `} np_track;`
- Line 71: `np_capabilities capabilities;`
- Line 72: `np_track track;`
- Line 74: `} np_status;`
- Line 87: `const np_status *np_current(np_manager *m);`

## B04

`native/sbproxy/sbproxy.c`

SHA256: `4402f987140b63fb55008ab5ceadf50ff15a449b3773a1db33f9019a9fcad284`

- Line 71: `#define MAX_WORKERS 8`
- Line 72: `#define WORKER_STACK (256U * 1024U)`
- Line 315: `return "Range Not Satisfiable";`
- Line 604: `(!public_only || !strcasecmp(name, "Range") ||`
- Line 605: `!strcasecmp(name, "If-Range") ||`
- Line 854: `static void *worker_main(void *opaque)`
- Line 938: `*   /https/example.com/path`
- Line 944: `if (!strncmp(target, "/hls/", 5)) {`
- Line 948: `if (!strncmp(target, "/artwork/", 9)) {`
- Line 951: `"/artwork/%u/%u/%u/%n",`
- Line 964: `plain_http = !strncmp(source_target, "/http/", 6);`
- Line 965: `if (!plain_http && strncmp(source_target, "/https/", 7)) {`
- Line 1312: `pthread_attr_setstacksize(&attributes, WORKER_STACK);`
- Line 1372: `if (pthread_create(&thread, &attributes, worker_main, arg)) {`

## B05

`native/sbproxy/hls_id3.c`

SHA256: `cb50d9029bfe6ff1d2943fb478ee1f17049973ab0bf1caae11c69571f047f9fc`

- Line 2: `* or segment. Delivers UTF-8 fields and validated small APIC to the registry.`
- Line 44: `text(char out[HLS_TEXT_MAX], unsigned enc, const unsigned char *p, size_t size)`
- Line 118: `/** Validate APIC signature and decoded dimensions without image decoding. */`
- Line 207: `if (!strcmp(s->id, "TIT2")) {`
- Line 211: `if (!strcmp(s->id, "TPE1")) {`
- Line 215: `if (!strcmp(s->id, "TALB")) {`
- Line 220: `text(out, p[0], p + 1, n - 1);`
- Line 241: `if (strcmp(s->id, "APIC") || n < 8 || p[0] > 3)`
- Line 312: `void hls_id3_init(hls_id3 *s, hls_metadata_output output, void *opaque)`
- Line 427: `"TIT2") ||`
- Line 429: `"TPE1") ||`
- Line 430: `!strcmp(s->id, "TALB");`

## B06

`native/sbproxy/hls_metadata.c`

SHA256: `0452d978a0bab63ddd092be020c22b32d390e06a5092d836617e34d384ea33fc`

- Line 25: `static metadata_session *sessions[2];`
- Line 108: `void hls_metadata_publish(void *opaque,`
- Line 220: `int hls_metadata_http(int fd, const char *target, int head)`

## B07

`native/sbwebserver/src/image_render.c`

SHA256: `c72ef9a92a1875c3758260bc2394112c2ebca02bb3983ef00b700542f5aa594b`

- Line 37: `#define CACHE_LIMIT (20u * 1024u * 1024u)`
- Line 38: `#define CACHE_TTL (7 * 86400)`
- Line 524: `static int cache_directory(char *path, size_t capacity)`
- Line 613: `static void cache_publish(const char *directory,`
- Line 648: `if (total + size <= CACHE_LIMIT && count - i < 256 &&`
- Line 649: `time(NULL) - entries[i].time <= CACHE_TTL)`
- Line 659: `if (total + size > CACHE_LIMIT)`
- Line 828: `static void worker(int pipe_fd,`
- Line 850: `int cache = cache_directory(directory, sizeof directory);`
- Line 855: `time(NULL) >= st.st_mtime && time(NULL) - st.st_mtime < CACHE_TTL) {`
- Line 940: `cache_publish(directory, target, png, png_size);`
- Line 980: `if (!equal(m->uri, "/api/image/render"))`
- Line 996: `int image_render_handle(struct mg_connection *c, struct mg_http_message *m)`
- Line 1002: `if (!equal(m->uri, "/api/image/render"))`
- Line 1096: `if (cache_directory(scratch,sizeof scratch) &&`
- Line 1136: `if (setrlimit(RLIMIT_AS, &limit) ||`
- Line 1140: `worker(pipes[1],`

## B08

`applet/Artwork.lua`

SHA256: `ff4bcd6983fef203e3f4a2cb748d2d57f9e76df27597837c004a92f6972d027e`

- Line 19: `function M.route(url, width, height)`
- Line 47: `function M.sink(self, key, size, url, decode)`

## B09

`applet/ArtworkUrlNormalizer.lua`

SHA256: `2c73792430affe3b59966a433d6c8992df0de9eed8314d7f18caef2483e86f97`

- Line 20: `function M.normalize(url)`

## B10

`native/sbwebserver/src/http_server.c`

SHA256: `bbcd81a64332b584329d03a9d83838b533766280522506febe3008cbcf935611`

- Line 535: `normal(Server *s, struct mg_connection *c, struct mg_http_message *m)`
- Line 655: `mg_http_serve_file(c, m, path, &o);`
- Line 712: `mg_http_serve_file(c, m, p, &o);`
- Line 714: `mg_http_serve_dir(c, m, &o);`
- Line 768: `normal(s, c, m);`

## B11

`native/sbwebserver/src/settings.c`

SHA256: `1d2992e93fb80c0090c32eaf92b6c5be1536cbb2ff0bdaaf7a4c96dc5a9bbf6a`

- Line 108: `fsync(fileno(f));`
- Line 112: `return rename(t, p);`

## B12

`native/sbbase/src/alarms.c`

SHA256: `b3908a1ef2758edde1ec5fe0d8ff0ea96c2efe50a509e7dbdbe0a4161dee235b`

- Line 152: `if (fflush(f) || fsync(fd))`
- Line 156: `if (ok && rename(temporary, a->path) == 0)`

## B13

`applet/Platform.lua`

SHA256: `0ad760bb2e474077756176fca51a74ad99088c5827ee78614beed5816cb862a5`

- Line 7: `function M.detect(cpu)`
- Line 26: `function M.portConflicts(reader)`

## B14

`native/sbbase/CMakeLists.txt`

SHA256: `b6ffe8f80daba3a5499dea550377dc82d004dcc70867a66e69c758824c5155a4`

- Line 3: `add_executable(sbbase src/main.c src/protocol.c src/nowplaying.c src/alarms.c src/sleep.c)`

## B15

`scripts/build-arm.sh`

SHA256: `2dbc57e566cdc19b334b61d3caea433f4d7b6a6223196fe1ff158a7e6de3c4ea`

- Line 27: `toolchain="${ARM_TOOLCHAIN_DIR:-$SBHTTPSPROXY_ROOT/build/toolchains/armv5-eabi--musl--stable-2020.02-2}"`
- Line 28: `target=arm-buildroot-linux-musleabi`
- Line 40: `flags="-Os -marm -march=armv5te -mtune=arm926ej-s -mfloat-abi=soft -ffunction-sections -fdata-sections"`
- Line 43: `flags="-Os -marm -march=armv6 -mtune=arm1136jf-s -mfloat-abi=soft -ffunction-sections -fdata-sections -DSB_TARGET_FAB4=1"`
- Line 62: `-I"$root/native/sbbase/include" "$root/native/sbbase/src/main.c" \`
- Line 63: `"$root/native/sbbase/src/protocol.c" "$root/native/sbbase/src/nowplaying.c" \`
- Line 64: `"$root/native/sbbase/src/alarms.c" "$root/native/sbbase/src/sleep.c" -o "$out/sbbase"`

## R01

`C:/Projects/squeezebox/applet/StandaloneRadio/StreamPlayer.lua`

SHA256: `054b8ee6e75b53b8fa71949e2adb8d31d3ce9278359f952e7c177eaa923201b8`

- Line 94: `function StreamPlayer:_isCurrent(generation, station)`
- Line 99: `function StreamPlayer:_notifyState(station, state, show)`
- Line 105: `function StreamPlayer:_resetWatchdog()`
- Line 325: `local title = string.match(data, "StreamTitle='(.-)';")`
- Line 326: `or string.match(data, 'StreamTitle="(.-)";')`
- Line 345: `self.log:info("StandaloneRadio: StreamTitle=", title)`
- Line 584: `function StreamPlayer:_installHooks(playback)`
- Line 597: `self.log:info("StandaloneRadio: releasing playback for explicit library selection")`
- Line 893: `decode:start(`
- Line 902: `playback:_streamConnect(ip, networkStation.port)`

## R02

`C:/Projects/squeezebox/applet/StandaloneRadio/StandaloneBaseNowPlaying.lua`

SHA256: `42e74cf0cd8d06739d1bfabd43c9443d0eaa8cb01e2bbbc30e709674667baba1`

- Line 281: `self:_send("claim", "POST", "/api/nowplaying/claim", {`
- Line 334: `self:_send("update", "POST", "/api/nowplaying/update", snapshot, function(status, response, err)`
- Line 370: `self:_send("release", "POST", "/api/nowplaying/release", {session_id=token}, function(status, response, err)`

## S01

`C:/Projects/SBSpotifyConnect/applet/SpotifyPlayback.lua`

SHA256: `3e28389acb9c3cad8255dfb3f4b0f5cba99c56d7e5fd359851e377b1b318456d`

- Line 17: `function start(self, host, port, path)`
- Line 48: `function setRemoteVolume(self, percent)`
- Line 62: `function stop(self)`
- Line 76: `function resume(self)`

## S02

`C:/Projects/SBSpotifyConnect/native/bridge/bridge.rs`

SHA256: `9b2844be840275805f74ba2c38d4f847fab16d713da784395e0829dc58f568df`

- Line 91: `fn accept_http(mut s: TcpStream) -> io::Result<TcpStream> {`
- Line 114: `if request.len() == 4096 || !(first == b"GET /spotify.ogg HTTP/1.0\r" || first == b"GET /spotify.ogg HTTP/1.0" || first == b"GET /spotify.ogg HTTP/1.1\r") {`
- Line 136: `fn main() -> io::Result<()> {`
- Line 142: `let listener = TcpListener::bind(format!("127.0.0.1:{port}"))?;`

## S03

`C:/Projects/SBSpotifyConnect/applet/SpotifyStandaloneNowPlaying.lua`

SHA256: `f27ae074d8aa2589ea9b2b29547a07601173329c54840566570263e40a94f051`

- Line 17: `local function capabilities()`
- Line 125: `self:_send("claim","POST","/api/nowplaying/claim",{source="spotify",name="Spotify Connect",capabilities=capabilities()},function(status,r,err)`
- Line 126: `if status==201 and r and r.success==true and r.active==true and type(r.session_id)=="string" and tonumber(r.generation) then`
- Line 127: `self.sessionId=r.session_id; self.sessionGeneration=tonumber(r.generation); self.retryIndex=1`
- Line 135: `self:_send("status","GET","/api/nowplaying",nil,function(status,r,err)`
- Line 144: `local token,revision=self.sessionId,self.revision; snapshot.session_id=token`
- Line 145: `self:_send("update","POST","/api/nowplaying/update",snapshot,function(status,r,err)`

## Q01

`C:/Projects/QoBuz/applet/QobuzConnect/QobuzPlayback.lua`

SHA256: `1e3c594f5ddda5521c6b052e4448dff733009a33524a7adaafb8ff7d3cf2bfae`

- Line 32: `function _monitorPlayback(self)`
- Line 61: `function start(self, url, generation, codec)`
- Line 74: `if playback.slimproto then playback.slimproto:disconnect() end`
- Line 131: `function stop(self, report, generation)`

## Q02

`C:/Projects/QoBuz/applet/QobuzConnect/QobuzNowPlaying.lua`

SHA256: `f7fe06c7daf9f084e6a7e76f145ca0714d4f907ac5085ea2bf8dd8981c4a0da3`

- Line 18: `function Client:play(command)`
- Line 27: `function Client:state(generation, state, position)`
- Line 41: `function Client:release()`
- Line 75: `request = RequestHttp(finish, "POST", "/api/nowplaying/" .. action, {`
- Line 117: `self:_send("claim", {source="qobuz", name="Qobuz Connect", capabilities={}}, function(result)`

## F01

`.local/fab4-music-research/device/usr/share/jive/jive/audio/Playback.lua`

SHA256: `8e06a00174707449858615e5a02452297420da4956864e19f03c0e9b3a1bea2c`

- Line 320: `self:sendStatus(status, "STMd")`
- Line 354: `self:sendStatus(status, "STMu")`
- Line 381: `self:sendStatus(status, "STMs")`
- Line 824: `function _streamHttpHeaders(self, headers)`
- Line 843: `self:sendStatus(status, "STMd")`
- Line 846: `self:sendStatus(status, "STMu")`
- Line 852: `function _strm(self, data)`
- Line 872: `-- If we have already (tried to) send STMd/STMo then too late to reconnect`
- Line 1145: `function _audg(self, data, isLocal)`

## F02

`.local/fab4-music-research/device/usr/share/jive/applets/SlimBrowser/SlimBrowserApplet.lua`

SHA256: `0d85ade9be0d47488378afa29dd78f87dee2f2718a5892868a57e945f9191e40`

- Line 817: `_server:userRequest(sink, playerid, request)`
- Line 852: `_server:userRequest(step.sink, playerid, step.jsonAction)`
- Line 1777: `-- sorts out the action business: item action, base action, default action...`
- Line 1778: `_actionHandler = function(menu, menuItem, db, dbIndex, event, actionName, item, selectedIndex)`
- Line 1977: `log:debug("_actionHandler(", actionName, "): base action")`

## F03

`.local/fab4-music-research/device/usr/share/jive/jive/slim/SlimServer.lua`

SHA256: `8c6cc761c2bea423cff361b7a3516b400084e3eddea5fa5b2ae91b762ef8d90e`

- Line 506: `obj.comet:subscribe('/slim/serverstatus',`
- Line 1100: `function fetchArtwork(self, iconId, icon, size, imgFormat)`
- Line 1401: `function userRequest(self, func, ...)`
- Line 1435: `function request(self, ...)`

## F04

`.local/fab4-music-research/device/usr/share/jive/applets/MediaManager/MediaManagerApplet.lua`

SHA256: `731d8b698a10cd0728101b35597317e7c01962ccf4852f8f629f7b6fd219a67f`

- Line 74: `function addMountedDevice(self, devName)`
- Line 172: `function udevEventHandler(self, evt, msg)`
- Line 270: `function mmRegisterOnEjectHandler(self, options)`
- Line 286: `function mmRegisterOnMountHandler(self, options)`

## F05

`.local/fab4-music-research/device/usr/share/jive/applets/SqueezeCenter/SqueezeCenterApplet.lua`

SHA256: `526973a57d2587cd86359dd0eded3a42744d142f2dbff3a1b199f5a77fec9dc6`

- Line 35: `function init(self)`
- Line 43: `callback = function()`
- Line 50: `callback = function()`
- Line 66: `function squeezecenterStartupCheck(self)`
- Line 147: `function mmSqueezeCenterMenu(self, devName)`
- Line 274: `function _squeezecenterAction(self, icon, text, subtext, time, action, silent)`
- Line 388: `function udevEventHandler(self, evt, msg)`
- Line 402: `--	if it does, hide the popup, start the scanner (/etc/init.d/squeezecenter rescan), and push to the SqueezeCenter menu`
- Line 432: `function mmStopSqueezeCenter(self, devName)`
- Line 459: `function startSqueezeCenter(self)`
- Line 555: `function _startServerWindow(self)`
- Line 795: `function _writeSCPrefsFile(self, devName)`

## L01

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Menu/BrowseLibrary.pm`

SHA256: `a86c71f0485f2916ae9f66cda998a562f45ab62ba25d9ded5a37cb6f023255f2`

- Line 604: `sub getJiveMenu {`
- Line 858: `sub _search {`
- Line 870: `sub _globalSearchMenu {`
- Line 884: `sub searchItems {`
- Line 1013: `sub _artists {`
- Line 1166: `sub _genres {`
- Line 1288: `sub _albums {`
- Line 1487: `sub _tracks {`
- Line 1677: `sub _bmf {`
- Line 1739: `sub _playlists {`

## L02

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Control/Jive.pm`

SHA256: `ec095df1d924fba0ed5f8aed24c18393df9106d5bb0f3d257221f9b979c36434`

- Line 182: `sub menuQuery {`
- Line 2337: `sub myMusicMenu {`

## L03

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Control/Request.pm`

SHA256: `3db1f626c5b303b79288b4ddfcfaf3c10d19ea04085e81046c9768ac68be15c2`

- Line 64: `N    musicfolder     <startindex>                <numitems>                  <tagged parameters>`
- Line 142: `Y    playlistcontrol <tagged parameters>`
- Line 466: `sub init {`
- Line 527: `# musicfolder is only here for backwards compatibility - it's calling mediafolder internally`
- Line 528: `addDispatch(['musicfolder',    '_index',         '_quantity'],                                     [0, 1, 1, \&Slim::Control::Queries::musicfolderQuery]);`
- Line 593: `addDispatch(['playlistcontrol'],                                                                   [1, 0, 1, \&Slim::Control::Commands::playlistcontrolCommand]);`
- Line 611: `addDispatch(['search',         '_index',         '_quantity'],                                     [0, 1, 1, \&Slim::Control::Queries::searchQuery]);`
- Line 689: `playlistcontrol playlists stop stopserver restartserver wipecache prefset mode`

## L04

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Utils/Scanner/Local.pm`

SHA256: `b9befe3769fdc1beda3fad9c715dd79d9efdb01dbf6c5bf414fe04017ebaea1e`

- Line 51: `sub find {`
- Line 114: `sub rescan {`
- Line 555: `sub deleted {`
- Line 765: `sub new {`
- Line 854: `sub changed {`
- Line 1070: `sub scanPlaylistFileHandle {`

## L05

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Utils/Scanner/Local/Async.pm`

SHA256: `cb71ea3953d052559a5ab2513d4ef95018751b3c824b0f35e6d5cd7bf22a4251`

- Line 17: `use File::Next;`
- Line 29: `sub find {`
- Line 61: `my $iter = File::Next::everything( {`
- Line 62: `file_filter    => sub {`
- Line 63: `-f _ && $_ ? Slim::Utils::Misc::fileFilter($File::Next::dir, $_, $types)`
- Line 66: `descend_filter => sub {`

## L06

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Utils/AutoRescan.pm`

SHA256: `a6397c78b2f2532a2de8169662e60d8d800ed3c4c06d30336277ab865c931e8e`

- Line 11: `# Mac 10.5+, Linux with inotify, and Windows.`
- Line 24: `use constant BATCH_DELAY => 15; # how long to wait for events to settle before handling them`
- Line 33: `sub init {`
- Line 99: `my $rewatch = sub {`
- Line 112: `Slim::Control::Request::subscribe( $rewatch, [['rescan'], ['done']] );`

## L07

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Player/Protocols/File.pm`

SHA256: `1af47d1d772513b892a04d47a1e2f4ebca117750f116dc906a157030c93e1552`

- Line 50: `sub open {`
- Line 320: `sub _timeToOffset {`
- Line 367: `sub getSeekData {`
- Line 377: `sub getSeekDataByPosition {`
- Line 398: `sub canSeek {`
- Line 412: `sub canSeekError {`

## L08

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Networking/Slimproto.pm`

SHA256: `4220262733c265d6a8d4fc91ff7da5e236db1a8e13b1b521d58dfadbe04e8d4f`

- Line 555: `sub _http_response_handler {`
- Line 581: `sub _disco_handler {`
- Line 681: `sub _stat_handler {`
- Line 974: `sub _hello_handler {`

## L09

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Utils/OS/SqueezeOS.pm`

SHA256: `646f321a068c1e58ee9162fafb66cea1bb9bfc4baf453b14e84fa0a76cc7d0e3`

- Line 135: `$prefs->setChange( \&_onAudiodirChange, 'mediadirs', 'FIRST' );`
- Line 138: `}, 'language', 'mediadirs' );`
- Line 320: `sub _setupMediaDir {`
- Line 352: `$prefs->set( mediadirs       => [ $path ] );`
- Line 353: `$prefs->set( librarycachedir => "$path/.Squeezebox/cache" );`

## L10

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Schema.pm`

SHA256: `4150a8c15c99c3ad8c48a6e35cb6e3119f1cc1ee218c7bcceb08af07d5786065`

- Line 22: `acting as a subclass of L<DBIx::Class::Schema>.`
- Line 103: `sub dbh {`
- Line 124: `sub init {`

## F06

`.local/fab4-music-research/device/etc/udev/scripts/mount.sh`

SHA256: `9d1cb3fc5c547f5665609ef7f681a01e25e3b9881a9484c152514347d577fbda`

- Line 5: `# Attempt to mount any added block devices and umount any removed devices`
- Line 8: `MOUNT="/bin/mount -t auto"`
- Line 9: `PMOUNT="/usr/bin/pmount"`
- Line 26: `! test -d "/media/$name" && mkdir -p "/media/$name"`
- Line 28: `if [ "$fstype" = "ntfs" ]; then`
- Line 29: `MOUNT="/usr/bin/ntfs-3g"`
- Line 35: `if ! $MOUNT $DEVNAME "/media/$name" $MOUNT_OPTIONS`
- Line 37: `#logger "mount.sh/automount" "$MOUNT $DEVNAME \"/media/$name\" failed!"`

## L11

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Control/XMLBrowser.pm`

SHA256: `fed655006d68991dbf47e90815834ae5418fb65dc684585c124acf578e7d6508`

- Line 1614: `sub _makeAction {`
- Line 897: `'itemsParams' => 'params',`
- Line 902: `'itemsParams' => 'params',`
- Line 909: `'itemsParams' => 'params',`
- Line 1582: `my @vars = exists $feedAction->{'variables'} ? @{$feedAction->{'variables'}} : @{$feedActions->{'commonVariables'} || []};`
- Line 1639: `} elsif ($actions->{'commonVariables'}) {`
- Line 1641: `$actionParamsNeeded->{$action{'itemsParams'}} = $actions->{'commonVariables'};`
- Line 904: `'nextWindow' => 'nowPlaying',`
- Line 1185: `if ($item->{nextWindow}) {`
- Line 1186: `$hash{'nextWindow'} = $item->{nextWindow};`
- Line 967: `$request->addResult('base', $base);`

## L12

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Control/Commands.pm`

SHA256: `de73dd1ec21e259a3ee44e4c8f8538b09586bb7ca4a728c50f1b2ca4133afe6c`

- Line 1746: `sub playlistcontrolCommand {`
- Line 900: `sub playlistJumpCommand {`
- Line 1035: `sub playlistRepeatCommand {`
- Line 1133: `sub playlistShuffleCommand {`

## L13

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Control/Queries.pm`

SHA256: `823fc8e096ba30d97a92aa54462796fa630fde807333dbcb583c83335b306f90`

- Line 244: `sub albumsQuery {`
- Line 610: `sub artistsQuery {`
- Line 1577: `sub musicfolderQuery {`
- Line 3590: `sub titlesQuery {`
- Line 2918: `sub statusQuery_filter {`
- Line 2992: `sub statusQuery {`

## L14

`.local/fab4-music-research/device/usr/squeezecenter/Slim/Music/Info.pm`

SHA256: `51b5afb6b36decd644c43dcc72d3a0d3d0df719bf9813e8b818f550ea227336f`

- Line 1303: `sub validTypeExtensions {`

## B16

`native/sbwebserver/third_party/mongoose.c`

SHA256: `3712aff8761c7291cd927805ac81da1c21a54134a3c653942653c56487f87889`

- Line 1942: `void mg_http_serve_file(struct mg_connection *c, struct mg_http_message *hm,`
- Line 1926: `static int getrange(struct mg_str *s, size_t *a, size_t *b) {`
- Line 1989: `size_t r1 = 0, r2 = 0, cl = size;`
- Line 1843: `static void static_cb(struct mg_connection *c, int ev, void *ev_data) {`
- Line 2000: `mg_snprintf(range, sizeof(range), "Content-Range: bytes */%lld\r\n",`
- Line 2006: `"Content-Range: bytes %llu-%llu/%llu\r\n", (uint64_t) r1,`

## B17

`applet/ImageRender.lua`

SHA256: `240421ca4397e7c502953db51330ef150a66a42f96c850e29a3dd410564ade00`

- Line 7: `local function diagnosticUrl(url)`
- Line 11: `local function webPort()`
- Line 25: `function M.artworkUrl(url, width, height, mime)`
- Line 41: `url = ArtworkUrlNormalizer.normalize(url)`
- Line 44: `" normalized=", diagnosticUrl(url))`

## B18

`native/sbbase/src/protocol.c`

SHA256: `398c2386f88e7f8051811959b5c9abf0dabe34fd7803fa9af4e68c294aeb599a`

- Line 103: `int sb_parse_client_frame(const unsigned char *d,`
- Line 136: `size_t sb_server_frame(`
- Line 44: `size_t sb_discovery_response(const unsigned char *r,`


## Confirmed findings and remaining hypotheses

| Finding | Evidence and scope | Classification |
|---|---|---|
| Base does not serve music files on its 9000 HTTP route | B01 handle_http:1265 rejects other routes; B10:535 serves existing web resources | Confirmed missing music route; reusable web server exists |
| HELO is the only interpreted player opcode in Base | B01 handle_slim:1567; B18 frame parser only frames | Confirmed partial SlimProto |
| Current command queue is not a track playlist | B02 np_queue_command:953; B03 np_command and limits | Confirmed |
| Claim can replace metadata ownership without stopping audio | B02 np_api:1010 and claim reset; separate R01/S01/Q01 playback | Confirmed boundary; physical timing untested |
| Radio handoff excludes localhost/127 destinations | R01 StreamPlayer.lua:593-597 | Confirmed local takeover gap |
| Qobuz disconnects SlimProto | Q01 start:61 onwards | Confirmed reconnect requirement; handoff outcome untested |
| Spotify Base control flags are false | S03 capabilities:17 | Confirmed, must remain honest |
| ID3 title/artist/album and bounded APIC exist | B05 frame handling:207-241 including TALB:215 | Confirmed partial parser, not general local indexer |
| Native file tag/SQLite build dependency is absent | B14 executable source list and B15 build inputs | Confirmed for current build; no claim about all historical repos |
| Artwork worker/cache can be extended | B07 cache_directory:524, worker:826, route/fork/limits; B08/B17 routing | Existing implementation confirmed; local-cover extension proposed |
| Fab4 persistent cache path needs adaptation | B07 default /mnt/sbdata plus earlier no-sbdata mount evidence | Confirmed inspected configuration, not all deployed devices |
| HTTP HEAD/basic Range already exist in vendored server | B16 mg_http_serve_file:1942, getrange:1926; counters use size_t | Confirmed partial reusable support; ARM large-file/suffix behavior untested |
| Music Folder uses bmf, not a _musicfolder handler | L01 _bmf:1677 invokes musicfolder | Confirmed source flow |
| Album action commonVariables becomes commonParams on wire | L01 _albums:1288 and L11 _makeAction:1614 | Confirmed serialization; examples reconstructed |
| USB/SD mounting already belongs to firmware | F06 script; F04 UI | Confirmed source; no insertion/removal experiment here |
| On-mount callback invocation is uncertain | F04 stores callback table; earlier active-path trace found no invocation | Unverified; do not assume hook works |
| Exact Baby shipping RAM is not established | B13 reads MemTotal; generic Freescale size macro not device-specific proof | Evidence gap |
| Proposed C/SQLite architecture meets budgets | No music implementation/profile exists | Hypothesis; future acceptance gate |
| Selected parser/journal mode is suitable | Alternatives compared, no ARM benchmark/filesystem experiment | Open decision |

External primary references consulted for proposed dependencies: [TagLib API](https://taglib.org/api/) and [build requirements](https://github.com/taglib/taglib/blob/master/INSTALL.md), [libid3tag upstream mirror](https://github.com/tenacityteam/libid3tag), [FLAC metadata](https://xiph.org/flac/api/group__flac__metadata.html), [Vorbisfile](https://xiph.org/vorbis/doc/vorbisfile/), [FFmpeg API](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/avformat.h) and [licensing](https://ffmpeg.org/legal.html), [SQLite threading](https://www.sqlite.org/threadsafe.html), [WAL](https://www.sqlite.org/wal.html) and [compile options](https://www.sqlite.org/compile.html). Accessed 2026-11-01; these are not pinned build inputs. Version choice, build size, resource use and linked license obligations remain implementation gates. Original runtime behavior is grounded in installed firmware, not substituted with current upstream behavior.
