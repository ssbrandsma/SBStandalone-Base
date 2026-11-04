#ifndef SB_MUSIC_STORAGE_LIBRARY_H
#define SB_MUSIC_STORAGE_LIBRARY_H
#include "music_storage.h"
#include "music_library.h"
/* Controller-side bridge for an ALREADY opened, independently validated DB.
 * Caller retains/retries desired snapshots on BUSY and receives normal WP1
 * completions. No implicit open, mount-relative path, create or write policy. */
sb_ml_status sb_storage_library_publish(sb_music_library *, const sb_music_media *,
 uint64_t request_id, uint64_t pending_request_id);
int sb_storage_library_result_current(const sb_storage_model *,unsigned slot,
 const sb_music_media *submitted, const sb_ml_result *);
/* WP0 resource revocation is synchronous notification only: no draining or
 * safe-eject acknowledgement in WP6a. Call for EACH formerly online snapshot
 * no longer current, including other slots affected by identity collisions. */
void sb_storage_revoke_resources(const sb_music_resource_provider *,const sb_music_media *previous,
 const sb_music_media *current);
#endif
