#ifndef SB_LMS_COMMAND_H
#define SB_LMS_COMMAND_H
#include <stddef.h>
#define SB_LMS_ARGUMENTS_MAX 24
#define SB_LMS_ARGUMENT_BYTES 512
#define SB_LMS_MESSAGE_MAX 16384u
/* Jive's request sink replays its whole backlog after any error (Comet.lua
 * _getRequestSink/_handshake never drop pending_reqs), so rejecting an
 * oversized batch wedges CometD in a permanent resend loop: every resend
 * exceeds the cap and nothing ever drains. Batches beyond 256 are
 * truncated to their head instead; the pump answers the first 32 with
 * keep-partial overflow, so any realistic backlog drains progressively
 * while per-request work stays bounded. Oversized replies remain
 * contained by SB_BROWSE_JSON_MAX via music_overflow. */
#define SB_LMS_BATCH_MAX 256u
typedef enum { SB_COMMAND_OK, SB_COMMAND_UNKNOWN, SB_COMMAND_UNSUPPORTED,
	SB_COMMAND_MALFORMED, SB_COMMAND_TOO_LARGE } sb_command_result;
typedef struct {
	char player[64];
	char arguments[SB_LMS_ARGUMENTS_MAX][SB_LMS_ARGUMENT_BYTES];
	unsigned count;
} sb_lms_command;
/* Strict JSON validation precedes the existing bounded JSON string decoder.
 * No allocations; one complete Comet message object (not its batch). */
int sb_lms_json_valid(const char *, size_t);
int sb_lms_json_valid_budget(const char *, size_t, unsigned);
sb_command_result sb_lms_command_parse(const char *, size_t, sb_lms_command *);
const char *sb_lms_tag(const sb_lms_command *, const char *);
const char *sb_lms_command_error(sb_command_result);
#endif
