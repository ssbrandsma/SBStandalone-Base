#ifndef SB_MUSIC_PLAYBACK_H
#define SB_MUSIC_PLAYBACK_H
#include "music_file.h"
#include "music_load.h"
#include "slim_controller.h"
#include "nowplaying.h"
typedef struct sb_music_playback sb_music_playback;
typedef int (*sb_music_send)(void *,
			     const unsigned char *,
			     size_t,
			     const sb_music_stream_id *);

typedef enum {
	SB_PLAY_IDLE,
	SB_PLAY_LOADING,
	SB_PLAY_FILE,
	SB_PLAY_RESERVING,
	SB_PLAY_OWNER,
	SB_PLAY_ACTIVATING,
	SB_PLAY_ACTIVE,
	SB_PLAY_NEED_CONNECTION,
	SB_PLAY_FAILED
} sb_play_phase;

sb_music_playback *sb_playback_create(int enabled,
				      sb_music_async *,
				      const sb_storage_root *,
				      sb_physical_owner *,
				      np_manager *,
				      int resource_ipc,
				      unsigned http_port,
				      sb_music_send,
				      void *);
sb_music_queue *sb_playback_queue(sb_music_playback *);
sb_play_phase sb_playback_phase(const sb_music_playback *);
const char *sb_playback_error(const sb_music_playback *);
int sb_playback_poll_fd(const sb_music_playback *, unsigned which);
short sb_playback_ipc_events(const sb_music_playback *);
uint64_t sb_playback_operation(const sb_music_playback *);
unsigned sb_playback_selection_count(const sb_music_playback *);
/* 0 pending, 1 succeeded, -1 failed/stale. */
int sb_playback_result(const sb_music_playback *,
		       uint64_t operation,
		       uint64_t context);
void sb_playback_cancel(sb_music_playback *,
			uint64_t operation,
			uint64_t context);
/* One action at a time. context must match sb_queue_snapshot.context.
 * Load/add/insert stages asynchronously; no command can acknowledge release. */
sb_queue_result sb_playback_load(sb_music_playback *,
				 sb_queue_edit,
				 uint64_t id,
				 int album,
				 uint64_t context,
				 uint64_t now);
int sb_playback_attach(sb_music_playback *,
		       uint64_t connection,
		       const sb_slim_hello *);
int sb_playback_idle_proof(sb_music_playback *,
			   uint64_t adapter,
			   uint64_t connection,
			   uint64_t owner_generation,
			   uint64_t now,
			   int producer_stopped,
			   int hooks_released,
			   int decoder_quiescent);
/* Trusted bridge calls after real Lua producer/hook/decoder proof. Still
 * refuses until private RESET/ACTIVATE/resource work is settled. */
int sb_playback_bridge_begin(void *,uint64_t now);
int sb_playback_bridge_ready(void *);
/* Startup RESET/ACK roundtrip; no decoder or resource activation. */
int sb_playback_probe_resources(sb_music_playback *, uint64_t now);
unsigned sb_playback_codecs(const sb_music_playback *);
int sb_playback_single_mp3(sb_music_playback *);
int sb_playback_bridge_proof(void *,uint64_t adapter,uint64_t connection,uint64_t generation,uint64_t now);
void sb_playback_detach(sb_music_playback *, uint64_t connection);
void sb_playback_tick(sb_music_playback *, uint64_t now);
int sb_playback_event(sb_music_playback *,
		      uint64_t connection,
		      const sb_music_stream_id *,
		      const sb_slim_event *,
		      uint64_t now);
const sb_music_stream_id *sb_playback_stream(const sb_music_playback *);
/* These never route external-app controls; caller falls back to old dispatch
 * unless this context has physical Music ownership. */
int sb_playback_control(sb_music_playback *,
			char command,
			uint64_t context,
			uint64_t now);
int sb_playback_select(sb_music_playback *,
		       uint32_t index,
		       uint64_t context,
		       uint64_t now);
void sb_playback_media_removed(sb_music_playback *, const sb_music_media *);
int sb_playback_status(sb_music_playback *, char *, size_t);
/* Parse a music-owned status page request. Bare ["status"] keeps offset 0
 * quantity 1; ["status", x] stays invalid; Jive's "-" offset means "from
 * the current song" (audible, else intended, else 0). Count/strings are
 * passed separately because sb_lms_command stores a fixed 2D argument
 * array (no char** decay). Returns nonzero with *offset/*quantity set. */
int sb_playback_status_args(unsigned,
			    const char *,
			    const char *,
			    uint32_t,
			    uint32_t,
			    uint32_t *,
			    uint32_t *);
int sb_playback_status_page(
	sb_music_playback *,
	uint32_t offset,
	uint32_t quantity,
	char *,
	size_t); /* at most200 wire rows, bounded caller buffer */
int sb_playback_destroy(sb_music_playback *); /* cooperative, retry BUSY */
#endif
