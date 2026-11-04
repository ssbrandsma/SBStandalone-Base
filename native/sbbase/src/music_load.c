#include "music_load.h"
#include "../../common/metadata_text.h"
#include <stdlib.h>
#include <string.h>

struct sb_music_load {
	sb_music_async *async;
	sb_music_queue *queue;
	sb_music_media media;
	uint64_t operation, context, id, deadline, job, revision;
	uint64_t page[SB_MUSIC_PAGE_MAX];
	unsigned offset, page_count, page_index, total, staged;
	int album, detail, state, cancelled;
	sb_ml_status error;
	sb_ml_track first;
};

sb_music_load *sb_music_load_create(sb_music_async *a, sb_music_queue *q)
{
	if (!a || !q)
		return NULL;
	sb_music_load *l = calloc(1, sizeof *l);
	if (l) {
		l->async = a;
		l->queue = q;
	}
	return l;
}

sb_queue_result sb_music_load_begin(sb_music_load *l,
				    const sb_music_media *media,
				    sb_queue_edit edit,
				    uint64_t op,
				    uint64_t context,
				    uint64_t id,
				    int album,
				    uint64_t now)
{
	if (!l || !media || !media->online || !id || id > INT64_MAX ||
	    now > UINT64_MAX - 5000)
		return SB_Q_INVALID;
	if (l->job || l->state == SB_LOAD_PENDING || l->state == SB_LOAD_READY)
		return SB_Q_BUSY;
	sb_queue_status s = sb_queue_snapshot(l->queue);
	sb_queue_result result =
		sb_queue_begin(l->queue, edit, op, s.revision, context);
	if (result != SB_Q_OK)
		return result;
	l->media = *media;
	l->operation = op;
	l->context = context;
	l->id = id;
	l->deadline = now + 5000;
	l->revision = 0;
	l->offset = l->page_count = l->page_index = l->total = l->staged = 0;
	l->album = !!album;
	l->detail = !album;
	l->state = SB_LOAD_PENDING;
	l->cancelled = 0;
	l->error = SB_ML_OK;
	return SB_Q_OK;
}

static void failure(sb_music_load *l, sb_ml_status error)
{
	l->error = error;
	l->state = SB_LOAD_FAILED;
	sb_queue_abort(l->queue, l->operation);
}

