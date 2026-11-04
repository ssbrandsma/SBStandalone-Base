#define _GNU_SOURCE
#include "music_browse.h"
#include "nowplaying.h"
#include "music_scanner.h"
#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
static int long_names;
static sb_music_media media = {{{1}}, 1, 1};

static void rest(unsigned ms)
{
	struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000};
	nanosleep(&t, NULL);
}

static sb_ml_result wait_open(sb_music_library *h)
{
	sb_ml_result r;
	while (sb_music_library_receive(h, &r) == SB_ML_BUSY) {
		struct pollfd p = {sb_music_library_fd(h), POLLIN, 0};
		assert(poll(&p, 1, 5000) > 0);
	}
	return r;
}

static sb_ml_result exchange(sb_music_library *h, sb_ml_request *q)
{
	sb_ml_result r;
	sb_ml_status s = sb_music_library_exchange(h, q, &r);
	assert(s == SB_ML_OK);
	return r;
}

static sb_music_library *open_db(const char *path, int create)
{
	sb_ml_options o = {0};
	snprintf(o.path, sizeof o.path, "%s", path);
	o.create = create;
	o.busy_ms = 20;
	sb_music_library *h = sb_music_library_start(&o);
	assert(h);
	assert(wait_open(h).status == SB_ML_OK);
	sb_ml_request q = {0};
	q.operation = SB_ML_SOURCE;
	q.data.media = media;
	exchange(h, &q);
	return h;
}

static void close_db(sb_music_library *h)
{
	sb_ml_request q = {0};
	q.operation = SB_ML_CLOSE;
	exchange(h, &q);
	assert(sb_music_library_destroy(h) == SB_ML_OK);
}

static void fixture(sb_ml_track *t, unsigned i)
{
	memset(t, 0, sizeof *t);
	t->identity.source = media.source;
	t->media_generation = 1;
	unsigned album = i / 10;
	t->disc = i % 10 / 5 + 1;
	t->track = i % 5 + 1;
	t->year = 2000 + album % 20;
	snprintf(t->path,
		 sizeof t->path,
		 "folder%05u/disc%u/track%05u.flac",
		 album,
		 t->disc,
		 i);
	snprintf(t->title, sizeof t->title, "Track %05u", i);
	if (i % 31 == 0)
		strcpy(t->title, "Duplicate");
	if (i == 7)
		strcpy(t->title, "Caf\xc3\xa9 \xf0\x9f\x8e\xb5");
	if (i == 8)
		strcpy(t->title, "Quote \" and slash \\");
	snprintf(t->album, sizeof t->album, "Album %02u", album % 7);
	snprintf(t->album_key, sizeof t->album_key, "release-%05u", album);
	snprintf(t->album_artist,
		 sizeof t->album_artist,
		 "Album Artist %02u",
		 album % 20);
	if (album % 3 == 0) {
		t->compilation = 1;
		strcpy(t->album_artist, "Various Artists");
	}
	if (long_names) {
		size_t a = strlen(t->album), b = strlen(t->album_artist);
		memset(t->album + a, 'L', 200);
		t->album[a + 200] = 0;
		memset(t->album_artist + b, 'R', 200);
		t->album_artist[b + 200] = 0;
	}
	t->artist_count = 1;
	snprintf(t->artists[0], sizeof t->artists[0], "Artist %02u", i % 40);
	if (i % 7 == 0) {
		strcpy(t->artists[1], "Guest");
		t->artist_count = 2;
	}
	t->genre_count = 1;
	snprintf(t->genres[0], sizeof t->genres[0], "Genre %u", i % 8);
	strcpy(t->codec, "flac");
	t->duration_ms = 180000 + i;
	t->sample_rate = 44100;
	t->channels = 2;
	t->file_size = 298;
	t->mtime = 1700000000 + i;
	snprintf(t->fingerprint, sizeof t->fingerprint, "fixture-%u", i);
}

static void populate(sb_music_library *h, unsigned n)
{
	sb_ml_request q = {0};
	q.operation = SB_ML_BEGIN;
	q.transaction = 99;
	exchange(h, &q);
	for (unsigned i = 0; i < n; i++) {
		q.operation = SB_ML_UPSERT;
		fixture(&q.data.track, i);
		exchange(h, &q);
	}
	q.operation = SB_ML_COMMIT;
	exchange(h, &q);
}

