#ifndef SB_LMS_COMMAND_H
#define SB_LMS_COMMAND_H
#include <stddef.h>
#define SB_LMS_ARGUMENTS_MAX 24
#define SB_LMS_ARGUMENT_BYTES 512
#define SB_LMS_MESSAGE_MAX 16384u
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
sb_command_result sb_lms_command_parse(const char *, size_t, sb_lms_command *);
const char *sb_lms_tag(const sb_lms_command *, const char *);
const char *sb_lms_command_error(sb_command_result);
#endif
