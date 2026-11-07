# Fab4 local-music evidence index - 2026-11-01

Files below were copied **read-only** from the supplied Touch. Numbers identify lines in the captured firmware files, not a newer LMS branch. Raw captures remain in the ignored `.local/fab4-music-research/` directory because they contain device-specific settings. No application source was edited.

## D01 - `/etc/init.d/squeezecenter`

SHA-256: `ae913ec4ba84d7b5003f08fddc2c3df15a453071784482e35f0dae2faf335c0d`

- Line 3: `BASE=/usr/squeezecenter`
- Line 14: `case "$1" in`
- Line 28: `nice -n 5 $BASE/slimserver.pl --charset utf8 --noimage --novideo --noupnp --noweb --notranscoding --nosb1slimp3sync --nostatistics --noinfolog --nodebuglog --logfile syslog &`
- Line 53: `stopserver)`

## D02 - `/etc/udev/rules.d/local.rules`

SHA-256: `fbb2957e270aa4b4be974e9976eec100d07449b832c0b3d9b8cdd04ada157f9b`

- Line 17: `SUBSYSTEM=="block", ACTION=="add"    RUN+="/etc/udev/scripts/mount.sh"`
- Line 18: `SUBSYSTEM=="block", ACTION=="remove" RUN+="/etc/udev/scripts/mount.sh"`

## D03 - `/etc/udev/scripts/mount.sh`

SHA-256: `9d1cb3fc5c547f5665609ef7f681a01e25e3b9881a9484c152514347d577fbda`

- Line 22: `automount() {`
- Line 28: `if [ "$fstype" = "ntfs" ]; then`
- Line 29: `MOUNT="/usr/bin/ntfs-3g"`
- Line 55: `if [ "$ACTION" = "add" ] && [ -n "$DEVNAME" ]; then`
- Line 66: `/etc/init.d/samba restart`
- Line 71: `if [ "$ACTION" = "remove" ] && [ -x "$UMOUNT" ] && [ -n "$DEVNAME" ]; then`
- Line 82: `/etc/init.d/samba restart`

## D04 - `/usr/share/jive/applets/MediaManager/MediaManagerMeta.lua`

SHA-256: `db8f081d0c91e818b9aa05c3c8ab334b38888a32849b7a494c9473ca853c954d`

- Line 37: `function configureApplet(meta)`
- Line 53: `appletManager:callService("addUeventListener", "",`

## D05 - `/usr/share/jive/applets/MediaManager/MediaManagerApplet.lua`

SHA-256: `731d8b698a10cd0728101b35597317e7c01962ccf4852f8f629f7b6fd219a67f`

- Line 31: `function init(self)`
- Line 172: `function udevEventHandler(self, evt, msg)`
- Line 286: `function mmRegisterOnMountHandler(self, options)`
- Line 492: `function _mountingDriveTimer(self, devName)`
- Line 895: `function _ejectWarning(self, devName)`

## D06 - `/usr/share/jive/applets/SqueezeCenter/SqueezeCenterMeta.lua`

SHA-256: `35db0ea936bb1d0c278853fc3dac767ddd0d7dab1b2e85fb7c43a9debebf9985`

- Line 26: `function registerApplet(meta)`
- Line 37: `function configureApplet(meta)`

## D07 - `/usr/share/jive/applets/SqueezeCenter/SqueezeCenterApplet.lua`

SHA-256: `526973a57d2587cd86359dd0eded3a42744d142f2dbff3a1b199f5a77fec9dc6`

- Line 66: `function squeezecenterStartupCheck(self)`
- Line 173: `function switchServerToDifferentMedia(self, devName)`
- Line 274: `function _squeezecenterAction(self, icon, text, subtext, time, action, silent)`
- Line 485: `function _wipeRescan(self)`
- Line 555: `function _startServerWindow(self)`
- Line 795: `function _writeSCPrefsFile(self, devName)`

## D08 - `/usr/squeezecenter/Slim/Utils/OS/SqueezeOS.pm`

SHA-256: `646f321a068c1e58ee9162fafb66cea1bb9bfc4baf453b14e84fa0a76cc7d0e3`