static sb_lms_command
command(const char *mode, unsigned offset, unsigned quantity)
{
	sb_lms_command c = {0};
	strcpy(c.player, "02:00:00:00:00:01");
	c.count = 6;
	strcpy(c.arguments[0], "browselibrary");
	strcpy(c.arguments[1], "items");
	snprintf(c.arguments[2], 512, "%u", offset);
	snprintf(c.arguments[3], 512, "%u", quantity);
	snprintf(c.arguments[4], 512, "mode:%s", mode);
	strcpy(c.arguments[5], "menu:1");
	return c;
}

static sb_browse_call *browse(sb_browse *b, const sb_lms_command *cmd)
{
	sb_browse_call *c = sb_browse_begin(b, cmd, np_monotonic_ms() + 5000);
	assert(c);
	uint64_t until = np_monotonic_ms() + 6000;
	while (!sb_browse_poll(b, c)) {
		assert(np_monotonic_ms() < until);
		struct pollfd p = {sb_browse_fd(b), POLLIN, 0};
		poll(&p, 1, 10);
	}
	return c;
}

typedef struct {
	sb_music_library *library;
} writer_args;

static void *writer(void *opaque)
{
	writer_args *args = opaque;
	sb_ml_request q = {0};
	q.operation = SB_ML_BEGIN;
	q.transaction = 112;
	exchange(args->library, &q);
	rest(40);
	q.operation = SB_ML_UPSERT;
	fixture(&q.data.track, 1000);
	exchange(args->library, &q);
	q.operation = SB_ML_COMMIT;
	exchange(args->library, &q);
	return NULL;
}

static void stop(sb_browse *b)
{
	while (sb_browse_stop(b) == SB_ML_BUSY)
		rest(1);
	uint64_t until = np_monotonic_ms() + 5000;
	while (sb_browse_destroy(b) == SB_ML_BUSY) {
		assert(np_monotonic_ms() < until);
		rest(1);
	}
}

