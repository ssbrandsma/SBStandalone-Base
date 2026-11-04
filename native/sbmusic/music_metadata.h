#ifndef SB_MUSIC_METADATA_H
#define SB_MUSIC_METADATA_H
#include "music_library.h"
#define SB_META_MAX (256u * 1024u)
#define SB_META_READ_MAX (768u * 1024u)
typedef enum {
    SB_META_OK,
    SB_META_UNSUPPORTED,
    SB_META_INVALID,
    SB_META_LIMIT,
    SB_META_IO
} sb_meta_status;
typedef struct {
    unsigned magic;
    sb_meta_status status;
    unsigned warnings;
    unsigned peak_rss_kib; /* optional helper self VmHWM; never parent wait4's inherited peak */
    uint64_t bytes_read;
    sb_ml_track track;
    char error[160];
} sb_metadata_result;
/* Descriptor only. Never opens a supplied path or decodes audio/images. Run
 * through the isolated helper for untrusted files, not on the protocol loop. */
void sb_music_metadata(int fd, sb_metadata_result *);
#endif
