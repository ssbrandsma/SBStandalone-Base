#include "vpn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "json")) {
		char *b = calloc(1, VPN_LIMIT + 2);
		size_t n = fread(b, 1, VPN_LIMIT + 1, stdin);
		int ok = vpn_json_valid(b, n);
		free(b);
		return ok ? 0 : 1;
	}
	char *data = calloc(1, VPN_CONFIG_LIMIT + 2);
	size_t n = fread(data, 1, VPN_CONFIG_LIMIT + 1, stdin);
	if (n > VPN_CONFIG_LIMIT || memchr(data, 0, n))
		return 1;
	VpnProfile *p = calloc(1, sizeof *p);
	char error[256];
	int result = vpn_profile_parse(data, p, error, sizeof error);
	if (result)
		puts(error);
	else
		printf("%d %d %s %u %s\ntls-min %s\n%s",
		       p->remotes,
		       p->needs_auth,
		       p->host[0],
		       p->port[0],
		       p->proto[0],
		       p->tls_min,
		       p->config);
	memset(data, 0, n);
	free(data);
	free(p);
	return result ? 1 : 0;
}