static void native_tests(sb_music_library *h)
{
	assert(!sb_browse_start(h, &media, 0, 1));
	assert(!sb_browse_start(h, &media, 1, 0));
	sb_browse *b = sb_browse_start(h, &media, 1, 1);
	assert(b);
	sb_browse_player(b, "02:00:00:00:00:01");
	assert(!sb_music_async_start(h, 1));
	const char *modes[] = {"artists", "albums", "tracks", "genres", "bmf"};
	for (unsigned i = 0; i < 5; i++) {
		sb_lms_command cmd = command(modes[i], 0, 200);
		sb_browse_call *c = browse(b, &cmd);
		if (!sb_browse_success(c))
			fprintf(stderr,
				"%s: %s\n",
				modes[i],
				sb_browse_json(c));
		assert(sb_browse_success(c));
		assert(!strstr(sb_browse_json(c), "playlistcontrol"));
		assert(!strstr(sb_browse_json(c), "\"play\""));
		assert(!strstr(sb_browse_json(c), "file://"));
		if (i == 2)
			assert(strstr(sb_browse_json(c),
				      "\"action\":\"none\""));
		sb_browse_cancel(b, c);
	}
	sb_lms_command cmd = command("albums", 0, 201);
	sb_browse_call *c = browse(b, &cmd);
	assert(!sb_browse_success(c));
	sb_browse_cancel(b, c);
	cmd = command("albums", 0, 50);
	strcpy(cmd.arguments[2], "-1");
	c = browse(b, &cmd);
	assert(!sb_browse_success(c));
	sb_browse_cancel(b, c);
	cmd = command("search", 0, 50);
	c = browse(b, &cmd);
	assert(!sb_browse_success(c));
	sb_browse_cancel(b, c);
	cmd = command("albums", 100000, 50);
	c = browse(b, &cmd);
	assert(sb_browse_success(c) &&
	       strstr(sb_browse_json(c), "\"item_loop\":[]"));
	sb_browse_cancel(b, c);
	cmd = command("albums", 0, 50);
	sb_browse_call *pending[8];
	for (unsigned i = 0; i < 8; i++) {
		pending[i] = sb_browse_begin(b, &cmd, np_monotonic_ms() + 5000);
		assert(pending[i]);
		(void)sb_browse_poll(b, pending[i]);
	}
	assert(!sb_browse_begin(b, &cmd, np_monotonic_ms() + 5000));
	for (unsigned i = 0; i < 8; i++)
		sb_browse_cancel(b, pending[i]);
	for (unsigned i = 0; i < 20; i++) {
		sb_browse_tick(b, np_monotonic_ms());
		rest(1);
	}
	/* Same mailbox/connection, writer-held transactions, no dirty reads or
	 * theft. */
	writer_args args = {h};
	pthread_t thread;
	assert(!pthread_create(&thread, NULL, writer, &args));
	rest(5);
	c = browse(b, &cmd);
	assert(sb_browse_success(c));
	sb_browse_cancel(b, c);
	pthread_join(thread, NULL);
	cmd = command("albums", 0, 200);
	c = sb_browse_begin(b, &cmd, np_monotonic_ms() + 5000);
	assert(c);
	uint64_t page_deadline = np_monotonic_ms() + 5000;
	while (!strstr(sb_browse_json(c), "item_loop")) {
		assert(!sb_browse_poll(b, c));
		rest(1);
		assert(np_monotonic_ms() < page_deadline);
	}
	sb_ml_request update = {0};
	update.operation = SB_ML_UPSERT;
	fixture(&update.data.track, 1001);
	exchange(h, &update);
	while (!sb_browse_poll(b, c)) {
		rest(1);
		assert(np_monotonic_ms() < page_deadline);
	}
	assert(!sb_browse_success(c) && strstr(sb_browse_json(c), "stale"));
	sb_browse_cancel(b, c);
	cmd = command("tracks", 0, 200);
	c = sb_browse_begin(b, &cmd, np_monotonic_ms() + 5000);
	assert(c);
	(void)sb_browse_poll(b, c);
	sb_music_media offline = media;
	offline.online = 0;
	assert(sb_browse_media(b, &offline, 0));
	assert(sb_browse_poll(b, c) && !sb_browse_success(c) &&
	       strstr(sb_browse_json(c), "media_changed"));
	sb_browse_cancel(b, c);
	assert(!sb_browse_media(b, &media, 0));
	media.generation = 2;
	assert(sb_browse_media(b, &media, 0));
	uint64_t until = np_monotonic_ms() + 5000;
	while (!sb_browse_available(b)) {
		assert(np_monotonic_ms() < until);
		sb_browse_tick(b, np_monotonic_ms());
		rest(1);
	}
	c = browse(b, &cmd);
	assert(sb_browse_success(c));
	sb_browse_cancel(b, c);
	c = sb_browse_begin(b, &cmd, np_monotonic_ms() + 1);
	assert(c);
	rest(3);
	assert(sb_browse_poll(b, c) && !sb_browse_success(c));
	sb_browse_cancel(b, c);
	stop(b);
	puts("Native browse, broker concurrency, cancellation and generations "
	     "passed");
}

static unsigned rss(void)
{
	FILE *f = fopen("/proc/self/status", "r");
	assert(f);
	char line[256];
	unsigned value = 0;
	while (fgets(line, sizeof line, f))
		if (sscanf(line, "VmHWM: %u kB", &value) == 1)
			break;
	fclose(f);
	return value;
}

static void benchmark(sb_music_library *h)
{
	printf("{\"query_ms\":[");
	for (unsigned i = 0; i < 120; i++) {
		sb_ml_request q = {0};
		q.operation = SB_ML_BROWSE;
		q.data.query.media = media;
		q.data.query.limit = 50;
		q.data.query.kind = i % 3 == 0	 ? SB_QUERY_ARTISTS
				    : i % 3 == 1 ? SB_QUERY_ALBUMS
						 : SB_QUERY_TRACKS;
		if (q.data.query.kind == SB_QUERY_TRACKS) {
			q.data.query.album_id = 1;
			q.data.query.sort = SB_SORT_ALBUM_TRACK;
		}
		uint64_t start = np_monotonic_ms();
		exchange(h, &q);
		printf("%s%llu",
		       i ? "," : "",
		       (unsigned long long)(np_monotonic_ms() - start));
	}
	printf("],\"peak_rss_kib\":%u,\"query_rows_max\":50,\"result_bytes\":%"
	       "zu}\n",
	       rss(),
	       sizeof(sb_ml_result));
}

