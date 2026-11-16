#include "music_queue.h"
#include <stdlib.h>
#include <string.h>

struct sb_music_queue {
	sb_queue_status status;
	sb_queue_item items[SB_QUEUE_LIMIT], pending[SB_QUEUE_LIMIT];
	uint64_t next_entry, last_operation, operation, expected_revision,
		edit_context;
	uint64_t terminal_epoch;
	uint64_t stream_entry;
	unsigned staged;
	sb_queue_edit edit;
	int edit_failed;
};

size_t sb_queue_allocation_bytes(void)
{
	return sizeof(sb_music_queue);
}

sb_music_queue *sb_queue_create(int enabled)
{
	if (!enabled)
		return NULL;
	sb_music_queue *q = calloc(1, sizeof *q);
	if (q) {
		q->status.revision = 1;
		q->status.intended = q->status.audible = SB_QUEUE_NONE;
	}
	return q;
}

void sb_queue_destroy(sb_music_queue *q)
{
	free(q);
}

sb_queue_status sb_queue_snapshot(const sb_music_queue *q)
{
	sb_queue_status empty = {0};
	empty.intended = empty.audible = SB_QUEUE_NONE;
	return q ? q->status : empty;
}

const sb_queue_item *sb_queue_at(const sb_music_queue *q, uint32_t i)
{
	return q && i < q->status.count ? &q->items[i] : NULL;
}

void sb_queue_context(sb_music_queue *q, uint64_t context, uint64_t owner)
{
	if (!q || context <= q->status.context)
		return;
	q->operation = 0;
	q->status.context = context;
	q->status.owner_generation = owner;
	q->status.mode = SB_Q_STOPPED;
	q->status.audible = SB_QUEUE_NONE;
	q->status.elapsed_ms = 0;
}

static sb_queue_result guard(sb_music_queue *q, uint64_t rev, uint64_t context)
{
	if (!q || !context)
		return SB_Q_INVALID;
	if (q->status.context != context || q->status.revision != rev)
		return SB_Q_STALE;
	if (q->operation)
		return SB_Q_BUSY;
	if (rev == UINT64_MAX)
		return SB_Q_FULL;
	return SB_Q_OK;
}

int sb_queue_bind_owner(sb_music_queue *q, uint64_t context, uint64_t owner)
{
	if (!q || !owner || q->status.context != context ||
	    owner < q->status.owner_generation)
		return 0;
	q->status.owner_generation = owner;
	return 1;
}

sb_queue_result sb_queue_begin(sb_music_queue *q,
			       sb_queue_edit edit,
			       uint64_t op,
			       uint64_t rev,
			       uint64_t context)
{
	sb_queue_result s = guard(q, rev, context);
	if (s != SB_Q_OK)
		return s;
	if (!op || op <= q->last_operation || edit < SB_Q_LOAD ||
	    edit > SB_Q_INSERT)
		return SB_Q_INVALID;
	q->last_operation = q->operation = op;
	q->expected_revision = rev;
	q->edit_context = context;
	q->edit = edit;
	q->staged = 0;
	q->edit_failed = 0;
	return SB_Q_OK;
}

static int valid(const sb_queue_item *i)
{
	return sb_music_source_equal(&i->identity.track.source,
				     &i->identity.track.source) &&
	       sb_music_track_valid(&i->identity.track) && i->identity.media_generation &&
	       (i->identity.track.kind==SB_TRACK_LOCAL ||
	        (memchr(i->network.server,0,sizeof i->network.server)&&i->network.server[0]&&
	         memchr(i->network.object,0,sizeof i->network.object)&&i->network.object[0]&&
	         memchr(i->network.uri,0,sizeof i->network.uri)&&i->network.uri[0]&&i->network.generation)) &&
	       i->available && i->eligible &&
	       (i->codec == 'm' || i->codec == 'f') &&
	       memchr(i->title, 0, sizeof i->title) &&
	       memchr(i->artist, 0, sizeof i->artist) &&
	       memchr(i->album, 0, sizeof i->album) &&
	       memchr(i->artwork,0,sizeof i->artwork) && memchr(i->genre,0,sizeof i->genre);
}

sb_queue_result sb_queue_stage(sb_music_queue *q,
			       uint64_t op,
			       const sb_queue_item *items,
			       unsigned count)
{
	if (!q || !op || q->operation != op)
		return SB_Q_STALE;
	if (q->edit_failed)
		return SB_Q_INVALID;
	unsigned existing = q->edit == SB_Q_LOAD ? 0 : q->status.count;
	if (count > SB_MUSIC_PAGE_MAX ||
	    count > SB_QUEUE_LIMIT - existing - q->staged) {
		q->edit_failed = 1;
		return SB_Q_FULL;
	}
	if (count && !items) {
		q->edit_failed = 1;
		return SB_Q_INVALID;
	}
	for (unsigned n = 0; n < count; n++)
		if (!valid(&items[n])) {
			q->edit_failed = 1;
			return SB_Q_UNAVAILABLE;
		}
	if (count)
		memcpy(q->pending + q->staged, items, count * sizeof *items);
	q->staged += count;
	return SB_Q_OK;
}

