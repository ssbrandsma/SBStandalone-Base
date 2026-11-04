#define _POSIX_C_SOURCE 200809L
#include "music_load.h"
#include "nowplaying.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void rest(void)
{
	struct timespec t = {0, 1000000};
	nanosleep(&t, NULL);
}

static sb_ml_result exchange(sb_music_library *db, sb_ml_request *q)
{
	sb_ml_result r;
	assert(sb_music_library_exchange(db, q, &r) == SB_ML_OK);
	return r;
}

static void finish(sb_music_load *l)
{
	uint64_t until = np_monotonic_ms() + 6000;
	while (sb_music_load_state(l) == SB_LOAD_PENDING) {
		sb_music_load_tick(l, np_monotonic_ms());
		rest();
		assert(np_monotonic_ms() < until);
	}
}

int main(void)
{
	char path[] = "/tmp/sb-music-load-XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	close(fd);
	sb_ml_options options = {0};
	options.create = 1;
	strcpy(options.path, path);
	sb_music_library *db = sb_music_library_start(&options);
	assert(db);
	sb_ml_result r;
	while (sb_music_library_receive(db, &r) == SB_ML_BUSY)
		rest();
	assert(r.status == SB_ML_OK);
	sb_music_media media = {{{3}}, 1, 1};
	sb_ml_request request = {0};
	request.operation = SB_ML_SOURCE;
	request.data.media = media;
	exchange(db, &request);
	request.operation = SB_ML_BEGIN;
	request.transaction = 1;
	exchange(db, &request);
	uint64_t track = 0;
	for (unsigned n = 0; n < 400; n++) {
		request.operation = SB_ML_UPSERT;
		sb_ml_track *t = &request.data.track;
		memset(t, 0, sizeof *t);
		t->identity.source = media.source;
		t->media_generation = 1;
		snprintf(t->path, sizeof t->path, "track%u.flac", n);
		snprintf(t->title, sizeof t->title, "Track %u", n);
		strcpy(t->album, n < 140 ? "Three discs" : "Too many");
		strcpy(t->album_key, t->album);
		strcpy(t->album_artist, "Artist");
		strcpy(t->artists[0], "Artist");
		t->artist_count = 1;
		strcpy(t->codec, n % 2 ? "mp3" : "flac");
		t->file_size = 1;
		t->mtime = 1;
		t->disc = n / 50 + 1;
		t->track = n % 50 + 1;
		r = exchange(db, &request);
		if (!n)
			track = r.record_id;
	}
	request.operation = SB_ML_COMMIT;
	exchange(db, &request);
	sb_music_async *async = sb_music_async_start(db, 1);
	assert(async);
	sb_music_queue *q = sb_queue_create(1);
	assert(q);
	sb_queue_context(q, 1, 4);
	sb_music_load *load = sb_music_load_create(async, q);
	assert(load);
	assert(sb_music_load_begin(load,
				   &media,
				   SB_Q_LOAD,
				   1,
				   1,
				   track,
				   0,
				   np_monotonic_ms()) == SB_Q_OK);
	finish(load);
	assert(sb_music_load_state(load) == SB_LOAD_READY);
	assert(!sb_queue_snapshot(q).count); /* staging never publishes early */
	assert(sb_queue_commit(q, 1) == SB_Q_OK);
	assert(sb_queue_snapshot(q).count == 1);
	assert(sb_music_load_cancel(load) == SB_ML_OK);
	assert(sb_music_load_begin(load,
				   &media,
				   SB_Q_LOAD,
				   2,
				   1,
				   2,
				   1,
				   np_monotonic_ms()) == SB_Q_OK);
	finish(load);
	assert(sb_music_load_state(load) == SB_LOAD_FAILED &&
	       sb_music_load_error(load) == SB_ML_LIMIT);
	assert(sb_queue_snapshot(q).count == 1 &&
	       sb_queue_at(q, 0)->identity.track.track_id == track);
	assert(sb_music_load_cancel(load) == SB_ML_OK);
	uint64_t began = np_monotonic_ms();
	assert(sb_music_load_begin(
		       load, &media, SB_Q_LOAD, 3, 1, 1, 1, began) == SB_Q_OK);
	finish(load);
	assert(sb_music_load_state(load) == SB_LOAD_READY);
	assert(sb_queue_snapshot(q).count == 1);
	assert(sb_queue_commit(q, 3) == SB_Q_OK &&
	       sb_queue_snapshot(q).count == 140);
	for (unsigned n = 0; n < 140; n++)
		assert(sb_queue_at(q, n)->identity.track.track_id == n + 1);
	printf("Paginated 140-track/three-disc load: %llu ms; oversized album "
	       "rollback passed\n",
	       (unsigned long long)(np_monotonic_ms() - began));
	assert(sb_music_load_cancel(load) == SB_ML_OK);
	assert(sb_music_load_begin(load,
				   &media,
				   SB_Q_LOAD,
				   4,
				   1,
				   1,
				   1,
				   np_monotonic_ms()) == SB_Q_OK);
	sb_music_load_tick(load, np_monotonic_ms());
	sb_queue_context(q, 2, 5);
	finish(load);
	assert(sb_music_load_state(load) == SB_LOAD_FAILED &&
	       sb_queue_snapshot(q).count == 140);
	while (!sb_music_load_destroy(load))
		rest();
	while (sb_music_async_stop(async) == SB_ML_BUSY)
		rest();
	while (sb_music_async_destroy(async) == SB_ML_BUSY)
		rest();
	request = (sb_ml_request){0};
	request.operation = SB_ML_CLOSE;
	exchange(db, &request);
	assert(sb_music_library_destroy(db) == SB_ML_OK);
	sb_queue_destroy(q);
	assert(!unlink(path));
	puts("Async load identity, atomic commit, page bounds, cancellation "
	     "and context guards passed");
	return 0;
}