static void
scan_browse(sb_music_library *h, const char *directory, const char *helper)
{
	assert(!strncmp(directory, "/dev/shm/", 9));
	sb_storage_mounts *table = malloc(sizeof *table);
	assert(table);
	assert(sb_storage_read_mounts(NULL, table) == SB_STORAGE_OK);
	sb_storage_mount mount = {0};
	for (unsigned i = 0; i < table->count; i++)
		if (!strcmp(table->rows[i].path, "/dev/shm"))
			mount = table->rows[i];
	free(table);
	assert(mount.mount_id);
	sb_storage_root whole, root;
	assert(sb_storage_validate(
		       "/dev", &mount, &media, NULL, NULL, &whole) ==
	       SB_STORAGE_OK);
	assert(sb_storage_root_child(
		       &whole, &media, directory + 9, NULL, NULL, &root) ==
	       SB_STORAGE_OK);
	sb_storage_root_close(&whole);
	sb_scan_options options = {0};
	options.enabled = options.approved_database = 1;
	options.library = h;
	options.root = root;
	options.work_duty_percent = 100;
	snprintf(options.helper, sizeof options.helper, "%s", helper);
	sb_scan_code code;
	sb_music_scanner *scanner = sb_music_scan_create(&options, &code);
	assert(scanner);
	sb_browse *b = sb_browse_start(h, &media, 1, 1);
	assert(b);
	sb_browse_player(b, "02:00:00:00:00:01");
	assert(sb_browse_media(b, &media, 1));
	assert(sb_music_scan_request(scanner, 1, 1) == SB_SCAN_OK);
	unsigned queries = 0, retryable = 0;
	uint64_t max_ms = 0, start = np_monotonic_ms(),
		 version = sb_browse_version(b);
	sb_scan_status status = {0};
	while (1) {
		sb_lms_command cmd = command("tracks", 0, 50);
		uint64_t before = np_monotonic_ms();
		sb_browse_call *call = browse(b, &cmd);
		if (sb_browse_success(call))
			queries++;
		else {
			assert(strstr(sb_browse_json(call), "stale") ||
			       strstr(sb_browse_json(call), "conflict") ||
			       strstr(sb_browse_json(call), "timeout"));
			retryable++;
		}
		uint64_t elapsed = np_monotonic_ms() - before;
		if (elapsed > max_ms)
			max_ms = elapsed;
		sb_browse_cancel(b, call);
		sb_browse_tick(b, np_monotonic_ms());
		assert(sb_browse_version(b) == version);
		if (sb_music_scan_status(scanner, &status) == SB_SCAN_OK &&
		    status.state >= SB_SCAN_COMPLETED)
			break;
		assert(np_monotonic_ms() - start < 120000);
	}
	if (status.state != SB_SCAN_COMPLETED)
		fprintf(stderr,
			"scan state=%d error=%s\n",
			status.state,
			status.error[0].message);
	assert(status.state == SB_SCAN_COMPLETED && queries);
	assert(sb_browse_media(b, &media, 0));
	sb_browse_tick(b, np_monotonic_ms());
	while (sb_music_scan_stop(scanner) == SB_SCAN_BUSY)
		rest(1);
	while (sb_music_scan_destroy(scanner) == SB_SCAN_BUSY)
		rest(1);
	printf("{\"scanner_queries\":%u,\"retryable\":%u,\"query_max_ms\":%llu,"
	       "\"scan_ms\":%llu,\"indexed\":%llu,\"peak_rss_kib\":%u}\n",
	       queries,
	       retryable,
	       (unsigned long long)max_ms,
	       (unsigned long long)(np_monotonic_ms() - start),
	       (unsigned long long)status.added,
	       rss());
	stop(b);
	sb_storage_root_close(&root);
}

int main(int argc, char **argv)
{
	assert(argc >= 3);
	unsigned n = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 10) : 1000;
	int create = !strcmp(argv[1], "create") || !strcmp(argv[1], "native") ||
		     !strcmp(argv[1], "scan");
	long_names = argc > 4 && !strcmp(argv[4], "long");
	sb_music_library *h = open_db(argv[2], create);
	if (create && strcmp(argv[1], "scan"))
		populate(h, n);
	if (!strcmp(argv[1], "native"))
		native_tests(h);
	if (!strcmp(argv[1], "bench"))
		benchmark(h);
	if (!strcmp(argv[1], "scan")) {
		assert(argc >= 5);
		scan_browse(h, argv[3], argv[4]);
	}
	close_db(h);
	return 0;
}
