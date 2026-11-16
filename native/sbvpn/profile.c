/* Never pass uploaded OpenVPN text through to the privileged client. */
#include "vpn.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *out, size_t n, const char *s)
{
	snprintf(out, n, "%s", s);
	return -1;
}

static int append(VpnProfile *p, const char *s)
{
	size_t used = strlen(p->config), n = strlen(s);
	if (n + used + 2 > sizeof p->config)
		return -1;
	memcpy(p->config + used, s, n);
	p->config[used + n] = '\n';
	p->config[used + n + 1] = 0;
	return 0;
}

static int number(const char *s, unsigned lo, unsigned hi)
{
	if (!*s || strspn(s, "0123456789") != strlen(s))
		return 0;
	unsigned long n = strtoul(s, NULL, 10);
	return n >= lo && n <= hi;
}

static int host(const char *s)
{
	if (!*s || strlen(s) > 253 || s[0] == '-' || s[0] == '.')
		return 0;
	for (; *s; s++)
		if (!isalnum((unsigned char)*s) && *s != '.' && *s != '-')
			return 0;
	return 1;
}

/* Quotes are supported; escapes and multiline directives are deliberately not.
 */
static int words(char *line, char *v[5])
{
	int n = 0;
	char *p = line;
	while (*p) {
		while (isspace((unsigned char)*p))
			p++;
		if (!*p || *p == '#' || *p == ';')
			break;
		if (n == 5)
			return -1;
		char quote = (*p == '"' || *p == '\'') ? *p++ : 0;
		v[n++] = p;
		while (*p &&
		       (quote ? *p != quote : !isspace((unsigned char)*p))) {
			if (*p == '\\' || (*p == '\"' || *p == '\'') ||
			    (unsigned char)*p < 32)
				return -1;
			p++;
		}
		if (quote && !*p)
			return -1;
		if (*p)
			*p++ = 0;
		if (quote && *p && !isspace((unsigned char)*p))
			return -1;
	}
	return n;
}

