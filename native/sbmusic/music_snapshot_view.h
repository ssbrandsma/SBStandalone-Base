#ifndef SB_MUSIC_SNAPSHOT_VIEW_H
#define SB_MUSIC_SNAPSHOT_VIEW_H
#include "music_snapshot.h"
#include "music_library.h"
typedef sb_storage_status (*sb_snapshot_identity_reader)(void *,const sb_storage_mount *,sb_storage_evidence *);
typedef struct {
 sb_storage_root initial_root; /* borrowed; immutable configuration */
 char directory[64],work[128],pending_path[256];
 uint64_t maximum_bytes,sequence,generation,pending_sequence,pending_generation;
 uint64_t corrupt_hint,retry_after;
 int offline;
 sb_storage_mount_reader mounts;void *mount_context;
 sb_snapshot_identity_reader identity;void *identity_context;
} sb_music_snapshot_view;
sb_ml_status sb_music_snapshot_refresh(void *,const char *,sb_ml_refresh *);
#endif
