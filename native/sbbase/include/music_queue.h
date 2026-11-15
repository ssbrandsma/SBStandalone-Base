#ifndef SB_MUSIC_QUEUE_H
#define SB_MUSIC_QUEUE_H
#include "../../common/music_contracts.h"

/* Single control-thread owner. No I/O, SQL, decoder control or implicit start.
 */
#define SB_QUEUE_LIMIT 256u
#define SB_QUEUE_NONE UINT32_MAX

typedef enum {
	SB_Q_STOPPED,
	SB_Q_BUFFERING,
	SB_Q_PLAYING,
	SB_Q_PAUSED,
	SB_Q_ERROR,
	SB_Q_WAIT_CONNECTION
} sb_queue_mode;

typedef enum { SB_Q_LOAD, SB_Q_ADD, SB_Q_INSERT } sb_queue_edit;

typedef enum {
	SB_Q_OK,
	SB_Q_INVALID,
	SB_Q_BUSY,
	SB_Q_STALE,
	SB_Q_FULL,
	SB_Q_UNAVAILABLE
} sb_queue_result;

typedef struct {
	sb_music_queue_entry identity;
	sb_music_network_track network;
	char title[128], artist[128], album[128];
	char artwork[384],genre[128];
	unsigned track_number;
	uint64_t duration_ms;
	uint64_t
		queue_revision; /* creation/last availability change revision */
	char codec; /* m/f playable only; indexed Ogg remains ineligible */
	int available, eligible;
} sb_queue_item;
typedef struct sb_music_queue sb_music_queue;

typedef struct {
	uint64_t revision, context, owner_generation, stream_epoch;
	uint32_t count, intended, audible;
	uint64_t elapsed_ms;
	sb_queue_mode mode;
} sb_queue_status;

sb_music_queue *sb_queue_create(int enabled);
void sb_queue_destroy(sb_music_queue *);
/* Any physical-owner/connection context change fences pending edits/events.
 * This records admission context, never acquires physical ownership itself. */
void sb_queue_context(sb_music_queue *, uint64_t context, uint64_t owner);
/* Trusted owner manager may bind its newly acknowledged generation without
 * invalidating a staged edit in the same controller context. */
int sb_queue_bind_owner(sb_music_queue *, uint64_t context, uint64_t owner);
sb_queue_status sb_queue_snapshot(const sb_music_queue *);
const sb_queue_item *sb_queue_at(const sb_music_queue *, uint32_t index);
/* One staged operation. Caller fetches bounded DB pages asynchronously, pins
 * library revision/generations and validates every result before staging. */
sb_queue_result sb_queue_begin(sb_music_queue *,
			       sb_queue_edit,
			       uint64_t operation,
			       uint64_t expected_revision,
			       uint64_t context);
sb_queue_result sb_queue_stage(sb_music_queue *,
			       uint64_t operation,
			       const sb_queue_item *,
			       unsigned count);
sb_queue_result sb_queue_commit(sb_music_queue *, uint64_t operation);
void sb_queue_abort(sb_music_queue *, uint64_t operation);
sb_queue_result
sb_queue_clear(sb_music_queue *, uint64_t revision, uint64_t context);
sb_queue_result sb_queue_remove(sb_music_queue *,
				uint32_t index,
				uint64_t revision,
				uint64_t context);
sb_queue_result sb_queue_select(sb_music_queue *,
				uint32_t index,
				uint64_t revision,
				uint64_t context);
sb_queue_result
sb_queue_step(sb_music_queue *, int direction, uint64_t context);
/* Start only after owner/resource/transport validation. Metadata becomes
 * audible only following the matching controller's STMs confirmation. */
sb_queue_result sb_queue_prepared(sb_music_queue *,
				  uint64_t context,
				  uint64_t owner_generation,
				  uint64_t stream_epoch);
sb_queue_result sb_queue_confirm(sb_music_queue *,
				 uint64_t context,
				 uint64_t owner_generation,
				 uint64_t epoch,
				 sb_queue_mode,
				 uint64_t elapsed_ms);
/* Returns 1 once for successful drain, 0 duplicate/stale/not completed.
 * Selects next intent but requires a NEW connection before preparing it. */
int sb_queue_terminal(sb_music_queue *,
		      uint64_t context,
		      uint64_t owner,
		      uint64_t epoch,
		      int drained);
void sb_queue_revoke(sb_music_queue *,
		     const sb_music_source_id *,
		     uint64_t generation);
size_t sb_queue_allocation_bytes(void);
#endif
