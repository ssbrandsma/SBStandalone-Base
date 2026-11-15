#ifndef SB_MUSIC_FILE_H
#define SB_MUSIC_FILE_H
#include "../../sbmusic/music_storage.h"
#include "../../sbmusic/music_library.h"
#include "../../common/music_resource_ipc.h"
typedef struct sb_music_file sb_music_file;
sb_ml_status sb_music_file_submit_network(sb_music_file *,uint64_t,const sb_ml_track *,const sb_music_network_track *,uint64_t,uint64_t,uint64_t);
/* Explicit validated root, duplicated at construction. One bounded open job.
 * No root discovery and no database opens; disabled creates nothing. */
sb_music_file *sb_music_file_start(int enabled, const sb_storage_root *);
int sb_music_file_fd(const sb_music_file *);
sb_ml_status sb_music_file_submit(sb_music_file *,
				  uint64_t request,
				  const sb_ml_track *,
				  uint64_t owner,
				  uint64_t epoch,
				  uint64_t expires_ms);
/* Successful completion transfers the owned result.fd to caller. */
sb_ml_status
sb_music_file_result(sb_music_file *, uint64_t request, sb_resource_message *);
sb_ml_status sb_music_file_cancel(sb_music_file *, uint64_t request);
sb_ml_status sb_music_file_stop(sb_music_file *);
sb_ml_status sb_music_file_destroy(sb_music_file *);
#endif
