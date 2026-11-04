#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "music_playback.h"
#include "music_browse.h"
#include "music_stream.h"
#include "music_http.h"
#include "music_scanner.h"
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static sb_music_media medium = {{{1}}, 1, 1};
static sb_stream_registry *registry;
static unsigned starts, commands;
static char last_token[65];
static sb_music_playback *playback;
static sb_physical_owner owner;

static void rest(void)
{
	struct timespec t = {0, 1000000};
	nanosleep(&t, NULL);
}

static sb_ml_result exchange(sb_music_library *db, sb_ml_request *request)
{
	sb_ml_result result;
	assert(sb_music_library_exchange(db, request, &result) == SB_ML_OK);
	return result;
}

static int send_frame(void *unused,
		      const unsigned char *frame,
		      size_t length,
		      const sb_music_stream_id *stream)
{
	(void)unused;
	assert(length >= 7 && !memcmp(frame + 2, "strm", 4));
	assert(stream->connection_generation && stream->owner_generation);
	commands++;
	if (frame[6] == 's') {
		starts++;
		const char *prefix = "/music/stream/";
		const unsigned char *path = NULL;
		for (size_t n = 0; n + 14 + 64 <= length; n++)
			if (!memcmp(frame + n, prefix, 14)) {
				path = frame + n + 14;
				break;
			}
		assert(path);
		memcpy(last_token, path, 64);
		last_token[64] = 0;
	}
	return 1;
}

static void tick(void)
{
	uint64_t now = np_monotonic_ms();
	sb_stream_registry_tick(registry, now);
	sb_playback_tick(playback, now);
}

static void phase(sb_play_phase expected)
{
	uint64_t until = np_monotonic_ms() + 25000;
	for (;;) {
		tick();
		if (sb_playback_phase(playback) == expected)
			return;
		if (sb_playback_phase(playback) == SB_PLAY_FAILED ||
		    np_monotonic_ms() >= until) {
			fprintf(stderr,
				"phase=%d expected=%d error=%s\n",
				sb_playback_phase(playback),
				expected,
				sb_playback_error(playback));
			assert(!"phase failed");
		}
		rest();
	}
}

static void settle(void)
{
	for (unsigned i = 0; i < 25; i++) {
		tick();
		rest();
	}
}

static void attach(unsigned connection)
{
	sb_slim_hello hello = {0};
	hello.codecs = SB_SLIM_CODEC_MP3 | SB_SLIM_CODEC_FLAC;
	assert(sb_playback_attach(playback, connection, &hello));
	assert(!sb_playback_idle_proof(playback,
				       99,
				       connection,
				       owner.value.generation,
				       np_monotonic_ms(),
				       1,
				       1,
				       0));
	uint64_t until = np_monotonic_ms() + 5000;
	while (!sb_playback_idle_proof(playback,
				       99,
				       connection,
				       owner.value.generation,
				       np_monotonic_ms(),
				       1,
				       1,
				       1)) {
		assert(np_monotonic_ms() < until);
		tick();
		rest();
	}
}

static void event(const char *code, unsigned jiffies, unsigned elapsed)
{
	sb_slim_event e = {0};
	e.kind = SB_SLIM_STAT;
	memcpy(e.code, code, 4);
	e.jiffies = jiffies;
	e.elapsed_ms = elapsed;
	assert(sb_playback_event(
		playback,
		sb_playback_stream(playback)->connection_generation,
		sb_playback_stream(playback),
		&e,
		np_monotonic_ms()));
}

