#include "../../native/common/music_contracts.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "nowplaying.h"

int main(void)
{
	sb_music_media media={{{1}},7,1}, other=media;
	assert(!SB_MUSIC_ENABLED);
	assert(sizeof(sb_music_row)*SB_MUSIC_PAGE_MAX+sizeof(sb_music_page)<=SB_MUSIC_QUERY_BYTES_MAX);
	assert(sb_music_media_current(&media,&other));
	other.generation++;assert(!sb_music_media_current(&media,&other));
	other=media;other.source.bytes[0]++;assert(!sb_music_media_current(&media,&other));
	other=media;other.online=0;assert(!sb_music_media_current(&media,&other));
	sb_music_ownership owner={SB_OWNER_RADIO,SB_OWNER_MUSIC,9,12,500,3,SB_YIELD_REQUESTED,0};
	np_manager *metadata=np_create(120,32);assert(metadata);
	char reply[512];int changed=0;
	const char *claim="{\"source\":\"music\",\"name\":\"Standalone Music\"}";
	assert(np_api(metadata,"POST","/api/nowplaying/claim",claim,strlen(claim),reply,sizeof reply,&changed)==201);
	assert(np_current(metadata)->active && changed);
	assert(owner.current==SB_OWNER_RADIO && !owner.physical_release_confirmed);
	assert(!sb_music_can_commit_owner(&owner,100)); /* metadata claim is not yield */
	np_destroy(metadata);
	assert(!sb_music_yield_ack(&owner,12,8,3,100,1));
	assert(!sb_music_yield_ack(&owner,12,9,4,100,1)); /* reconnect invalidates callback */
	assert(sb_music_yield_ack(&owner,12,9,3,100,0));assert(!sb_music_can_commit_owner(&owner,100));
	owner.yield=SB_YIELD_REQUESTED;assert(sb_music_yield_ack(&owner,12,9,3,100,1));
	assert(sb_music_can_commit_owner(&owner,100));assert(!sb_music_yield_ack(&owner,12,9,3,100,1));
	assert(!sb_music_can_commit_owner(&owner,500));
	owner.yield=SB_YIELD_REQUESTED;assert(!sb_music_yield_ack(&owner,12,9,3,500,1));assert(owner.yield==SB_YIELD_TIMEOUT);
	owner.yield=SB_YIELD_REVOKED;assert(!sb_music_can_commit_owner(&owner,100));
	sb_music_stream_guard stream={0};stream.id=(sb_music_stream_id){1,9,7,3,{{{1}},4}};
	sb_music_stream_id old=stream.id;old.epoch++;
	assert(!sb_music_stream_event(&stream,&old,SB_EVENT_AUDIBLE));
	assert(sb_music_stream_event(&stream,&stream.id,SB_EVENT_CONNECTED));
	assert(sb_music_stream_event(&stream,&stream.id,SB_EVENT_DECODER_READY));assert(!stream.terminal_seen);
	assert(sb_music_stream_event(&stream,&stream.id,SB_EVENT_AUDIBLE));
	assert(sb_music_stream_event(&stream,&stream.id,SB_EVENT_INPUT_EOF));
	assert(stream.state==SB_STREAM_AUDIBLE && !stream.terminal_seen);
	assert(!sb_music_stream_event(&stream,&stream.id,SB_EVENT_INPUT_EOF));
	assert(sb_music_stream_event(&stream,&stream.id,SB_EVENT_DRAINED));
	assert(!sb_music_stream_event(&stream,&stream.id,SB_EVENT_STOPPED));
	sb_music_queue_snapshot queue={0};queue.revision=4;queue.owner_generation=9;queue.media_generation=7;
	sb_music_queue_update update={4,9,7,SB_MUSIC_QUEUE_MAX};assert(sb_music_queue_update_valid(&queue,&update));
	update.result_count++;assert(!sb_music_queue_update_valid(&queue,&update));update.result_count=0;
	update.expected_revision--;assert(!sb_music_queue_update_valid(&queue,&update));
	sb_music_query query={0};query.media=media;query.limit=50;
	assert(sb_music_query_valid(&query));query.limit=51;assert(!sb_music_query_valid(&query));
	query.limit=50;query.offset=UINT32_MAX;assert(!sb_music_query_valid(&query));query.offset=0;
	memset(query.search,'x',sizeof query.search);assert(!sb_music_query_valid(&query));
	sb_music_resource resource={0};resource.token[0]=1;resource.media=media;resource.expires_ms=500;
	resource.owner_generation=9;resource.stream_epoch=1;resource.methods=SB_RESOURCE_GET|SB_RESOURCE_HEAD;
	assert(sb_music_resource_current(&resource,&media,9,1,100,SB_RESOURCE_GET));
	assert(!sb_music_resource_current(&resource,&media,9,1,500,SB_RESOURCE_GET));
	assert(!sb_music_resource_current(&resource,&media,8,1,100,SB_RESOURCE_GET));
	assert(!sb_music_resource_current(&resource,&media,9,1,100,4));
	other=media;other.generation++;assert(!sb_music_resource_current(&resource,&other,9,1,100,SB_RESOURCE_GET));
	resource.revoked=1;assert(!sb_music_resource_current(&resource,&media,9,1,100,SB_RESOURCE_GET));
	sb_music_byte_range range={UINT64_MAX-2,2};assert(sb_music_range_valid(UINT64_MAX,&range));
	range.length=4;assert(!sb_music_range_valid(UINT64_MAX,&range));range.length=0;assert(!sb_music_range_valid(1,&range));
	puts("music contract guards: passed; no runtime controller activated");
	return 0;
}
