#ifndef SB_MUSIC_SNAPSHOT_H
#define SB_MUSIC_SNAPSHOT_H
#include "music_storage.h"
/* SQLite runs on caller-approved private RAM. Removable media holds two
 * checksummed closed database snapshots, never a live SQLite journal. */
typedef struct {
    sb_storage_root root;
    sb_storage_mount_reader reader;
    void *context;
    int directory;
    uint64_t sequence, maximum_bytes;
    int slot, damaged, quiet;
    char name[64];
} sb_music_snapshot;
int sb_music_snapshot_open(sb_music_snapshot *, const sb_storage_root *,
    const char *directory, int create, uint64_t maximum_bytes,
    sb_storage_mount_reader, void *);
/* 1 restored, 0 genuinely empty, -1 corrupt/I/O/stale; never resets corruption. */
int sb_music_snapshot_load(sb_music_snapshot *, int output_fd);
int sb_music_snapshot_load_previous(sb_music_snapshot *,int output_fd);
int sb_music_snapshot_publish(sb_music_snapshot *, int closed_database_fd);
/* Explicit operator recovery only; preserve a corrupt slot before replacing it. */
int sb_music_snapshot_recover(sb_music_snapshot *);
/* Untrusted change hint only; any new generation still requires full load/hash. */
uint64_t sb_music_snapshot_hint(sb_music_snapshot *);
void sb_music_snapshot_close(sb_music_snapshot *);
int sb_music_snapshot_source(const sb_storage_evidence *,sb_music_source_id *);
#endif
