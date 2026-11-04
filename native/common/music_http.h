#ifndef SB_MUSIC_HTTP_H
#define SB_MUSIC_HTTP_H
#include "music_contracts.h"

/* Pure HTTP policy, shared with the existing webserver adapter. No listener,
 * path resolution, token creation or I/O. Entire header must be supplied. */
typedef struct {
	int status; /* 200, 206 or 416 */
	uint64_t first, length;
} sb_music_http_range;

sb_music_http_range sb_music_http_select(uint64_t size, const char *range);
int sb_music_token_decode(const char *,
			  size_t,
			  unsigned char token[SB_MUSIC_TOKEN_BYTES]);
void sb_music_token_encode(const unsigned char token[SB_MUSIC_TOKEN_BYTES],
			   char hex[65]);
#endif