int main(int argc, char **argv)
{
	char directory[] = "/dev/shm/sb-music-playback-XXXXXX";
	assert(mkdtemp(directory));
	char dbpath[] = "/tmp/sb-music-playback-db-XXXXXX";
	int dbfd = mkstemp(dbpath);
	assert(dbfd >= 0);
	close(dbfd);
	sb_ml_options options = {0};
	strcpy(options.path, dbpath);
	options.create = 1;
	sb_music_library *db = sb_music_library_start(&options);
	assert(db);
	sb_ml_result opened;
	while (sb_music_library_receive(db, &opened) == SB_ML_BUSY)
		rest();
	assert(opened.status == SB_ML_OK);
	sb_ml_request request = {0};
	request.operation = SB_ML_SOURCE;
	request.data.media = medium;
	exchange(db, &request);
	if (argc >= 3) {
		/* Qualify the empty fixture's scan scope before manually
		 * indexing its initial tracks, exactly as WP2 requires. Never
		 * rebind a nonempty unqualified user library to a guessed
		 * filesystem root. */
		request = (sb_ml_request){0};
		request.operation = SB_ML_SCAN_START;
		request.data.track.identity.source = medium.source;
		request.data.track.media_generation = medium.generation;
		strcpy(request.data.track.path, directory + 9);
		uint64_t scan_epoch = exchange(db, &request).record_id;
		request = (sb_ml_request){0};
		request.operation = SB_ML_SCAN_ABORT;
		request.scan_token = scan_epoch;
		exchange(db, &request);
		request = (sb_ml_request){0};
	}
	request.operation = SB_ML_BEGIN;
	request.transaction = 1;
	exchange(db, &request);
	uint64_t ids[3];
	for (unsigned i = 0; i < 3; i++) {
		sb_ml_track track = {0};
		track.identity.source = medium.source;
		track.media_generation = 1;
		snprintf(track.path,
			 sizeof track.path,
			 "track%u.%s",
			 i,
			 i == 0	  ? "mp3"
			 : i == 1 ? "flac"
				  : "ogg");
		snprintf(track.title, sizeof track.title, "Track %u", i);
		strcpy(track.album, "Album");
		strcpy(track.album_key, "album");
		strcpy(track.album_artist, "Artist");
		strcpy(track.artists[0], "Artist");
		track.artist_count = 1;
		strcpy(track.codec, i == 0 ? "mp3" : i == 1 ? "flac" : "ogg");
		track.track = i + 1;
		track.disc = i + 1;
		track.duration_ms = 10000;
		char path[8192];
		snprintf(path, sizeof path, "%s/%s", directory, track.path);
		int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
		assert(fd >= 0);
		if (argc >= 3) {
			char source[4096];
			snprintf(source,
				 sizeof source,
				 "%s/%s",
				 argv[2],
				 i == 0	  ? "base.mp3"
				 : i == 1 ? "comments.flac"
					  : "comments.ogg");
			int input = open(source, O_RDONLY);
			assert(input >= 0);
			unsigned char buffer[4096];
			ssize_t bytes;
			while ((bytes = read(input, buffer, sizeof buffer)) > 0)
				assert(write(fd, buffer, (size_t)bytes) ==
				       bytes);
			assert(!bytes);
			close(input);
		} else
			assert(write(fd, "encoded fixture bytes", 21) == 21);
		struct stat st;
		assert(!fstat(fd, &st));
		close(fd);
		track.file_size = (uint64_t)st.st_size;
		track.mtime = st.st_mtime;
		request.operation = SB_ML_UPSERT;
		request.data.track = track;
		ids[i] = exchange(db, &request).record_id;
	}
	request.operation = SB_ML_COMMIT;
	exchange(db, &request);
	sb_storage_mounts *mounts = malloc(sizeof *mounts);
	assert(mounts);
	assert(sb_storage_read_mounts(NULL, mounts) == SB_STORAGE_OK);
	sb_storage_mount mount = {0};
	for (unsigned n = 0; n < mounts->count; n++)
		if (!strcmp(mounts->rows[n].path, "/dev/shm"))
			mount = mounts->rows[n];
	free(mounts);
	assert(mount.mount_id);
	sb_storage_root whole, root;
	assert(sb_storage_validate(
		       "/dev", &mount, &medium, NULL, NULL, &whole) ==
	       SB_STORAGE_OK);
	assert(sb_storage_root_child(
		       &whole, &medium, directory + 9, NULL, NULL, &root) ==
	       SB_STORAGE_OK);
	sb_storage_root_close(&whole);
	sb_browse *browse = sb_browse_start(db, &medium, 1, 1);
	assert(browse);
	sb_browse_player(browse, "02:00:00:00:00:01");
	np_manager *np = np_create(120, 32);
	assert(np);
	assert(sb_owner_init(&owner, 1));
	assert(sb_owner_register(&owner, SB_OWNER_MUSIC, 99));
	assert(sb_owner_register(&owner, SB_OWNER_RADIO, 77));
	int ipc[2];
	assert(sb_resource_pair(ipc));
	assert(fcntl(ipc[0], F_SETFD, 0) == 0);
	registry = sb_stream_registry_start(1, ipc[1]);
	assert(registry);
	playback = sb_playback_create(1,
				      sb_browse_dispatcher(browse),
				      &root,
				      &owner,
				      np,
				      ipc[0],
				      8080,
				      send_frame,
				      NULL);
	assert(playback);
	assert(fcntl(ipc[0], F_GETFD) & FD_CLOEXEC);
	attach(1);
	uint64_t context =
		sb_queue_snapshot(sb_playback_queue(playback)).context;
	assert(sb_playback_load(playback,
				SB_Q_LOAD,
				ids[0],
				0,
				context,
				np_monotonic_ms()) == SB_Q_OK);
	phase(SB_PLAY_ACTIVE);
	assert(starts == 1);
	assert(sb_queue_snapshot(sb_playback_queue(playback)).audible ==
	       SB_QUEUE_NONE);
	assert(!np_current(np)->track.title[0]);
	sb_slim_event response = {0};
	response.kind = SB_SLIM_RESP;
	response.http_status = 200;
	assert(sb_playback_event(playback,
				 1,
				 sb_playback_stream(playback),
				 &response,
				 np_monotonic_ms()));
	event("STMs", 1, 0);
	assert(!strcmp(np_current(np)->track.title, "Track 0"));
	event("STMt", 2, 1000);
	assert(np_position(np) == 1000);
	assert(sb_playback_control(playback, 'p', context, np_monotonic_ms()));
	event("STMp", 3, 1000);
	rest();
	assert(np_position(np) == 1000);
	assert(sb_playback_control(playback, 'u', context, np_monotonic_ms()));
	event("STMr", 4, 1000);
	assert(sb_playback_load(playback,
				SB_Q_ADD,
				ids[1],
				0,
				context,
				np_monotonic_ms()) == SB_Q_OK);
	phase(SB_PLAY_ACTIVE);
	assert(sb_queue_snapshot(sb_playback_queue(playback)).count == 2);
	/* Failed Ogg load cannot clear queue, revoke the active stream or
	 * start. */
	assert(sb_playback_load(playback,
				SB_Q_LOAD,
				ids[2],
				0,
				context,
				np_monotonic_ms()) == SB_Q_OK);
	uint64_t until = np_monotonic_ms() + 6000;
	while (sb_playback_phase(playback) != SB_PLAY_FAILED) {
		tick();
		rest();
		assert(np_monotonic_ms() < until);
	}
	assert(starts == 1 &&
	       sb_queue_snapshot(sb_playback_queue(playback)).count == 2);
	unsigned char token[32];
	assert(sb_music_token_decode(last_token, 64, token));
	settle();
	assert(sb_stream_info(registry,
			      token,
			      SB_RESOURCE_GET,
			      np_monotonic_ms(),
			      NULL) == 200);
	event("STMd", 5, 10000);
	assert(starts == 1);
	sb_music_stream_id old = *sb_playback_stream(playback);
	event("STMu", 6, 10000);
	assert(sb_playback_phase(playback) == SB_PLAY_NEED_CONNECTION);
	assert(sb_queue_snapshot(sb_playback_queue(playback)).intended == 1);
	assert(!sb_playback_event(playback,
				  1,
				  &old,
				  &(sb_slim_event){.kind = SB_SLIM_DSCO},
				  np_monotonic_ms()));
	settle();
	sb_playback_detach(playback, 1);
	attach(2);
	phase(SB_PLAY_ACTIVE);
	assert(starts == 2);
	assert(!sb_playback_event(
		playback, 1, &old, &response, np_monotonic_ms()));
	assert(sb_playback_event(playback,
				 2,
				 sb_playback_stream(playback),
				 &response,
				 np_monotonic_ms()));
	event("STMs", 1, 0);
	assert(!strcmp(np_current(np)->track.title, "Track 1"));
	char status[16384];
	assert(sb_playback_status(playback, status, sizeof status));
	assert(strstr(status, "\"playlist_tracks\":2") &&
	       strstr(status, "\"playlist_cur_index\":1"));
	unsigned scan_files = 0;
	if (argc >= 3) {
		char source[4096];
		snprintf(source, sizeof source, "%s/comments.flac", argv[2]);
		int input = open(source, O_RDONLY);
		assert(input >= 0);
		unsigned char bytes[16384];
		ssize_t length = read(input, bytes, sizeof bytes);
		assert(length > 0 && length < (ssize_t)sizeof bytes);
		close(input);
		scan_files = 1000;
		for (unsigned n = 0; n < scan_files; n++) {
			char path[4096];
			snprintf(path,
				 sizeof path,
				 "%s/scan%u.flac",
				 directory,
				 n);
			int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
			assert(fd >= 0);
			assert(write(fd, bytes, (size_t)length) == length);
			close(fd);
		}
		sb_scan_options scan = {0};
		scan.enabled = scan.approved_database = scan.inode_identity = 1;
		scan.library = db;
		scan.root = root;
		scan.batch_files = 16;
		scan.work_duty_percent = 100;
		scan.helper_timeout_ms = 500;
		snprintf(scan.helper, sizeof scan.helper, "%s", argv[1]);
		sb_scan_code code;
		sb_music_scanner *scanner = sb_music_scan_create(&scan, &code);
		assert(scanner && code == SB_SCAN_OK);
		assert(sb_browse_media(browse, &medium, 1));
		assert(sb_music_scan_request(scanner, 1, 1) == SB_SCAN_OK);
		uint64_t began = np_monotonic_ms(), query_max = 0,
			 query_started = 0;
		unsigned queries = 0, successful_queries = 0, retryable = 0;
		sb_browse_call *call = NULL;
		sb_scan_status progress = {0};
		while (progress.state < SB_SCAN_COMPLETED) {
			tick();
			sb_browse_tick(browse, np_monotonic_ms());
			if (!call) {
				sb_lms_command cmd = {0};
				strcpy(cmd.player, "02:00:00:00:00:01");
				cmd.count = 6;
				strcpy(cmd.arguments[0], "browselibrary");
				strcpy(cmd.arguments[1], "items");
				strcpy(cmd.arguments[2], "0");
				strcpy(cmd.arguments[3], "50");
				strcpy(cmd.arguments[4], "mode:tracks");
				strcpy(cmd.arguments[5], "menu:1");
				query_started = np_monotonic_ms();
				call = sb_browse_begin(
					browse, &cmd, query_started + 5000);
				assert(call && sb_browse_valid(call));
			}
			if (sb_browse_poll(browse, call)) {
				uint64_t elapsed =
					np_monotonic_ms() - query_started;
				if (elapsed > query_max)
					query_max = elapsed;
				if (sb_browse_success(call))
					successful_queries++;
				else {
					const char *error =
						sb_browse_json(call);
					assert(strstr(error, "stale") ||
					       strstr(error, "conflict") ||
					       strstr(error, "timeout") ||
					       strstr(error, "busy"));
					retryable++;
				}
				queries++;
				sb_browse_cancel(browse, call);
				call = NULL;
			}
			assert(sb_queue_snapshot(sb_playback_queue(playback))
				       .mode == SB_Q_PLAYING);
			sb_scan_code observed =
				sb_music_scan_status(scanner, &progress);
			assert(observed == SB_SCAN_OK ||
			       observed == SB_SCAN_BUSY);
			assert(np_monotonic_ms() - began < 120000);
			rest();
		}
		if (call)
			sb_browse_cancel(browse, call);
		if (progress.state != SB_SCAN_COMPLETED || progress.errors) {
			fprintf(stderr,
				"scanner state=%u errors=%" PRIu64 "\n",
				progress.state,
				progress.errors);
			for (unsigned n = 0; n < progress.error_count; n++)
				fprintf(stderr,
					"%s: %s\n",
					progress.error[n].path,
					progress.error[n].message);
		}
		assert(progress.state == SB_SCAN_COMPLETED &&
		       !progress.errors && successful_queries);
		while (sb_music_scan_stop(scanner) == SB_SCAN_BUSY)
			rest();
		while (sb_music_scan_destroy(scanner) == SB_SCAN_BUSY)
			rest();
		assert(sb_browse_media(browse, &medium, 0));
		request = (sb_ml_request){0};
		request.operation = SB_ML_QUERY;
		request.data.query.media = medium;
		request.data.query.kind = SB_QUERY_TRACKS;
		request.data.query.limit = 1;
		assert(exchange(db, &request).data.query.page.total ==
		       scan_files + 3);
		printf("{\"actual_scanner_files\":%u,\"concurrent_browse_"
		       "queries\":%u,\"successful_queries\":%u,"
		       "\"retryable_queries\":%u,\"query_max_ms\":%" PRIu64
		       ",\"scan_ms\":%" PRIu64
		       ",\"decoder\":\"mocked playing\"}\n",
		       scan_files,
		       queries,
		       successful_queries,
		       retryable,
		       query_max,
		       np_monotonic_ms() - began);
	}
	event("STMo", 2, 0);
	assert(np_current(np)->state == NP_BUFFERING);
    /* Production bridge cleanup cannot skip private RESET acknowledgement. */
    assert(!sb_playback_bridge_ready(playback));
    assert(sb_playback_bridge_begin(playback,np_monotonic_ms()));
    assert(!sb_playback_bridge_ready(playback));
    assert(!sb_playback_bridge_proof(playback,99,2,owner.value.generation,np_monotonic_ms()));
    settle();
    assert(sb_playback_bridge_ready(playback));
    assert(owner.value.current==SB_OWNER_MUSIC); /* resource cleanup is not decoder proof */
	sb_playback_media_removed(playback, &medium);
	settle();
	assert(!sb_queue_at(sb_playback_queue(playback), 0)->available);
	assert(sb_playback_phase(playback) == SB_PLAY_FAILED);
	assert(sb_playback_load(playback,
				SB_Q_LOAD,
				ids[0],
				0,
				context,
				np_monotonic_ms()) == SB_Q_STALE);
	assert(sb_playback_idle_proof(playback,
				      99,
				      2,
				      owner.value.generation,
				      np_monotonic_ms(),
				      1,
				      1,
				      1));
	while (!sb_playback_destroy(playback))
		rest();
	while (!sb_stream_registry_stop(registry))
		rest();
	while (!sb_stream_registry_destroy(registry))
		rest();
	while (sb_browse_stop(browse) == SB_ML_BUSY)
		rest();
	while (sb_browse_destroy(browse) == SB_ML_BUSY)
		rest();
	request.operation = SB_ML_CLOSE;
	exchange(db, &request);
	assert(sb_music_library_destroy(db) == SB_ML_OK);
	sb_storage_root_close(&root);
	np_destroy(np);
	for (unsigned i = 0; i < scan_files; i++) {
		char path[4096];
		snprintf(path, sizeof path, "%s/scan%u.flac", directory, i);
		assert(!unlink(path));
	}
	for (unsigned i = 0; i < 3; i++) {
		char path[1024];
		snprintf(path,
			 sizeof path,
			 "%s/track%u.%s",
			 directory,
			 i,
			 i == 0	  ? "mp3"
			 : i == 1 ? "flac"
				  : "ogg");
		assert(!unlink(path));
	}
	assert(!rmdir(directory));
	assert(!unlink(dbpath));
	printf("Mock decoder integration: %u starts, %u commands; real scoped "
	       "opens/IPC, atomic queue, STAT metadata, next-connection and "
	       "removal passed\n",
	       starts,
	       commands);
	return 0;
}