- Line 67: `sub initPrefs {`
- Line 130: `sub postInitPrefs {`
- Line 224: `sub dirsFor {`
- Line 302: `sub skipPlugins {`
- Line 320: `sub _setupMediaDir {`
- Line 377: `sub _onAudiodirChange {`
- Line 396: `sub _checkMediaAtStartup {`

## D09 - `/usr/squeezecenter/Slim/Utils/AutoRescan.pm`

SHA-256: `a6397c78b2f2532a2de8169662e60d8d800ed3c4c06d30336277ab865c931e8e`

- Line 24: `use constant BATCH_DELAY => 15; # how long to wait for events to settle before handling them`
- Line 33: `sub init {`
- Line 157: `Slim::Utils::Timers::setTimer( undef, AnyEvent->now + BATCH_DELAY, \&handleQueue );`
- Line 160: `sub handleQueue {`

## D10 - `/usr/squeezecenter/Slim/Utils/AutoRescan/Linux.pm`

SHA-256: `4c1975ebaad3eb05085e531193f2310f2cb4c839174735fa798ebc62d7ce003c`

- Line 37: `sub canWatch {`
- Line 80: `sub watch {`
- Line 103: `sub event {`
- Line 189: `IN_MOVE | IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF,`
- Line 225: `IN_MOVE | IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF,`

## D11 - `/usr/squeezecenter/Slim/Utils/Scanner/Local.pm`

SHA-256: `b9befe3769fdc1beda3fad9c715dd79d9efdb01dbf6c5bf414fe04017ebaea1e`

- Line 114: `sub rescan {`
- Line 231: `scanned_files.timestamp != tracks.timestamp`

## D12 - `/usr/squeezecenter/Slim/Music/Import.pm`

SHA-256: `1fae039354c469ad2256b17b4771d26942859973f92e47606ff79e83a1cac68c`

- Line 127: `# scannerPriority has been specified.`
- Line 129: `my $scannerPriority = $prefs->get('scannerPriority');`
- Line 131: `unless (defined $scannerPriority && $scannerPriority ne "") {`
- Line 132: `$scannerPriority = Slim::Utils::Misc::getPriority();`
- Line 135: `if (defined $scannerPriority && $scannerPriority ne "") {`
- Line 136: `$args->{"priority=$scannerPriority"} = 1;`
- Line 144: `my $command  = Slim::Utils::OSDetect::getOS->scanner();`

## D13 - `/usr/squeezecenter/Slim/Formats/MP3.pm`

SHA-256: `3041fac1c65bd042095d7b5caa3955f499161a43668991d57e93adbefce21f22`

- Line 29: `use Audio::Scan;`
- Line 108: `sub getTag {`

## D14 - `/usr/squeezecenter/Slim/Utils/SQLiteHelper.pm`

SHA-256: `52ff7a2da4da14bfd1450d7124651456d0081453d3d4e45df0a9c5038c737b9a`

- Line 94: `sub source {`
- Line 106: `my $dbFile = catfile( $prefs->get('librarycachedir'), 'library.db' );`
- Line 117: `sub on_connect_do {`
- Line 297: `sub checkDataSource {`

## D15 - `/usr/squeezecenter/Slim/Schema.pm`

SHA-256: `4150a8c15c99c3ad8c48a6e35cb6e3119f1cc1ee218c7bcceb08af07d5786065`

- Line 464: `sub migrateDB {`
- Line 652: `sub searchTypes {`

## D16 - `/usr/squeezecenter/SQL/SQLite/schema_1_up.sql`

SHA-256: `c98b12d354b96e3fded33171804731cbfbc13bab2669993452f824114afdc9cf`

- Line 35: `CREATE TABLE tracks (`
- Line 96: `CREATE TABLE playlist_track (`
- Line 112: `CREATE TABLE albums (`
- Line 152: `CREATE TABLE contributors (`
- Line 219: `CREATE TABLE genre_track (`

## D17 - `/usr/squeezecenter/SQL/SQLite/schema_10_up.sql`

SHA-256: `482f5dc9338bb1a3d3fa839a26af44541b6779a1248f01056c7628b047ed1986`

- Line 6: `CREATE TABLE playlist_track (`

## D18 - `/usr/squeezecenter/SQL/SQLite/schema_16_up.sql`

SHA-256: `e0853e58eeeaba6fa88bccc9567cdd393e2f8f94f438f57213826a13d133e18a`

