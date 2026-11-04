#include "music_http.h"
#include <string.h>

static int number(const char **input, uint64_t *out)
{
	const char *p = *input;
	uint64_t n = 0;
	if (*p < '0' || *p > '9')
		return 0;
	do {
		unsigned d = (unsigned)(*p++ - '0');
		if (n > (UINT64_MAX - d) / 10)
			return 0;
		n = n * 10 + d;
	} while (*p >= '0' && *p <= '9');
	*input = p;
	*out = n;
	return 1;
}

sb_music_http_range sb_music_http_select(uint64_t size, const char *range)
{
	sb_music_http_range r = {200, 0, size};
	if (!range)
		return r;
	r.status = 416;
	r.length = 0;
	/* Bound parsing work even for an unexpected caller bypassing the HTTP
	 * header bound. No multipart ranges, whitespace or trailing garbage. */
	size_t n = 0;
	while (n < 64 && range[n])
		n++;
	if (n == 64 || n < 7 || memcmp(range, "bytes=", 6) || !size)
		return r;
	const char *p = range + 6;
	uint64_t first, last;
	if (*p == '-') {
		p++;
		if (!number(&p, &last) || *p || !last)
			return r;
		r.length = last < size ? last : size;
		r.first = size - r.length;
	} else {
		if (!number(&p, &first) || *p++ != '-' || first >= size)
			return r;
		if (!*p)
			last = size - 1;
		else if (!number(&p, &last) || *p || last < first)
			return r;
		if (last >= size)
			last = size - 1;
		r.first = first;
		r.length = last - first + 1;
	}
	r.status = 206;
	return r;
}

int sb_music_token_decode(const char *s,
			  size_t n,
			  unsigned char token[SB_MUSIC_TOKEN_BYTES])
{
	unsigned char result[SB_MUSIC_TOKEN_BYTES];
	if (!s || !token || n != 64)
		return 0;
	for (unsigned i = 0; i < 64; i++) {
		unsigned char c = (unsigned char)s[i];
		unsigned value;
		if (c >= '0' && c <= '9')
			value = c - '0';
		else if (c >= 'a' && c <= 'f')
			value = c - 'a' + 10;
		else
			return 0;
		if (!(i & 1))
			result[i / 2] = (unsigned char)(value << 4);
		else
			result[i / 2] |= (unsigned char)value;
	}
	memcpy(token, result, sizeof result);
	return 1;
}

void sb_music_token_encode(const unsigned char token[SB_MUSIC_TOKEN_BYTES],
			   char hex[65])
{
	static const char digits[] = "0123456789abcdef";
	for (unsigned i = 0; i < SB_MUSIC_TOKEN_BYTES; i++) {
		hex[i * 2] = digits[token[i] >> 4];
		hex[i * 2 + 1] = digits[token[i] & 15];
	}
	hex[64] = 0;
}