int vpn_profile_parse(const char *input,
		      VpnProfile *out,
		      char *error,
		      size_t cap)
{
	memset(out, 0, sizeof *out);
	strcpy(out->tls_min, "1.2");
	size_t len = strlen(input);
	if (!len || len > VPN_CONFIG_LIMIT)
		return fail(error, cap, "Profile must be 1-49152 bytes");
	char *copy = strdup(input);
	if (!copy)
		return fail(error, cap, "Out of memory");
	char *save = NULL, *line;
	char block[16] = "";
	int client = 0, verify = 0, inline_bytes = 0, seen_ca = 0,
	    seen_cert = 0, seen_key = 0;
	char protocol[16] = "udp4";
	unsigned port = 1194;
	int result = -1;
	for (line = strtok_r(copy, "\n", &save); line;
	     line = strtok_r(NULL, "\n", &save)) {
		size_t l = strlen(line);
		if (l && line[l - 1] == '\r')
			line[--l] = 0;
		if (l > 2048) {
			fail(error, cap, "Profile line exceeds 2048 bytes");
			goto done;
		}
		for (size_t i = 0; i < l; i++)
			if ((unsigned char)line[i] < 32 && line[i] != '\t') {
				fail(error, cap, "Invalid profile character");
				goto done;
			}
		if (*block) {
			char closing[24];
			snprintf(closing, sizeof closing, "</%s>", block);
			if (!strcmp(line, closing)) {
				if (!inline_bytes) {
					fail(error,
					     cap,
					     "Empty inline certificate/key");
					goto done;
				}
				block[0] = 0;
			} else {
				if (strchr(line, '<') || strchr(line, '>') ||
				    strstr(line, "ENCRYPTED")) {
					fail(error,
					     cap,
					     "Malformed inline material or "
					     "encrypted private key; use an "
					     "unencrypted service key");
					goto done;
				}
				inline_bytes += (int)l;
			}
			if (append(out, line))
				goto size;
			continue;
		}
		while (*line == ' ' || *line == '\t')
			line++;
		if (!*line || *line == '#' || *line == ';')
			continue;
		if (*line == '<') {
			const char *tags[] = {
				"ca", "cert", "key", "tls-auth", "tls-crypt"};
			int found = 0;
			for (unsigned i = 0; i < 5; i++) {
				char opening[24];
				snprintf(opening,
					 sizeof opening,
					 "<%s>",
					 tags[i]);
				if (!strcmp(line, opening)) {
					strcpy(block, tags[i]);
					found = 1;
					break;
				}
			}
			if (!found) {
				fail(error, cap, "Unsupported inline section");
				goto done;
			}
			if ((!strcmp(block, "ca") && seen_ca++) ||
			    (!strcmp(block, "cert") && seen_cert++) ||
			    (!strcmp(block, "key") && seen_key++)) {
				fail(error,
				     cap,
				     "Duplicate certificate/key section");
				goto done;
			}
			inline_bytes = 0;
			if (append(out, line))
				goto size;
			continue;
		}
		char *v[5];
		int count = words(line, v);
		if (count <= 0) {
			fail(error, cap, "Malformed profile directive");
			goto done;
		}
		const char *k = v[0];
		int valid = 0;
		if (!strcmp(k, "client")) {
			valid = count == 1;
			client++;
		} else if (!strcmp(k, "dev")) {
			valid = count == 2 && !strcmp(v[1], "tun");
			if (!valid)
				goto invalid;
			continue;
		} else if (!strcmp(k, "dev-type")) {
			valid = count == 2 && !strcmp(v[1], "tun");
			if (!valid) {
				fail(error,
				     cap,
				     "Only TUN profiles are supported");
				goto done;
			}
			continue;
		} else if (!strcmp(k, "proto")) {
			valid = count == 2 && (!strcmp(v[1], "udp") ||
					       !strcmp(v[1], "udp4") ||
					       !strcmp(v[1], "tcp-client") ||
					       !strcmp(v[1], "tcp4-client"));
			if (valid)
				strcpy(protocol,
				       v[1][0] == 'u' ? "udp4" : "tcp4-client");
			if (!valid) {
				fail(error,
				     cap,
				     "Only IPv4 UDP/TCP client profiles are "
				     "supported");
				goto done;
			}
			continue;
		} else if (!strcmp(k, "port") || !strcmp(k, "rport")) {
			valid = count == 2 && number(v[1], 1, 65535);
			if (!valid)
				goto invalid;
			port = (unsigned)atoi(v[1]);
			continue;
		} else if (!strcmp(k, "remote")) {
			valid = count >= 2 && count <= 4 && host(v[1]) &&
				(count < 3 || number(v[2], 1, 65535)) &&
				(count < 4 || !strcmp(v[3], "udp") ||
				 !strcmp(v[3], "udp4") ||
				 !strcmp(v[3], "tcp-client") ||
				 !strcmp(v[3], "tcp4-client"));
			if (!valid || out->remotes == 8) {
				fail(error,
				     cap,
				     "Invalid remote endpoint or more than "
				     "eight remotes");
				goto done;
			}
			int i = out->remotes++;
			strcpy(out->host[i], v[1]);
			out->port[i] = count >= 3 ? (unsigned)atoi(v[2]) : 0;
			if (count == 4)
				strcpy(out->proto[i],
				       v[3][0] == 'u' ? "udp4" : "tcp4-client");
			continue;
		} else if (!strcmp(k, "auth-user-pass")) {
			if (count != 1) {
				fail(error,
				     cap,
				     "External credential files are forbidden; "
				     "provision credentials securely");
				goto done;
			}
			out->needs_auth = 1;
			continue;
		} else if (!strcmp(k, "remote-cert-tls")) {
			valid = count == 2 && !strcmp(v[1], "server");
			verify |= valid;
		} else if (!strcmp(k, "verify-x509-name")) {
			valid = (count == 2 || count == 3) &&
				strlen(v[1]) < 128 &&
				(count < 3 || !strcmp(v[2], "name") ||
				 !strcmp(v[2], "subject") ||
				 !strcmp(v[2], "name-prefix"));
		} else if (!strcmp(k, "tls-version-min")) {
			valid = count == 2 &&
				(!strcmp(v[1], "1.2") || !strcmp(v[1], "1.3"));
			if (valid) {
				strcpy(out->tls_min, v[1]);
				continue;
			}
		} else if (!strcmp(k, "cipher") || !strcmp(k, "data-ciphers") ||
			   !strcmp(k, "data-ciphers-fallback")) {
			valid = count == 2;
			char b[256];
			if (!valid || strlen(v[1]) >= sizeof b)
				goto invalid;
			strcpy(b, v[1]);
			char *sv, *c;
			for (c = strtok_r(b, ":", &sv); c;
			     c = strtok_r(NULL, ":", &sv))
				if (strcmp(c, "AES-128-GCM") &&
				    strcmp(c, "AES-256-GCM") &&
				    strcmp(c, "AES-128-CBC") &&
				    strcmp(c, "AES-256-CBC") &&
				    strcmp(c, "CHACHA20-POLY1305"))
					valid = 0;
		} else if (!strcmp(k, "auth")) {
			valid = count == 2 && (!strcmp(v[1], "SHA256") ||
					       !strcmp(v[1], "SHA384") ||
					       !strcmp(v[1], "SHA512") ||
					       !strcmp(v[1], "SHA1"));
		} else if (!strcmp(k, "key-direction")) {
			valid = count == 2 &&
				(!strcmp(v[1], "0") || !strcmp(v[1], "1"));
		} else if (!strcmp(k, "nobind") || !strcmp(k, "persist-key") ||
			   !strcmp(k, "persist-tun") ||
			   !strcmp(k, "remote-random") ||
			   !strcmp(k, "auth-nocache")) {
			valid = count == 1;
			if (!valid)
				goto invalid;
			continue;
		} else if (!strcmp(k, "resolv-retry")) {
			valid = count == 2 && (!strcmp(v[1], "infinite") ||
					       number(v[1], 1, 300));
			if (!valid)
				goto invalid;
			continue;
		} else if (!strcmp(k, "verb")) {
			valid = count == 2 && number(v[1], 0, 4);
			if (!valid)
				goto invalid;
			continue;
		} else if (!strcmp(k, "pull") || !strcmp(k, "ping-timer-rem")) {
			valid = count == 1;
			if (!valid)
				goto invalid;
			continue;
		} else if (!strcmp(k, "fast-io")) {
			valid = count == 1;
		} else if (!strcmp(k, "ping") || !strcmp(k, "ping-restart") ||
			   !strcmp(k, "ping-exit") ||
			   !strcmp(k, "connect-timeout")) {
			valid = count == 2 && number(v[1], 0, 300);
			if (!valid)
				goto invalid;
			continue;
		} else if (!strcmp(k, "keepalive")) {
			valid = count == 3 && number(v[1], 1, 60) &&
				number(v[2], 1, 300);
			if (!valid)
				goto invalid;
			continue;
		} else if (!strcmp(k, "auth-retry")) {
			valid = count == 2 && (!strcmp(v[1], "none") ||
					       !strcmp(v[1], "nointeract"));
			if (!valid)
				goto invalid;
			continue;
		} else if (!strcmp(k, "tun-mtu-extra")) {
			valid = count == 2 && number(v[1], 0, 64);
			if (!valid)
				goto invalid;
			continue;
		} else if (!strcmp(k, "tun-mtu")) {
			valid = count == 2 && number(v[1], 576, 1500);
		} else if (!strcmp(k, "mssfix")) {
			valid = count == 2 &&
				(!strcmp(v[1], "0") || number(v[1], 576, 1450));
		} else if (!strcmp(k, "reneg-sec")) {
			valid = count == 2 && number(v[1], 0, 86400);
		} else if (!strcmp(k, "explicit-exit-notify")) {
			valid = count == 1 ||
				(count == 2 && number(v[1], 0, 2));
		} else if (!strcmp(k, "allow-compression")) {
			valid = count == 2 && !strcmp(v[1], "no");
		} else {
			snprintf(error,
				 cap,
				 "Unsupported OpenVPN directive (scripts, "
				 "files, plugins, routing overrides and "
				 "compression are not accepted)");
			goto done;
		}
	invalid:
		if (!valid) {
			snprintf(
				error,
				cap,
				"Invalid arguments for OpenVPN directive %.64s",
				k);
			goto done;
		}
		char normalized[2300];
		size_t used = 0;
		for (int i = 0; i < count; i++) {
			int n = snprintf(normalized + used,
					 sizeof normalized - used,
					 "%s\"%s\"",
					 i ? " " : "",
					 v[i]);
			if (n < 0 || (size_t)n >= sizeof normalized - used)
				goto size;
			used += (size_t)n;
		}
		if (append(out, normalized))
			goto size;
	}
	if (*block || !client || !verify || !seen_ca || !out->remotes ||
	    seen_cert != seen_key) {
		fail(error,
		     cap,
		     "Require client, remote, remote-cert-tls server, inline "
		     "CA, and matching inline certificate/key; inline section "
		     "must be closed");
		goto done;
	}
	out->has_ca = seen_ca;
	out->has_cert = seen_cert;
	out->has_key = seen_key;
	for (int i = 0; i < out->remotes; i++) {
		if (!out->port[i])
			out->port[i] = port;
		if (!*out->proto[i])
			strcpy(out->proto[i], protocol);
	}
	result = 0;
	goto done;
size:
	fail(error, cap, "Normalized profile is too large");
done:
	memset(copy, 0, len);
	free(copy);
	return result;
}