- Line 4: `CREATE TABLE tracks (`
- Line 6: `url text NOT NULL COLLATE NOCASE,	-- bug 17457: url needs to be case insensitive, or some import actions on Windows might fail`
- Line 43: `coverid char(8) default NULL,`

## D19 - `/usr/squeezecenter/SQL/SQLite/schema_18_up.sql`

SHA-256: `c8b0c87d9a7a3ade6f16a1276b9d0198693a12f07e79381f625cbd832a8f1932`

- Line 1: `ALTER TABLE tracks ADD dlna_profile varchar(32);`
- Line 2: `ALTER TABLE tracks ADD hash char(8);`

## D20 - `/usr/squeezecenter/Slim/Utils/ArtworkCache.pm`

SHA-256: `c2e7f94d84ffdcba5165e98ee7fda5e427b17bb9fc57200d42f03954b24d5cf4`

- Line 26: `package Slim::Utils::DbArtworkCache;`
- Line 38: `$root = Slim::Utils::Prefs::preferences('server')->get('librarycachedir');`
- Line 40: `# Update root value if librarycachedir changes`
- Line 45: `}, 'librarycachedir' );`

## D21 - `/usr/squeezecenter/Slim/Utils/DbCache.pm`

SHA-256: `00342eb28a399e5a6a1d6f7e55749b38d247e4a05d69951aa99bf5f79e3db1b8`

- Line 180: `sub _get_dbfile {`
- Line 197: `sub _init_db {`
- Line 240: `unlink $dbfile;`

## D22 - `/usr/squeezecenter/gdresized.pl`

SHA-256: `4aead3097c249517ab361c530ff8925b16e5ddfb824d34d165d80d5a64afcf24`

- Line 19: `use constant SOCKET_PATH  => '/tmp/sbs_artwork';`
- Line 92: `if ( -e SOCKET_PATH ) {`
- Line 93: `unlink SOCKET_PATH || die "Unable to remove old socket";`
- Line 97: `my $socket = IO::Socket::UNIX->new(`
- Line 99: `Local  => SOCKET_PATH,`
- Line 106: `DEBUG && warn "SIG received, removing " . SOCKET_PATH . "\n";`
- Line 107: `unlink SOCKET_PATH if -e SOCKET_PATH;`
- Line 113: `DEBUG && warn "$0 listening on " . SOCKET_PATH . "\n";`

## D23 - `/usr/squeezecenter/Slim/Utils/GDResizer.pm`

SHA-256: `1cddcf948899e4ff0b89ccd10cb4a8d5ea51e6be034ff7c79ef0fa5ffacfbe5d`

- Line 5: `use Image::Scale;`
- Line 105: `? Image::Scale->new( $file, { offset => $offset, length => $length } )`
- Line 106: `: Image::Scale->new($origref);`
- Line 199: `my $im = Image::Scale->new($ref) || return (0, 0);`

## D24 - `/usr/squeezecenter/Slim/Menu/BrowseLibrary.pm`

SHA-256: `a86c71f0485f2916ae9f66cda998a562f45ab62ba25d9ded5a37cb6f023255f2`

- Line 604: `sub getJiveMenu {`
- Line 1288: `sub _albums {`
- Line 1444: `commonVariables	=> [album_id => 'id'],`
- Line 1487: `sub _tracks {`

## D25 - `/usr/share/jive/applets/SlimBrowser/SlimBrowserApplet.lua`

SHA-256: `0d85ade9be0d47488378afa29dd78f87dee2f2718a5892868a57e945f9191e40`

- Line 704: `local function _performJSONAction(jsonAction, from, qty, step, sink, itemType, cachedResponse)`
- Line 817: `_server:userRequest(sink, playerid, request)`
- Line 852: `_server:userRequest(step.sink, playerid, step.jsonAction)`

## D26 - `/usr/share/jive/jive/slim/SlimServer.lua`

SHA-256: `8c6cc761c2bea423cff361b7a3516b400084e3eddea5fa5b2ae91b762ef8d90e`

- Line 1401: `function userRequest(self, func, ...)`
- Line 1435: `function request(self, ...)`

## D27 - `/usr/share/jive/jive/slim/LocalPlayer.lua`

SHA-256: `5f72349475aeebfe75cc9c550850d4531bf01f762a13d98c40b3556ef4fc9b76`

