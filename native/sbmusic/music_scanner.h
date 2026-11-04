#ifndef SB_MUSIC_SCANNER_H
#define SB_MUSIC_SCANNER_H
#include "music_library.h"
#include "music_storage.h"
#define SB_SCAN_DEPTH 16
#define SB_SCAN_ERRORS 16
#define SB_SCAN_BATCH 16
typedef enum {
    SB_SCAN_IDLE,
    SB_SCAN_QUEUED,
    SB_SCAN_SCANNING,
    SB_SCAN_COMMITTING,
    SB_SCAN_COMPLETED,
    SB_SCAN_CANCELLED,
    SB_SCAN_FAILED,
    SB_SCAN_MEDIA_REMOVED
} sb_scan_state;
typedef enum {
    SB_SCAN_OK,
    SB_SCAN_DISABLED,
    SB_SCAN_BUSY,
    SB_SCAN_INVALID,
    SB_SCAN_POLICY,
    SB_SCAN_IO
} sb_scan_code;
typedef struct {
    char path[128], message[128];
    int code;
} sb_scan_error;
typedef struct {
    sb_scan_state state;
    uint64_t request_id, generation, epoch, library_revision;
    uint64_t visited, recognized, added, updated, unchanged, renamed, removed, errors, elapsed_ms,
        parser_bytes;
    unsigned error_count, max_descriptors, parser_peak_rss_kib;
    sb_scan_error error[SB_SCAN_ERRORS];
} sb_scan_status;
typedef struct {
    int enabled, approved_database; /* explicit independently qualified DB; no default */
    sb_music_library *library;      /* already OPEN/SOURCE complete; broker access mode */
    sb_storage_root root;           /* source + validated fixed scan scope, borrowed then dup'd */
    char helper[SB_STORAGE_PATH];   /* explicit trusted executable, not user input */
    sb_storage_mount_reader mount_reader;
    void *mount_context;
    unsigned helper_timeout_ms, yield_ms, batch_files; /* <=5000, <=1000, <=16 */
    unsigned work_duty_percent, io_bytes_per_second;   /* 1..100 (0 defaults 50); payload pacing */
    int inode_identity; /* opt-in ext2/ext3 or controlled tmpfs only */
} sb_scan_options;
typedef struct sb_music_scanner sb_music_scanner;
/* No implicit construction or product integration. Disabled does no work. */
sb_music_scanner *sb_music_scan_create(const sb_scan_options *, sb_scan_code *);
sb_scan_code sb_music_scan_request(sb_music_scanner *, uint64_t request_id, int initial);
sb_scan_code sb_music_scan_cancel(sb_music_scanner *, uint64_t request_id);
sb_scan_code sb_music_scan_media(sb_music_scanner *, const sb_music_media *current);
sb_scan_code sb_music_scan_status(sb_music_scanner *, sb_scan_status *);
int sb_music_scan_fd(const sb_music_scanner *); /* coalesced progress/terminal wakeup */
sb_scan_code sb_music_scan_stop(sb_music_scanner *);
sb_scan_code sb_music_scan_destroy(sb_music_scanner *); /* exited only */
#endif
