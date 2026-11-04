#ifndef SB_MUSIC_LOAD_H
#define SB_MUSIC_LOAD_H
#include "music_queue.h"
#include "../../sbmusic/music_async.h"
/* One pending paginated selection; caller owns queue, media and dispatcher.
 * READY is staged only: caller must authorize physical ownership/resource
 * and then commit, or abort. A READY loader never starts playback. */
typedef struct sb_music_load sb_music_load;

enum { SB_LOAD_IDLE, SB_LOAD_PENDING, SB_LOAD_READY, SB_LOAD_FAILED };

sb_music_load *sb_music_load_create(sb_music_async *, sb_music_queue *);
sb_queue_result sb_music_load_begin(sb_music_load *,
				    const sb_music_media *,
				    sb_queue_edit,
				    uint64_t operation,
				    uint64_t context,
				    uint64_t id,
				    int album,
				    uint64_t now);
void sb_music_load_tick(sb_music_load *, uint64_t now);
int sb_music_load_state(const sb_music_load *);
sb_ml_status sb_music_load_error(const sb_music_load *);
const sb_ml_track *sb_music_load_first(const sb_music_load *); /* READY only */
unsigned
sb_music_load_count(const sb_music_load *); /* staged selection count */
/* Returns BUSY while cancellation is being retired. */
sb_ml_status sb_music_load_cancel(sb_music_load *);
int sb_music_load_destroy(sb_music_load *);
#endif