- Line 81: `obj.slimproto = SlimProto(jnt, {`
- Line 90: `obj.playback = Playback(jnt, obj.slimproto)`
- Line 190: `function connectToServer(self, server)`

## D28 - `/usr/share/jive/jive/audio/Playback.lua`

SHA-256: `8e06a00174707449858615e5a02452297420da4956864e19f03c0e9b3a1bea2c`

- Line 115: `decode:initAudio(slimproto)`
- Line 475: `function _streamConnect(self, serverIp, serverPort, reader, writer, slaves)`
- Line 852: `function _strm(self, data)`
- Line 947: `decode:start(string.byte(data.mode),`

## D29 - `/usr/squeezecenter/Slim/Networking/Slimproto.pm`

SHA-256: `4220262733c265d6a8d4fc91ff7da5e236db1a8e13b1b521d58dfadbe04e8d4f`

- Line 30: `use constant SLIMPROTO_PORT   => 3483;`
- Line 108: `my $listenerport = SLIMPROTO_PORT;`
- Line 112: `$listenerport = $main::SLIMPROTO_PORT;`
- Line 125: `LocalAddr => $main::localClientNetAddr,`
- Line 126: `LocalPort => $listenerport,`

## D30 - `/usr/squeezecenter/Slim/Player/Protocols/File.pm`

SHA-256: `1af47d1d772513b892a04d47a1e2f4ebca117750f116dc906a157030c93e1552`

- Line 50: `sub open {`
- Line 63: `my $filepath = Slim::Utils::Misc::pathFromFileURL($url);`
- Line 101: `if (!$sock->SUPER::open($filepath)) {`
- Line 133: `$seekdata->{sourceStreamOffset} = _timeToOffset($sock, $format, $song, $seekdata->{'timeOffset'});`
- Line 320: `sub _timeToOffset {`

## D31 - `/usr/squeezecenter/Slim/Player/StreamingController.pm`

SHA-256: `6c7ab2360fef8e5f96c583d05dee1d492fd8573b3a69f3ce398a12e70817360f`

- Line 333: `sub _Playing {`
- Line 395: `sub _Stopped {`
- Line 579: `sub _Stop {					# stop -> Stopped, Idle`
- Line 647: `# Otherwise, we use the repeat mode to decide which playlist entry to ask for.`
- Line 957: `sub _StopGetNext {			# stop, getNextTrack -> Stopped, TrackWait`
- Line 995: `sub _StopNextIfMore {		# -> Stopped, Idle; IF [moreTracks] THEN getNextTrack -> TrackWait ENDIF`
- Line 1383: `sub _PlayIfReady {		# -> Stopped; IF [trackReady] THEN play -> Buffering, Streaming ENDIF`
- Line 1397: `sub _PlayAndStream {		# -> PLAYING; IF [allReadyToStream] THEN play -> Streaming ENDIF`
- Line 1568: `sub _Pause {				# pause -> Paused`

## D32 - `/usr/squeezecenter/Slim/Player/Playlist.pm`

SHA-256: `032736ffda68f167dd400429ceaa2a577b22799d3cf169633cf983b729572ba7`

- Line 43: `sub shuffleType {`
- Line 165: `sub shuffleList {`
- Line 181: `sub addTracks {`
- Line 273: `sub shuffle {`
- Line 298: `sub repeat {`

## D33 - `/usr/squeezecenter/Slim/Web/HTTP.pm`

SHA-256: `4fce1f3740c3877851cb5387dd423be0867d34387ee99ef164ff0bd95ea5d763`

- Line 118: `if ( main::WEBUI ) {`
- Line 145: `Slim::Web::HTTP::openport($prefs->get('httpport'), $::httpaddr);`
- Line 243: `Slim::Web::HTTP::openport($prefs->get('httpport'), $::httpaddr);`
- Line 817: `# Bug 14825, allow multiple stream.mp3 clients from the same address with a player param`
- Line 1148: `main::INFOLOG && $log->is_info && $log->info("Disabling keep-alive for stream.mp3");`
- Line 1263: `if ( main::WEBUI ) {`
- Line 1269: `if ( main::WEBUI ) {`
- Line 1275: `if ( main::WEBUI ) {`
- Line 2599: `sub protectURI { if ( main::WEBUI ) {`
- Line 2604: `sub protectName { if ( main::WEBUI ) {`
- Line 2609: `sub protectCommand { if ( main::WEBUI ) {`
- Line 2614: `sub protect { if ( main::WEBUI ) {`

