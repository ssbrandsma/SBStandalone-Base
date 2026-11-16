#include "vpn.h"
#include <ctype.h>
#include <string.h>

typedef struct {
	const unsigned char *p, *end;
	unsigned tokens;
} cursor;

static void space(cursor *c)
{
	while (c->p < c->end && *c->p && strchr(" \t\r\n", *c->p))
		c->p++;
}

static int hex(unsigned char c)
{
	return c >= '0' && c <= '9'   ? c - '0'
	       : c >= 'a' && c <= 'f' ? c - 'a' + 10
	       : c >= 'A' && c <= 'F' ? c - 'A' + 10
				      : -1;
}

static int unicode(cursor *c)
{
	int n = 0;
	for (unsigned i = 0; i < 4; i++) {
		if (c->p == c->end || hex(*c->p) < 0)
			return -1;
		n = n * 16 + hex(*c->p++);
	}
	return n;
}

static int string(cursor *c, int key)
{
	if (c->p == c->end || *c->p++ != '"')
		return 0;
	while (c->p < c->end) {
		unsigned ch = *c->p++;
		if (ch == '"')
			return 1;
		if (ch < 32)
			return 0;
		if (ch == '\\') {
			if (key || c->p == c->end)
				return 0; /* protocol keys are literal ASCII */
			ch = *c->p++;
			if (!ch)
				return 0;
			if (ch == 'u') {
				int u = unicode(c);
				if (u <= 0 || (u >= 0xdc00 && u <= 0xdfff))
					return 0;
				if (u >= 0xd800 && u <= 0xdbff) {
					if (c->end - c->p < 6 ||
					    c->p[0] != '\\' || c->p[1] != 'u')
						return 0;
					c->p += 2;
					u = unicode(c);
					if (u < 0xdc00 || u > 0xdfff)
						return 0;
				}
			} else if (!strchr("\"\\/bfnrt", ch))
				return 0;
		} else if (ch >= 128) {
			unsigned n = ch >= 0xc2 && ch <= 0xdf	? 1
				     : ch >= 0xe0 && ch <= 0xef ? 2
				     : ch >= 0xf0 && ch <= 0xf4 ? 3
								: 0;
			if (key || !n || (size_t)(c->end - c->p) < n)
				return 0;
			if ((ch == 0xe0 && c->p[0] < 0xa0) ||
			    (ch == 0xed && c->p[0] >= 0xa0) ||
			    (ch == 0xf0 && c->p[0] < 0x90) ||
			    (ch == 0xf4 && c->p[0] >= 0x90))
				return 0;
			for (unsigned i = 0; i < n; i++)
				if ((*c->p++ & 0xc0) != 0x80)
					return 0;
		}
	}
	return 0;
}

static int value(cursor *c, unsigned depth, unsigned budget)
{
	space(c);
	if (c->p == c->end || depth > 16 || ++c->tokens > budget)
		return 0;
	if (*c->p == '"')
		return string(c, 0);
	if (*c->p == '{' || *c->p == '[') {
		unsigned char open = *c->p++, close = open == '{' ? '}' : ']';
		const unsigned char *keys[32];
		size_t lengths[32];
		unsigned count = 0;
		space(c);
		if (c->p < c->end && *c->p == close) {
			c->p++;
			return 1;
		}
		for (;;) {
			if (open == '{') {
				const unsigned char *start = c->p;
				if (count == 32 || !string(c, 1))
					return 0;
				size_t n = (size_t)(c->p - start);
				for (unsigned i = 0; i < count; i++)
					if (n == lengths[i] &&
					    !memcmp(start, keys[i], n))
						return 0;
				keys[count] = start;
				lengths[count++] = n;
				space(c);
				if (c->p == c->end || *c->p++ != ':')
					return 0;
			}
			if (!value(c, depth + 1, budget))
				return 0;
			space(c);
			if (c->p == c->end)
				return 0;
			if (*c->p == close) {
				c->p++;
				return 1;
			}
			if (*c->p++ != ',')
				return 0;
			space(c);
		}
	}
	for (unsigned i = 0; i < 3; i++) {
		const char *literal = i == 0   ? "true"
				      : i == 1 ? "false"
					       : "null";
		size_t n = strlen(literal);
		if ((size_t)(c->end - c->p) >= n && !memcmp(c->p, literal, n)) {
			c->p += n;
			return 1;
		}
	}
	if (*c->p == '-')
		c->p++;
	if (c->p == c->end || !isdigit(*c->p))
		return 0;
	if (*c->p == '0')
		c->p++;
	else
		while (c->p < c->end && isdigit(*c->p))
			c->p++;
	if (c->p < c->end && *c->p == '.') {
		c->p++;
		if (c->p == c->end || !isdigit(*c->p))
			return 0;
		while (c->p < c->end && isdigit(*c->p))
			c->p++;
	}
	if (c->p < c->end && (*c->p == 'e' || *c->p == 'E')) {
		c->p++;
		if (c->p < c->end && (*c->p == '+' || *c->p == '-'))
			c->p++;
		if (c->p == c->end || !isdigit(*c->p))
			return 0;
		while (c->p < c->end && isdigit(*c->p))
			c->p++;
	}
	return 1;
}

int vpn_json_valid(const char *json, size_t length)
{
	if (!json || !length || length > VPN_LIMIT)
		return 0;
	cursor c = {(const unsigned char *)json,
		    (const unsigned char *)json + length,
		    0};
	if (!value(&c, 0, 32768))
		return 0;
	space(&c);
	return c.p == c.end;
}
