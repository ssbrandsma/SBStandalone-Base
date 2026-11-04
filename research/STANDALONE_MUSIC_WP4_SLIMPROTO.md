# WP4 SlimProto, queue transitions and status

The [coordinator](../native/sbbase/src/music_playback.c) uses the existing [WP3a controller](../native/sbbase/src/slim_controller.c), serializers and scoped nonblocking output queue. Original firmware/LMS source contracts are retained; no decoder, ALSA, transcoder or new audio engine is introduced.

## Start and event sequence

1. Resolve/stage a source-qualified indexed selection through WP1 asynchronously.
2. Validate MP3/FLAC against negotiated HELO mp3/flc bits. Scanner recognition of Ogg grants no playback capability.
3. Securely open and version-pin the first file on a worker; reserve its private HTTP capability while it remains inactive.
4. Request Music physical ownership. Missing adapter, refused yield, timeout or stale callback fails closed; metadata leases cannot satisfy this step.
5. Require current connection/owner quiescence and a connection not previously used for a native stream. Activate the capability using the acknowledged owner generation.
6. Prepare and issue owned strm-s, enqueue its generated loopback HTTP request with connection/owner/stream scope, then atomically commit the queue and mark buffering. Admission is not delivery or audio confirmation.
7. RESP must report200; matching STMs confirms the audible entry and publishes its metadata. Intended next entries remain separate.

Outgoing start uses codec m/f, generated opaque resource token and validated existing webserver port/address127.0.0.1. Pause/unpause/stop use p/u/q. Existing heartbeat handling remains. No native seek, ReplayGain, audg gain override, output routing rewrite, gapless/prebuffer or crossfade is introduced; firmware local volume and existing PCM/ALSA/output preferences remain authoritative and need hardware qualification.

## Terminal and reconnect policy

WP3a accepts one native stream per connection. WP4 does not weaken that boundary or invent an epoch inside received STAT packets. A successful transition requires audible start plus decoder-ready STMd plus output-drained STMu. STMd or normal DSCO input EOF alone never advances. Duplicate/stale terminal events are processed at most once; HTTP errors, decoder errors, timeouts and disconnect are failures, not successful ends.

After a successful drain or explicit next/previous/restart, the queue selects intent and waits for a fresh SlimProto connection. The Base fixture bridge requests reconnect by draining/closing the old socket. HELO alone gives no decoder permission. The native adapter must acknowledge producer/hook/decoder quiescence on the fresh tuple, release/reacquire ownership as required, and activate a fresh resource/epoch. This is sequential playback with a deliberate reconnect gap, not gapless playback. Old-connection events cannot mutate the new stream.

Stop sends q, revokes resources and freezes position; STMf only confirms input flush, not decoder/output silence. Missing stop proof cannot grant another owner. Destruction revokes the private channel and quarantines the Music adapter rather than treating thread exit as decoder proof. Buffer underrun reports buffering and retains only confirmed position; automatic underrun recovery is not implemented. Explicit stop/play can restart on a newly qualified connection.

## Now Playing and Jive status

The trusted native publisher uses existing bounded np_status metadata structures, only while physical Music control is admitted. External Radio/Spotify/Qobuz metadata/control APIs keep their existing behavior. Native position is a STAT observation with position_clock_ms=0; it never advances simply because host time passes after pause, stop or disconnect.

Status exposes accurate queue count, audible index, intended index, queue revision, context, codec/source and confirmed elapsed/duration. `playlist_loop` supports bounded offset/quantity pagination up to200 rows. `item_loop` contains the confirmed audible item with string `params.track_id` and numeric playlist_index. Before STMs/no current item, the key is omitted: stock `jive/slim/Player.lua:_whatsPlaying` at272 indexes item_loop[1] when the table exists, so an empty Lua table is unsafe. Track IDs/entry IDs/context are strings to avoid Lua numeric precision loss. Artwork uses an empty/default placeholder; no broken local filesystem URL or artwork rendering endpoint is fabricated.

The protocol subset follows LMS7.7.3 Commands.pm playlistcontrolCommand1746, playlistJumpCommand900 and playlistClearCommand764, plus WP3b XMLBrowser go/play/add/commonParams action shapes. Unsupported actions are not advertised. Actual host fixture command/reply/start examples are recorded separately; all firmware events in them are mocked, not captured hardware playback.

Local status omits `remote` and unsupported `can_seek` fields: Lua treats numeric zero as true. `rate` is 1 while playing and 0 otherwise, satisfying stock Player:getTrackElapsed arithmetic. The native Now Playing publisher remains STAT-only; stock Jive may interpolate its own display between status observations. Seeking is not implemented or advertised.
