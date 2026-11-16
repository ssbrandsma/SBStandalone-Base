/* Bounded parsing only; network policy and cache stay in the VPN manager. */
#include "verification.h"
#include "vpn.h"
#include "../common/public_address.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int vpn_verify_ip(const char *body, size_t size, char out[16])
{
	if (!body || size > 64)
		return -1;
	while (size && isspace((unsigned char)*body)) {
		body++;
		size--;
	}
	while (size && isspace((unsigned char)body[size - 1]))
		size--;
	if (!size || size >= 16 || memchr(body, 0, size))
		return -1;
	char ip[16];
	memcpy(ip, body, size);
	ip[size] = 0;
	struct in_addr address;
	if (inet_pton(AF_INET, ip, &address) != 1 ||
	    !sb_public_ipv4((const unsigned char *)&address.s_addr))
		return -1;
	strcpy(out, ip);
	return 0;
}

static int
text(struct mg_str json, const char *key, char *out, size_t cap, int required)
{
	char path[64];
	mg_snprintf(path, sizeof path, "$.%s", key);
	struct mg_str token = mg_json_get_tok(json, path);
	if (!token.len || (!required && token.len == 4 &&
			    !memcmp(token.buf, "null", 4))) {
		*out = 0;
		return required ? -1 : 0;
	}
	char *s = mg_json_get_str(json, path);
	if (!s)
		return -1;
	int invalid = strlen(s) >= cap;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
		if (*p < 32 || *p == 127)
			invalid = 1;
	if (!invalid)
		memcpy(out, s, strlen(s) + 1);
	free(s);
	return invalid ? -1 : 0;
}

int vpn_verify_location(const char *body,
			size_t size,
			const char *expected,
			VpnLocation *out)
{
	if (!body || !size || size > 2048 || !vpn_json_valid(body, size))
		return -1;
	VpnLocation location;
	memset(&location, 0, sizeof location);
	struct mg_str j = mg_str_n(body, size);
	bool success = false;
	char ip[16];
	if (!mg_json_get_bool(j, "$.success", &success) || !success ||
	    text(j, "ip", ip, sizeof ip, 1) || strcmp(ip, expected) ||
	    text(j, "country_code", location.code, sizeof location.code, 1) ||
	    strlen(location.code) != 2 || location.code[0] < 'A' ||
	    location.code[0] > 'Z' || location.code[1] < 'A' ||
	    location.code[1] > 'Z')
		return -1;
	if (text(j, "country", location.country, sizeof location.country, 0) ||
	    text(j, "region", location.region, sizeof location.region, 0) ||
	    text(j, "city", location.city, sizeof location.city, 0))
		return -1;
	*out = location;
	return 0;
}
