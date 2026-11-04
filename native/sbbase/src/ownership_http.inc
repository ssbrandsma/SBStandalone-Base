/* Production authenticated route; never uses fixture proof routes. */
static void ownership_http(client *c,
			   const char *method,
			   const char *headers,
			   size_t hlen,
			   const char *body,
			   size_t size,
			   int chunked)
{
	char result[SB_OB_REPLY] = "{\"error\":\"ownership_request_rejected\"}";
	int status = 404;
	unsigned adapter = 0;
	const char *auth = NULL;
	size_t auth_size = 0;
	int auths = 0, cls = 0, tes = 0;
	struct sockaddr_in peer;
	socklen_t plen = sizeof peer;
	if (ownership_bridge.enabled) {
		status = 400;
		const char *line = memchr(headers, '\n', hlen);
		while (line && ++line < headers + hlen) {
			const char *end = memchr(
				line, '\r', (size_t)(headers + hlen - line));
			if (!end)
				break;
			size_t n = (size_t)(end - line);
			if (n >= 18 &&
			    !strncasecmp(line, "X-Music-Ownership:", 18)) {
				auths++;
				if (n == 83 && line[18] == ' ') {
					auth = line + 19;
					auth_size = 64;
				}
			}
			if (n >= 15 &&
			    !strncasecmp(line, "Content-Length:", 15))
				cls++;
			if (n >= 18 &&
			    !strncasecmp(line, "Transfer-Encoding:", 18))
				tes++;
			line = memchr(
				end, '\n', (size_t)(headers + hlen - end));
		}
		if (getpeername(c->fd, (struct sockaddr *)&peer, &plen) ||
		    peer.sin_family != AF_INET ||
		    (ntohl(peer.sin_addr.s_addr) >> 24) != 127 || auths != 1 ||
		    !sb_ob_auth(&ownership_bridge, auth, auth_size))
			status = 403;
		else if (strcmp(method, "POST"))
			status = 405;
		else if (!chunked && !tes && cls == 1) {
			status = sb_ob_command(&ownership_bridge,
					       body,
					       size,
					       np_monotonic_ms(),
					       result,
					       sizeof result);
			if (status == 202 &&
			    sscanf(body, "SBOWN1 query %u", &adapter) == 1) {
				for (int i = 0; i < CLIENTS; i++)
					if (clients[i].fd >= 0 &&
					    clients[i].ownership_wait ==
						    (int)adapter) {
						status = 409;
						break;
					}
				if (status == 202) {
					c->ownership_wait = (int)adapter;
					c->ownership_revision =
						production_owner.revision;
					c->ownership_deadline =
						np_monotonic_ms() + 20000;
					c->used = 0;
					return;
				}
			}
		}
	}
	if (status != 200)
		snprintf(result,
			 sizeof result,
			 "{\"error\":\"ownership_request_rejected\",\"status\":"
			 "%d}",
			 status);
	http_reply(c,
		   status == 200   ? "200 OK"
		   : status == 403 ? "403 Forbidden"
		   : status == 404 ? "404 Not Found"
		   : status == 405 ? "405 Method Not Allowed"
		   : status == 409 ? "409 Conflict"
				   : "400 Bad Request",
		   result,
		   0);
}

static void ownership_events(client *cs)
{
	if (!ownership_bridge.enabled)
		return;
	uint64_t now = np_monotonic_ms();
	sb_ob_tick(&ownership_bridge, now);
	int hold = sb_alarms_active(alarms);
	sb_ob_alarm(&ownership_bridge, hold);
	for (int i = 0; i < CLIENTS; i++) {
		client *c = &cs[i];
		if (c->fd < 0 || !c->ownership_wait)
			continue;
		if (c->ownership_revision != production_owner.revision ||
		    now >= c->ownership_deadline) {
			char out[SB_OB_REPLY];
			int ok = sb_ob_snapshot(&ownership_bridge,
						(unsigned)c->ownership_wait,
						now,
						out,
						sizeof out);
			c->ownership_wait = 0;
			http_reply(c,
				   ok ? "200 OK" : "409 Conflict",
				   ok ? out : "{}",
				   0);
		}
	}
}
