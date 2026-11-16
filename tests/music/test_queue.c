#include "music_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static sb_queue_item item(unsigned id)
{
	sb_queue_item i = {0};
	i.identity.track.source.bytes[0] = 1;
	i.identity.track.track_id = id;
	i.identity.media_generation = 3;
	i.codec = id % 2 ? 'm' : 'f';
	i.available = i.eligible = 1;
	snprintf(i.title, sizeof i.title, "Track %u", id);
	return i;
}

static void edit(sb_music_queue *q, sb_queue_edit kind, uint64_t op, unsigned n)
{
	sb_queue_status s = sb_queue_snapshot(q);
	assert(sb_queue_begin(q, kind, op, s.revision, s.context) == SB_Q_OK);
	for (unsigned j = 0; j < n; j++) {
		sb_queue_item i = item(j + 1);
		assert(sb_queue_stage(q, op, &i, 1) == SB_Q_OK);
	}
	assert(sb_queue_commit(q, op) == SB_Q_OK);
}

int main(void)
{
	assert(!sb_queue_create(0));
	sb_music_queue *q = sb_queue_create(1);
	assert(q);
	sb_queue_context(q, 1, 8);
	assert(sb_queue_step(q, 1, 1) == SB_Q_INVALID);
	edit(q, SB_Q_LOAD, 1, 3);
	sb_queue_status s = sb_queue_snapshot(q);
	assert(s.count == 3 && s.intended == 0 && s.audible == SB_QUEUE_NONE);
	uint64_t first = sb_queue_at(q, 0)->identity.entry_id;
	assert(sb_queue_begin(q, SB_Q_LOAD, 2, s.revision, 1) == SB_Q_OK);
	sb_queue_item bad = item(99);
	bad.available = 0;
	assert(sb_queue_stage(q, 2, &bad, 1) == SB_Q_UNAVAILABLE);
	assert(sb_queue_commit(q, 2) == SB_Q_INVALID);
	assert(sb_queue_at(q, 0)->identity.entry_id == first);
	sb_queue_abort(q, 2);
	assert(sb_queue_prepared(q, 1, 8, 1) == SB_Q_OK);
	assert(sb_queue_snapshot(q).audible == SB_QUEUE_NONE);
	/* Selecting a future item cannot change the identity confirmed by STMs.
	 */
	assert(sb_queue_select(q, 1, s.revision, 1) == SB_Q_OK);
	assert(sb_queue_confirm(q, 1, 8, 1, SB_Q_PLAYING, 10) == SB_Q_OK);
	assert(sb_queue_snapshot(q).audible == 0);
	assert(sb_queue_confirm(q, 1, 8, 1, SB_Q_PAUSED, 15) == SB_Q_OK);
	assert(sb_queue_snapshot(q).elapsed_ms == 15);
	assert(sb_queue_terminal(q, 1, 8, 1, 1));
	assert(!sb_queue_terminal(q, 1, 8, 1, 1));
	assert(sb_queue_snapshot(q).intended == 1);
	assert(sb_queue_snapshot(q).mode == SB_Q_WAIT_CONNECTION);
	assert(sb_queue_prepared(q, 1, 8, 1) == SB_Q_STALE);
	sb_queue_context(q, 2, 9);
	assert(sb_queue_prepared(q, 2, 9, 2) == SB_Q_OK);
	assert(sb_queue_confirm(q, 1, 8, 1, SB_Q_PLAYING, 99) == SB_Q_STALE);
	assert(sb_queue_confirm(q, 2, 9, 2, SB_Q_PLAYING, 0) == SB_Q_OK);
	assert(sb_queue_terminal(q, 2, 9, 2, 1));
	assert(sb_queue_snapshot(q).intended == 2);
	edit(q, SB_Q_INSERT, 3, 2);
	assert(sb_queue_snapshot(q).count == 5);
	assert(sb_queue_at(q, 3)->identity.track.track_id == 1);
	edit(q, SB_Q_ADD, 4, 1);
	assert(sb_queue_snapshot(q).count == 6);
	s = sb_queue_snapshot(q);
	assert(sb_queue_remove(q, 0, s.revision, 1) == SB_Q_STALE);
	assert(sb_queue_remove(q, 0, s.revision, 2) == SB_Q_OK);
	assert(sb_queue_step(q, -1, 2) == SB_Q_OK);
	/* Removal cancels pending additions and keeps unavailable identities.
	 */
	s = sb_queue_snapshot(q);
	assert(sb_queue_begin(q, SB_Q_ADD, 5, s.revision, 2) == SB_Q_OK);
	sb_queue_revoke(q, &bad.identity.track.source, 3);
	assert(sb_queue_commit(q, 5) == SB_Q_STALE);
	assert(sb_queue_snapshot(q).count == 5);
	assert(!sb_queue_at(q, 0)->available);
	assert(sb_queue_select(q, 0, sb_queue_snapshot(q).revision, 2) ==
	       SB_Q_UNAVAILABLE);
	edit(q, SB_Q_LOAD, 6, SB_QUEUE_LIMIT);
	s = sb_queue_snapshot(q);
	assert(sb_queue_begin(q, SB_Q_ADD, 7, s.revision, 2) == SB_Q_OK);
	sb_queue_item one = item(1);
	assert(sb_queue_stage(q, 7, &one, 1) == SB_Q_FULL);
	assert(sb_queue_commit(q, 7) == SB_Q_INVALID);
	sb_queue_abort(q, 7);
	assert(sb_queue_snapshot(q).count == SB_QUEUE_LIMIT);
	assert(sb_queue_clear(q, s.revision, 2) == SB_Q_OK);
	assert(!sb_queue_snapshot(q).count);
	assert(sb_queue_begin(
		       q, SB_Q_LOAD, 8, sb_queue_snapshot(q).revision, 2) ==
	       SB_Q_OK);
	sb_queue_context(q, 3, 10);
	assert(sb_queue_commit(q, 8) == SB_Q_STALE);
	/* Local IDs and opaque remote objects coexist; moving the audible entry
	 * changes only its presentation index, never its stream identity. */
	sb_queue_item mixed[2]={item(46),item(81)};
	mixed[1].identity.track.kind=SB_TRACK_NETWORK;mixed[1].identity.track.track_id=0;mixed[1].identity.track.network_id=1;
	mixed[1].identity.track.source.bytes[0]=2;mixed[1].identity.media_generation=1;
	strcpy(mixed[1].network.server,"uuid:queue-fixture");strcpy(mixed[1].network.object,"opaque:&81");strcpy(mixed[1].network.uri,"http://192.168.1.2:8080/music.mp3");mixed[1].network.generation=1;
	s=sb_queue_snapshot(q);assert(sb_queue_begin(q,SB_Q_LOAD,9,s.revision,3)==SB_Q_OK);
	assert(sb_queue_stage(q,9,mixed,2)==SB_Q_OK&&sb_queue_commit(q,9)==SB_Q_OK);
	assert(sb_queue_prepared(q,3,10,3)==SB_Q_OK&&sb_queue_confirm(q,3,10,3,SB_Q_PLAYING,123)==SB_Q_OK);
	s=sb_queue_snapshot(q);uint64_t audible=sb_queue_at(q,0)->identity.entry_id;
	assert(sb_queue_move(q,0,1,s.revision,3)==SB_Q_OK);
	assert(sb_queue_snapshot(q).audible==1&&sb_queue_snapshot(q).intended==1&&sb_queue_snapshot(q).elapsed_ms==123);
	assert(sb_queue_at(q,1)->identity.entry_id==audible&&sb_queue_at(q,0)->identity.track.kind==SB_TRACK_NETWORK);
	assert(sb_queue_move(q,1,0,s.revision,3)==SB_Q_STALE);
	assert(sb_queue_confirm(q,3,10,3,SB_Q_PLAYING,456)==SB_Q_OK&&sb_queue_snapshot(q).audible==1);
	printf("Queue atomicity, identities, intent/audible, epochs, bounds "
	       "and revocation passed; bytes=%zu\n",
	       sb_queue_allocation_bytes());
	sb_queue_destroy(q);
	return 0;
}