sb_queue_result sb_queue_commit(sb_music_queue *q, uint64_t op)
{
	if (!q || !op || q->operation != op)
		return SB_Q_STALE;
	if (q->status.context != q->edit_context ||
	    q->status.revision != q->expected_revision)
		return SB_Q_STALE;
	if (q->edit_failed || !q->staged)
		return SB_Q_INVALID;
	if (q->next_entry > UINT64_MAX - q->staged)
		return SB_Q_FULL;
	unsigned index = q->status.count;
	if (q->edit == SB_Q_LOAD)
		index = 0;
	else if (q->edit == SB_Q_INSERT && q->status.intended != SB_QUEUE_NONE)
		index = q->status.intended + 1;
	for (unsigned n = 0; n < q->staged; n++) {
		q->pending[n].identity.entry_id = ++q->next_entry;
		q->pending[n].queue_revision = q->status.revision + 1;
	}
	if (q->edit != SB_Q_LOAD) {
		memmove(q->items + index + q->staged,
			q->items + index,
			(q->status.count - index) * sizeof *q->items);
		if (q->status.audible != SB_QUEUE_NONE &&
		    q->status.audible >= index)
			q->status.audible += q->staged;
	} else {
		q->status.count = 0;
		q->status.intended = q->status.audible = SB_QUEUE_NONE;
		q->status.mode = SB_Q_STOPPED;
		q->status.elapsed_ms = 0;
	}
	memcpy(q->items + index, q->pending, q->staged * sizeof *q->items);
	q->status.count += q->staged;
	if (q->status.intended == SB_QUEUE_NONE)
		q->status.intended = 0;
	q->status.revision++;
	q->operation = 0;
	return SB_Q_OK;
}

void sb_queue_abort(sb_music_queue *q, uint64_t op)
{
	if (q && q->operation == op)
		q->operation = 0;
}

sb_queue_result
sb_queue_clear(sb_music_queue *q, uint64_t rev, uint64_t context)
{
	sb_queue_result s = guard(q, rev, context);
	if (s != SB_Q_OK)
		return s;
	q->status.count = 0;
	q->terminal_epoch = q->status.stream_epoch;
	q->status.intended = q->status.audible = SB_QUEUE_NONE;
	q->status.mode = SB_Q_STOPPED;
	q->status.elapsed_ms = 0;
	q->status.revision++;
	return SB_Q_OK;
}

sb_queue_result sb_queue_remove(sb_music_queue *q,
				uint32_t index,
				uint64_t rev,
				uint64_t context)
{
	sb_queue_result s = guard(q, rev, context);
	if (s != SB_Q_OK)
		return s;
	if (index >= q->status.count)
		return SB_Q_INVALID;
	memmove(q->items + index,
		q->items + index + 1,
		(q->status.count - index - 1) * sizeof *q->items);
	q->status.count--;
	if (q->status.audible == index) {
		q->terminal_epoch = q->status.stream_epoch;
		q->status.audible = SB_QUEUE_NONE;
		q->status.mode = SB_Q_STOPPED;
		q->status.elapsed_ms = 0;
	} else if (q->status.audible != SB_QUEUE_NONE &&
		   q->status.audible > index)
		q->status.audible--;
	if (!q->status.count)
		q->status.intended = SB_QUEUE_NONE;
	else if (q->status.intended > index &&
		 q->status.intended != SB_QUEUE_NONE)
		q->status.intended--;
	else if (q->status.intended >= q->status.count)
		q->status.intended = q->status.count - 1;
	q->status.revision++;
	return SB_Q_OK;
}

sb_queue_result sb_queue_move(sb_music_queue *q,uint32_t from,uint32_t to,uint64_t rev,uint64_t context)
{
 sb_queue_result result=guard(q,rev,context);if(result!=SB_Q_OK)return result;
 if(from>=q->status.count||to>=q->status.count)return SB_Q_INVALID;
 if(from==to)return SB_Q_OK;
 uint64_t audible=q->status.audible<q->status.count?q->items[q->status.audible].identity.entry_id:0;
 uint64_t intended=q->status.intended<q->status.count?q->items[q->status.intended].identity.entry_id:0;
 sb_queue_item moving=q->items[from];
 if(from<to)memmove(q->items+from,q->items+from+1,(to-from)*sizeof moving);
 else memmove(q->items+to+1,q->items+to,(from-to)*sizeof moving);
 q->items[to]=moving;
 for(uint32_t i=0;i<q->status.count;i++){
  if(audible&&q->items[i].identity.entry_id==audible)q->status.audible=i;
  if(intended&&q->items[i].identity.entry_id==intended)q->status.intended=i;
 }
 q->status.revision++;return SB_Q_OK;
}

sb_queue_result sb_queue_select(sb_music_queue *q,
				uint32_t index,
				uint64_t rev,
				uint64_t context)
{
	sb_queue_result s = guard(q, rev, context);
	if (s != SB_Q_OK)
		return s;
	if (index >= q->status.count)
		return SB_Q_INVALID;
	if (!q->items[index].available || !q->items[index].eligible)
		return SB_Q_UNAVAILABLE;
	q->status.intended = index;
	q->status.revision++;
	return SB_Q_OK;
}