void sb_music_load_tick(sb_music_load *l, uint64_t now)
{
	if (!l)
		return;
	if (l->cancelled) {
		(void)sb_music_load_cancel(l);
		return;
	}
	if (l->state != SB_LOAD_PENDING)
		return;
	if (now >= l->deadline ||
	    sb_queue_snapshot(l->queue).context != l->context) {
		failure(l, SB_ML_CANCELLED);
		l->cancelled = 1;
		(void)sb_music_load_cancel(l);
		return;
	}
	if (l->job) {
		sb_ml_result result;
		sb_ml_status received =
			sb_music_async_poll(l->async, l->job, &result);
		if (received == SB_ML_BUSY)
			return;
		l->job = 0;
		if (received != SB_ML_OK || result.status != SB_ML_OK) {
			failure(l,
				received != SB_ML_OK ? received
						     : result.status);
			return;
		}
		if (l->revision && result.revision != l->revision) {
			failure(l, SB_ML_STALE);
			return;
		}
		l->revision = result.revision;
		if (!l->detail) {
			sb_music_page *p = &result.data.query.page;
			if (p->total > SB_QUEUE_LIMIT || !p->total ||
			    p->media_generation != l->media.generation ||
			    !p->media_online || p->count > SB_MUSIC_PAGE_MAX ||
			    p->offset != l->offset) {
				failure(l,
					p->total > SB_QUEUE_LIMIT
						? SB_ML_LIMIT
						: SB_ML_NOT_FOUND);
				return;
			}
			l->total = p->total;
			l->page_count = p->count;
			l->page_index = 0;
			if (!p->count) {
				failure(l, SB_ML_STALE);
				return;
			}
			for (unsigned n = 0; n < p->count; n++)
				l->page[n] = result.data.query.rows[n].id;
			l->detail = 1;
		} else {
			sb_ml_track *track = &result.data.track;
			if (track->media_generation != l->media.generation ||
			    !sb_music_source_equal(&track->identity.source,
						   &l->media.source) ||
			    track->identity.track_id !=
				    (l->album ? l->page[l->page_index]
					      : l->id)) {
				failure(l, SB_ML_STALE);
				return;
			}
			sb_queue_item item = {0};
			item.identity.track = track->identity;
			item.identity.media_generation =
				track->media_generation;
			item.duration_ms = track->duration_ms;
			item.available = 1;
			item.codec = !strcmp(track->codec, "mp3")    ? 'm'
				     : !strcmp(track->codec, "flac") ? 'f'
								     : 0;
			item.eligible = item.codec != 0;
			sb_metadata_text(item.title,
					 sizeof item.title,
					 3,
					 (const unsigned char *)track->title,
					 strlen(track->title));
			sb_metadata_text(
				item.artist,
				sizeof item.artist,
				3,
				(const unsigned char *)track->artists[0],
				strlen(track->artists[0]));
			sb_metadata_text(item.album,
					 sizeof item.album,
					 3,
					 (const unsigned char *)track->album,
					 strlen(track->album));
			sb_queue_result staged = sb_queue_stage(
				l->queue, l->operation, &item, 1);
			if (staged != SB_Q_OK) {
				failure(l,
					staged == SB_Q_FULL
						? SB_ML_LIMIT
						: SB_ML_UNSUPPORTED);
				return;
			}
			l->staged++;
			if (l->staged == 1)
				l->first = *track;
			if (!l->album || l->staged == l->total) {
				l->state = SB_LOAD_READY;
				return;
			}
			if (++l->page_index == l->page_count) {
				l->offset += l->page_count;
				l->detail = 0;
			}
		}
	}
	sb_ml_request request = {0};
	if (l->detail) {
		request.operation = SB_ML_DETAIL;
		request.data.detail.identity.source = l->media.source;
		request.data.detail.identity.track_id =
			l->album ? l->page[l->page_index] : l->id;
		request.data.detail.generation = l->media.generation;
	} else {
		request.operation = SB_ML_BROWSE;
		request.data.query.media = l->media;
		request.data.query.library_revision = l->revision;
		request.data.query.kind = SB_QUERY_TRACKS;
		request.data.query.sort = SB_SORT_ALBUM_TRACK;
		request.data.query.album_id = l->id;
		request.data.query.limit = SB_MUSIC_PAGE_MAX;
		request.data.query.offset = l->offset;
	}
	sb_ml_status sent =
		sb_music_async_submit(l->async, &request, l->deadline, &l->job);
	if (sent != SB_ML_BUSY && sent != SB_ML_OK)
		failure(l, sent);
}

int sb_music_load_state(const sb_music_load *l)
{
	return l ? l->state : SB_LOAD_FAILED;
}

sb_ml_status sb_music_load_error(const sb_music_load *l)
{
	return l ? l->error : SB_ML_INVALID;
}

const sb_ml_track *sb_music_load_first(const sb_music_load *l)
{
	return l && l->state == SB_LOAD_READY ? &l->first : NULL;
}

unsigned sb_music_load_count(const sb_music_load *l)
{
	return l && l->state == SB_LOAD_READY ? l->staged : 0;
}

sb_ml_status sb_music_load_cancel(sb_music_load *l)
{
	if (!l)
		return SB_ML_INVALID;
	int failed = l->state == SB_LOAD_FAILED;
	l->cancelled = 1;
	sb_queue_abort(l->queue, l->operation);
	if (l->job) {
		sb_ml_status s = sb_music_async_cancel(l->async, l->job);
		if (s == SB_ML_BUSY)
			return s;
		l->job = 0;
	}
	l->state = failed ? SB_LOAD_FAILED : SB_LOAD_IDLE;
	l->cancelled = 0;
	return SB_ML_OK;
}

int sb_music_load_destroy(sb_music_load *l)
{
	if (!l)
		return 1;
	if (sb_music_load_cancel(l) == SB_ML_BUSY)
		return 0;
	free(l);
	return 1;
}
