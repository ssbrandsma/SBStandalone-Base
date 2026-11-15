#ifndef SB_REMOTE_HTTP_H
#define SB_REMOTE_HTTP_H
#include <stdint.h>
/* Worker-only. Numeric LAN HTTP, no redirects/DNS/proxies or decoder commands.
 * Returns a connected compressed-audio stream positioned after bounded headers.
 * Caller owns fd. Content-Length is mandatory; chunked transport is rejected. */
int sb_remote_http_open(const char *url,uint64_t *length);
#endif