## D34 - `/usr/squeezecenter/Slim/Web/JSONRPC.pm`

SHA-256: `480f57f850aee14af88761db0a5ca1b44889b3fea2925a3a3672181da5e93e1d`

- Line 41: `Slim::Web::Pages->addRawFunction('jsonrpc.js', \&handleURI);`

## D35 - `/usr/squeezecenter/Slim/Web/Cometd.pm`

SHA-256: `fd19b8d6f0f383277b99763160409b1d9392f31e03f8961c50022296bacdcaa8`

- Line 56: `Slim::Web::Pages->addRawFunction( '/cometd', \&webHandler );`

## D36 - `/usr/squeezecenter/Slim/Plugin/CLI/Plugin.pm`

SHA-256: `6d36404a2a8b471953ff5a245cfabeadd52e07a07142657a25661955c94446f3`

- Line 71: `$prefs->set('cliport', Slim::Utils::Prefs::OldPrefs->get('cliport') || 9090); 1;`
- Line 159: `sub cli_socket_open {`

## D37 - `/usr/share/jive/applets/NowPlaying/NowPlayingApplet.lua`

SHA-256: `eab17c7e30d533160dad360015ada4e83496823f638142272a97e14c65a9f21b`

- Line 386: `function notify_playerShuffleModeChange(self, player, shuffleMode)`
- Line 390: `log:debug("notify_playerShuffleModeChange(): ", shuffleMode)`
- Line 395: `function notify_playerDigitalVolumeControl(self, player, digitalVolumeControl)`
- Line 399: `log:info('notify_playerDigitalVolumeControl: ', digitalVolumeControl)`
- Line 404: `function notify_playerRepeatModeChange(self, player, repeatMode)`
- Line 408: `log:debug("notify_playerRepeatModeChange(): ", repeatMode)`
- Line 469: `function notify_playerTitleStatus(self, player, text, duration)`
- Line 473: `log:debug("notify_playerTitleStatus(): ", text)`
- Line 477: `function notify_playerPower(self, player, power)`
- Line 481: `log:debug("notify_playerPower(): ", power)`
- Line 504: `function notify_playerTrackChange(self, player, nowPlaying)`
- Line 508: `log:debug("notify_playerTrackChange(): ", nowPlaying)`
- Line 547: `function notify_playerPlaylistChange(self, player)`
- Line 551: `log:debug("notify_playerPlaylistChange()")`

## D38 - `/usr/share/jive/applets/SqueezeboxFab4/SqueezeboxFab4Meta.lua`

SHA-256: `03b785a5f77982e4eb28d40f9446faab6fc974b0b4ef689c12953b12dcf02a6c`

- Line 29: `alsaPlaybackDevice = "default",`
- Line 32: `alsaEffectsDevice = "plughw:2,0",`
- Line 62: `LocalPlayer:setDeviceType("fab4", "Squeezebox Touch")`

## D39 - `/etc/asound.conf`

SHA-256: `5cfab7210ac08e506a36bd6b42f2ba5308f2ff4c0ca42c65a3750d9e2c0f517b`

- Line 5: `pcm.digital {`
- Line 16: `pcm.ak4420 {`
- Line 27: `pcm.multi {`
- Line 88: `pcm.!default plugmulti`

## D40 - `/usr/squeezecenter/slimserver.pl`

SHA-256: `9f713a83e59be6b387667b1c6b3fbaf9ef53435b4ac1ed811866ca2dc20d7eb2`

- Line 20: `use constant TRANSCODING  => ( grep { /--notranscoding/ } @ARGV ) ? 0 : 1;`
- Line 26: `use constant WEBUI        => ( grep { /--noweb/ } @ARGV ) ? 0 : 1;`
- Line 313: `$noweb,`
- Line 724: `[--noweb] [--notranscoding] [--nosb1slimp3sync] [--nostatistics] [--norestart]`
- Line 766: `--noweb          => Disable web interface. JSON-RPC, Comet, and artwork web APIs are still enabled.`
- Line 810: `'playeraddr=s'  => \$localClientNetAddr,`
- Line 828: `'noweb'         => \$noweb,`
