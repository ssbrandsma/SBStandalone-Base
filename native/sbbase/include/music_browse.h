#ifndef SB_MUSIC_BROWSE_H
#define SB_MUSIC_BROWSE_H
#include "lms_command.h"
#include "../../sbmusic/music_async.h"
#define SB_BROWSE_WIRE_PAGE 200u
#define SB_BROWSE_JSON_MAX 262144u
typedef struct sb_browse sb_browse;
typedef struct sb_browse_call sb_browse_call;
sb_browse *sb_browse_start(sb_music_library *,
			   const sb_music_media *,
			   int enabled,
			   int storage_supported);
int sb_browse_fd(const sb_browse *);
void sb_browse_player(sb_browse *, const char *);
int sb_browse_media(sb_browse *, const sb_music_media *, int scan_active);
void sb_browse_tick(sb_browse *, uint64_t now);
uint64_t sb_browse_version(const sb_browse *);
uint64_t sb_browse_media_version(const sb_browse *);
int sb_browse_available(const sb_browse *);
unsigned sb_browse_pending_peak(sb_browse *);
/* Trusted native consumers share this dispatcher, never attach a second one. */
sb_music_async *sb_browse_dispatcher(sb_browse *);
/* Nonzero only when trusted test playback/adapter/resource integration exists.
 * Bits match negotiated MP3=1/FLAC=2, never scanner recognition of Vorbis. */
void sb_browse_playback(sb_browse *, unsigned negotiated_codecs);
void sb_browse_control_context(sb_browse *, uint64_t context);
int sb_browse_native(const sb_lms_command *);
sb_browse_call *
sb_browse_begin(sb_browse *, const sb_lms_command *, uint64_t deadline);
/* Returns 1 completed / 0 pending. Data remains owned by the call. */
int sb_browse_poll(sb_browse *, sb_browse_call *);
const char *sb_browse_json(const sb_browse_call *);
int sb_browse_success(const sb_browse_call *);
int sb_browse_valid(const sb_browse_call *);
void sb_browse_cancel(sb_browse *, sb_browse_call *);
/* Native menu nodes or remove IDs, no enclosing menu envelope. */
int sb_browse_nodes(sb_browse *, int remove, char *, size_t, unsigned *count);
sb_ml_status sb_browse_stop(sb_browse *);
sb_ml_status sb_browse_destroy(sb_browse *);
#endif
