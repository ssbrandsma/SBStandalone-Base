#include "music_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
check(uint64_t size, const char *s, int status, uint64_t first, uint64_t length)
{
	sb_music_http_range r = sb_music_http_select(size, s);
	assert(r.status == status);
	if (status != 416)
		assert(r.first == first && r.length == length);
}

int main(void)
{
	check(0, NULL, 200, 0, 0);
	check(0, "bytes=0-", 416, 0, 0);
	check(100, NULL, 200, 0, 100);
	check(100, "bytes=0-9", 206, 0, 10);
	check(100, "bytes=10-", 206, 10, 90);
	check(100, "bytes=-10", 206, 90, 10);
	check(100, "bytes=-1000", 206, 0, 100);
	check(100, "bytes=90-999", 206, 90, 10);
	check(100, "bytes=100-", 416, 0, 0);
	check(100, "bytes=9-8", 416, 0, 0);
	check(100, "bytes=-0", 416, 0, 0);
	check(100, "bytes=0-1,5-6", 416, 0, 0);
	check(100, "bytes=0-1\r\nX: y", 416, 0, 0);
	check(100, "bytes=18446744073709551616-", 416, 0, 0);
	check(UINT64_MAX,
	      "bytes=18446744073709551614-",
	      206,
	      UINT64_MAX - 1,
	      1);
	check(UINT64_MAX, "bytes=0-18446744073709551615", 206, 0, UINT64_MAX);
	check(UINT64_MAX, "bytes=-18446744073709551615", 206, 0, UINT64_MAX);
	check(UINT64_C(8589934592),
	      "bytes=4294967296-4294967395",
	      206,
	      UINT64_C(4294967296),
	      100);
	char huge[80];
	memset(huge, '1', sizeof huge);
	huge[79] = 0;
	check(100, huge, 416, 0, 0);
	unsigned char token[32], decoded[32];
	char hex[65];
	for (unsigned i = 0; i < 32; i++)
		token[i] = (unsigned char)(i * 7);
	sb_music_token_encode(token, hex);
	assert(sb_music_token_decode(hex, 64, decoded));
	assert(!memcmp(token, decoded, 32));
	memset(decoded, 42, sizeof decoded);
	hex[7] = '/';
	assert(!sb_music_token_decode(hex, 64, decoded));
	for (unsigned i = 0; i < 32; i++)
		assert(decoded[i] == 42);
	assert(!sb_music_token_decode(hex, 63, decoded));
	puts("HTTP single-range 64-bit bounds and opaque token grammar passed "
	     "(policy only)");
	return 0;
}