sb_queue_result
sb_queue_step(sb_music_queue *q, int direction, uint64_t context)
{
	if (!q || (direction != 1 && direction != -1))
		return SB_Q_INVALID;
	uint32_t i = q->status.intended;
	if (i == SB_QUEUE_NONE || (!i && direction == -1) ||
	    (direction == 1 && i + 1 >= q->status.count))
		return SB_Q_INVALID;
	return sb_queue_select(
		q, direction == 1 ? i + 1 : i - 1, q->status.revision, context);
}

sb_queue_result sb_queue_prepared(sb_music_queue *q,
				  uint64_t context,
				  uint64_t owner,
				  uint64_t epoch)
{
	if (!q || !owner || !epoch || q->status.context != context ||
	    q->status.owner_generation != owner ||
	    epoch <= q->status.stream_epoch)
		return SB_Q_STALE;
	const sb_queue_item *i = sb_queue_at(q, q->status.intended);
	if (!i || !i->available || !i->eligible)
		return SB_Q_UNAVAILABLE;
	q->status.stream_epoch = epoch;
	q->stream_entry = i->identity.entry_id;
	q->status.mode = SB_Q_BUFFERING;
	q->status.audible = SB_QUEUE_NONE;
	q->status.elapsed_ms = 0;
	return SB_Q_OK;
}

static int stream(const sb_music_queue *q,
		  uint64_t context,
		  uint64_t owner,
		  uint64_t epoch)
{
	return q && epoch && context == q->status.context &&
	       owner == q->status.owner_generation &&
	       epoch == q->status.stream_epoch && q->terminal_epoch != epoch;
}

sb_queue_result sb_queue_confirm(sb_music_queue *q,
				 uint64_t context,
				 uint64_t owner,
				 uint64_t epoch,
				 sb_queue_mode mode,
				 uint64_t elapsed)
{
	if (!stream(q, context, owner, epoch))
		return SB_Q_STALE;
	if (mode != SB_Q_PLAYING && mode != SB_Q_PAUSED &&
	    mode != SB_Q_BUFFERING && mode != SB_Q_STOPPED &&
	    mode != SB_Q_ERROR)
		return SB_Q_INVALID;
	if (mode == SB_Q_PAUSED && q->status.audible == SB_QUEUE_NONE)
		return SB_Q_INVALID;
	if (mode == SB_Q_PLAYING) {
		unsigned n;
		for (n = 0; n < q->status.count; n++)
			if (q->items[n].identity.entry_id == q->stream_entry)
				break;
		if (n == q->status.count || !q->items[n].available)
			return SB_Q_STALE;
		q->status.audible = n;
	}
	q->status.mode = mode;
	q->status.elapsed_ms = elapsed;
	if (mode == SB_Q_STOPPED || mode == SB_Q_ERROR)
		q->terminal_epoch = epoch;
	return SB_Q_OK;
}

int sb_queue_terminal(sb_music_queue *q,
		      uint64_t context,
		      uint64_t owner,
		      uint64_t epoch,
		      int drained)
{
	if (!stream(q, context, owner, epoch))
		return 0;
	q->terminal_epoch = epoch;
	q->status.elapsed_ms = 0;
	if (!drained || q->status.audible == SB_QUEUE_NONE) {
		q->status.mode = SB_Q_ERROR;
		return 0;
	}
	q->status.mode = SB_Q_STOPPED;
	if (q->status.revision == UINT64_MAX) {
		q->status.mode = SB_Q_ERROR;
		return 0;
	}
	if (q->status.intended != q->status.audible) {
		q->status.mode = SB_Q_WAIT_CONNECTION;
		return 1;
	}
	if (q->status.intended + 1 >= q->status.count)
		return 1;
	q->status.intended++;
	q->status.revision++;
	q->status.mode = SB_Q_WAIT_CONNECTION;
	return 1;
}

void sb_queue_revoke(sb_music_queue *q,
		     const sb_music_source_id *source,
		     uint64_t generation)
{
	if (!q || !source)
		return;
	int changed = 0;
	for (unsigned n = 0; n < q->status.count; n++) {
		sb_queue_item *i = &q->items[n];
		if (i->identity.media_generation != generation ||
		    !sb_music_source_equal(&i->identity.track.source, source))
			continue;
		if (i->available)
			changed = 1;
		i->available = i->eligible = 0;
		i->queue_revision = q->status.revision == UINT64_MAX
					    ? UINT64_MAX
					    : q->status.revision + 1;
		if (q->status.audible == n || q->status.intended == n) {
			q->status.mode = SB_Q_ERROR;
			q->status.elapsed_ms = 0;
			q->terminal_epoch = q->status.stream_epoch;
		}
	}
	/* Pending pages must never survive a removal, including additions that
	 * are not present in the committed queue yet. Caller may start afresh.
	 */
	q->operation = 0;
	if (changed && q->status.revision != UINT64_MAX)
		q->status.revision++;
}
