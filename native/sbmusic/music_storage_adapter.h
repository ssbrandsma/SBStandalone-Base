#ifndef SB_MUSIC_STORAGE_ADAPTER_H
#define SB_MUSIC_STORAGE_ADAPTER_H
#include "music_storage.h"
typedef struct sb_storage_adapter sb_storage_adapter;
/* Explicit opt-in only. NULL for disabled/Baby: no allocation, FDs or thread. */
sb_storage_adapter *sb_storage_adapter_start(const sb_storage_worker_options *,uint64_t random_incarnation);
/* Single controller owner, nonblocking. Hints are NOT mount/identity authority.
 * Removal revokes matching snapshots before returning, even while worker busy.
 * sequence scoped to this ingress connection; stale hints trigger reconcile. */
sb_storage_status sb_storage_adapter_hint(sb_storage_adapter *,uint64_t sequence,const char *action,const char *device_basename);
/* Request only: revoke admission, enter QUIESCING. Never unmount or report
 * safe-to-remove completion. Future WP6b owns the drain/close/unmount barrier. */
sb_storage_status sb_storage_adapter_eject(sb_storage_adapter *,unsigned slot,const sb_music_media *);
/* Pump on notification fd or bounded scheduler deadline (1s while mounting up
 * to 60s, 30s while mounted; no-media idle has no periodic deadline). */
sb_storage_status sb_storage_adapter_pump(sb_storage_adapter *,uint64_t now_ms);
uint64_t sb_storage_adapter_deadline(const sb_storage_adapter *);
int sb_storage_adapter_fd(const sb_storage_adapter *);
const sb_storage_model *sb_storage_adapter_state(const sb_storage_adapter *);
sb_storage_status sb_storage_adapter_stop(sb_storage_adapter *);
sb_storage_status sb_storage_adapter_destroy(sb_storage_adapter *);
#endif
