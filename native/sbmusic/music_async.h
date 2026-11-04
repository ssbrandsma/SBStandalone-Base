#ifndef SB_MUSIC_ASYNC_H
#define SB_MUSIC_ASYNC_H
#include "music_library.h"
#define SB_ASYNC_SLOTS 8
typedef struct sb_music_async sb_music_async;
/* Explicit already-open broker-mode library. Disabled allocates nothing. */
sb_music_async *sb_music_async_start(sb_music_library *, int enabled);
int sb_music_async_fd(const sb_music_async *);
sb_ml_status sb_music_async_submit(sb_music_async *,
				   const sb_ml_request *,
				   uint64_t deadline_ms,
				   uint64_t *id);
sb_ml_status sb_music_async_poll(sb_music_async *, uint64_t id, sb_ml_result *);
sb_ml_status sb_music_async_cancel(sb_music_async *, uint64_t id);
sb_ml_status sb_music_async_stop(sb_music_async *);
sb_ml_status sb_music_async_destroy(sb_music_async *);
unsigned sb_music_async_peak(sb_music_async *);
#endif
