#include "music_contracts.h"
#include <string.h>

int sb_music_source_equal(const sb_music_source_id *a, const sb_music_source_id *b)
{
	static const sb_music_source_id zero = {{0}};
	return a && b && memcmp(a, &zero, sizeof *a) && !memcmp(a, b, sizeof *a);
}
int sb_music_media_current(const sb_music_media *a, const sb_music_media *b)
{
	return a && b && a->online && b->online && a->generation &&
		a->generation == b->generation && sb_music_source_equal(&a->source, &b->source);
}
int sb_music_yield_ack(sb_music_ownership *o, uint64_t request, uint64_t generation,
	uint64_t connection, uint64_t now, int accepted)
{
	if (!o || !request || !generation || !connection || o->yield != SB_YIELD_REQUESTED ||
		o->request_id != request || o->generation != generation ||
		o->connection_generation != connection) return 0;
	if (now >= o->deadline_ms) { o->yield = SB_YIELD_TIMEOUT; o->physical_release_confirmed = 0; return 0; }
	o->yield = accepted ? SB_YIELD_ACKNOWLEDGED : SB_YIELD_REFUSED;
	o->physical_release_confirmed = !!accepted;
	return 1;
}
int sb_music_can_commit_owner(const sb_music_ownership *o, uint64_t now)
{
	return o && o->generation && o->request_id && o->connection_generation &&
		o->requested > SB_OWNER_NONE && o->requested <= SB_OWNER_MUSIC &&
		o->yield == SB_YIELD_ACKNOWLEDGED && o->physical_release_confirmed && now < o->deadline_ms;
}
static int stream_equal(const sb_music_stream_id *a, const sb_music_stream_id *b)
{
	return a->epoch && a->owner_generation && a->media_generation && a->connection_generation &&
		a->track.track_id && a->epoch == b->epoch && a->owner_generation == b->owner_generation &&
		a->media_generation == b->media_generation && a->connection_generation == b->connection_generation &&
		a->track.track_id == b->track.track_id && sb_music_source_equal(&a->track.source, &b->track.source);
}
int sb_music_stream_event(sb_music_stream_guard *s, const sb_music_stream_id *id, sb_music_event event)
{
	static const sb_music_stream_state states[] = { SB_STREAM_CONNECTED, SB_STREAM_DECODER_READY,
		SB_STREAM_AUDIBLE, SB_STREAM_INPUT_EOF, SB_STREAM_DRAINED, SB_STREAM_STOPPED, SB_STREAM_FAILED };
	if (!s || !id || (unsigned)event > SB_EVENT_FAILED || s->terminal_seen ||
		!stream_equal(&s->id, id) || (s->seen_events & (1u << event))) return 0;
	s->seen_events |= 1u << event;
	/* Input progress must not replace the audible state. Flags retain both. */
	if (event >= SB_EVENT_DRAINED || !(s->seen_events & (1u << SB_EVENT_AUDIBLE))) s->state = states[event];
	if (event == SB_EVENT_AUDIBLE) s->state = SB_STREAM_AUDIBLE;
	s->terminal_seen = event >= SB_EVENT_DRAINED;
	return 1;
}
int sb_music_queue_update_valid(const sb_music_queue_snapshot *s, const sb_music_queue_update *u)
{
	return s && u && s->revision && s->revision != UINT64_MAX && s->owner_generation &&
		s->media_generation && s->revision == u->expected_revision &&
		s->owner_generation == u->owner_generation && s->media_generation == u->media_generation &&
		u->result_count <= SB_MUSIC_QUEUE_MAX;
}
int sb_music_query_valid(const sb_music_query *q)
{
	return q && (unsigned)q->kind <= SB_QUERY_PLAYLISTS && (unsigned)q->sort <= SB_SORT_RECENT &&
		(unsigned)q->search_field <= SB_SEARCH_ALBUM &&
		q->media.online && q->media.generation && sb_music_source_equal(&q->media.source, &q->media.source) &&
		q->limit && q->limit <= SB_MUSIC_PAGE_MAX && q->offset <= UINT32_MAX - q->limit &&
		memchr(q->search, 0, sizeof q->search);
}
int sb_music_resource_current(const sb_music_resource *r, const sb_music_media *m,
	uint64_t owner, uint64_t epoch, uint64_t now, unsigned method)
{
	unsigned char any = 0;
	if (!r) return 0;
	for (size_t i = 0; i < sizeof r->token; i++) any |= r->token[i];
	return any && !r->revoked && now < r->expires_ms &&
		(method == SB_RESOURCE_GET || method == SB_RESOURCE_HEAD) && (r->methods & method) &&
		sb_music_media_current(&r->media, m) &&
		(r->kind == SB_RESOURCE_ARTWORK || (r->kind == SB_RESOURCE_AUDIO && owner && epoch &&
		 owner == r->owner_generation && epoch == r->stream_epoch));
}
int sb_music_range_valid(uint64_t size, const sb_music_byte_range *r)
{
	return r && r->length && r->first < size && r->length <= size - r->first;
}
