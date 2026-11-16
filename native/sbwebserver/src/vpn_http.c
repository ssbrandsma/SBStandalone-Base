/* Nonblocking bridge to the single privileged VPN manager. No shell execution.
 */
#include "vpn_http.h"
#include "auth.h"
#include "../../sbvpn/vpn.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static struct {
	int fd, sent;
	unsigned long client;
	uint64_t deadline;
	char *request;
} jobs[4];

static int prefix(struct mg_str uri, const char *s)
{
	size_t n = strlen(s);
	return uri.len >= n && !memcmp(uri.buf, s, n) &&
	       (uri.len == n || uri.buf[n] == '/');
}

int vpn_http_is_api(struct mg_str uri)
{
	return prefix(uri, "/api/vpn") || prefix(uri, "/api/internal/vpn");
}

static void reply(struct mg_connection *c, int code, const char *message)
{
	mg_http_reply(
		c,
		code,
		"Content-Type: application/json\r\nCache-Control: no-store\r\n",
		"{\"error\":%m}",
		MG_ESC(message));
}


void vpn_http_handle(struct mg_connection *c,
		     struct mg_http_message *m,
		     int internal,
		     int readonly)
{
	int native = prefix(m->uri, "/api/internal/vpn");
	if ((native && !internal) || (!native && !auth_session_ok(c, m, NULL))) {
		reply(c,
		      401,
		      "Browser pairing or trusted applet capability required");
		return;
	}
	int read = mg_strcmp(m->method, mg_str("GET")) == 0;
	if (!read && (readonly || (!native && !auth_origin_ok(c, m)))) {
		reply(c,
		      403,
		      readonly ? "Management is read-only"
			       : "Same-origin request required");
		return;
	}
	size_t offset = strlen(native ? "/api/internal/vpn" : "/api/vpn");
	char path[128], method[12];
	if (m->uri.len - offset >= sizeof path ||
	    m->method.len >= sizeof method) {
		reply(c, 400, "Invalid VPN path or method");
		return;
	}
	memcpy(path, m->uri.buf + offset, m->uri.len - offset);
	path[m->uri.len - offset] = 0;
	memcpy(method, m->method.buf, m->method.len);
	method[m->method.len] = 0;
	if (!strncmp(path, "/event", 6)) {
		reply(c, 403, "Private process hooks are not HTTP endpoints");
		return;
	}
	if (native && !read &&
	    (!strcmp(path, "/profiles") || !strncmp(path, "/profiles/", 10))) {
		reply(c,
		      403,
		      "Provision profiles through the paired browser using an "
		      "SSH tunnel");
		return;
	}
	if (!read &&
	    (!strcmp(path, "/profiles") || !strncmp(path, "/profiles/", 10)) &&
	    !(strlen(path) == 51 && !strcmp(path + 42, "/validate") &&
	      !strcmp(method, "POST")) &&
	    !auth_sensitive_transport(c,m)) {
		reply(c,
		      426,
		      "Profile and credential provisioning requires a paired "
		      "HTTPS browser or SSH recovery tunnel; LAN HTTP is not "
		      "encrypted");
		return;
	}
	if (m->body.len > VPN_LIMIT - 256) {
		reply(c,
		      413,
		      "VPN upload exceeds 127 KiB request limit (49 KiB "
		      "profile limit)");
		return;
	}
	int slot = -1;
	for (int i = 0; i < 4; i++) {
		if (jobs[i].request && jobs[i].client == c->id) {
			reply(c,
			      409,
			      "A VPN request is already pending on this "
			      "connection");
			return;
		}
		if (!jobs[i].request)
			slot = i;
	}
	if (slot < 0) {
		reply(c, 503, "VPN request queue is busy");
		return;
	}
	const char *socket_path = VPN_SOCKET;
#ifdef SB_VPN_TESTING
	if (getenv("SB_VPN_TEST_SOCKET"))
		socket_path = getenv("SB_VPN_TEST_SOCKET");
#endif
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	if (strlen(socket_path) >= sizeof sa.sun_path) {
		reply(c, 503, "VPN manager socket unavailable");
		return;
	}
	strcpy(sa.sun_path, socket_path);
	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		reply(c, 503, "VPN manager unavailable");
		return;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fcntl(fd, F_SETFL, O_NONBLOCK);
	int size = VPN_LIMIT * 2;
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) &&
	    errno != EINPROGRESS) {
		close(fd);
		reply(c,
		      503,
		      "VPN manager unavailable; update or enable the "
		      "Standalone Base service");
		return;
	}
	char *request = mg_mprintf("{\"method\":%m,\"path\":%m,\"body\":%.*s}",
				   MG_ESC(method),
				   MG_ESC(path),
				   (int)(m->body.len ? m->body.len : 2),
				   m->body.len ? m->body.buf : "{}");
	if (!request) {
		close(fd);
		reply(c, 500, "Out of memory");
		return;
	}
	jobs[slot].fd = fd;
	jobs[slot].request = request;
	jobs[slot].client = c->id;
	jobs[slot].deadline = mg_millis() + 10000;
	jobs[slot].sent = 0;
}

void vpn_http_tick(struct mg_mgr *mgr)
{
	for (int i = 0; i < 4; i++)
		if (jobs[i].request) {
			struct mg_connection *client = NULL;
			for (struct mg_connection *c = mgr->conns; c;
			     c = c->next)
				if (c->id == jobs[i].client && !c->is_closing)
					client = c;
			int done = !client;
			if (!done && !jobs[i].sent) {
				struct ucred peer;
				socklen_t length = sizeof peer;
				if (getsockopt(jobs[i].fd,
					       SOL_SOCKET,
					       SO_PEERCRED,
					       &peer,
					       &length) ||
				    peer.uid != geteuid()) {
					reply(client,
					      503,
					      "VPN manager peer identity "
					      "rejected");
					done = 1;
				}
			}
			if (!done && !jobs[i].sent) {
				ssize_t n = send(jobs[i].fd,
						 jobs[i].request,
						 strlen(jobs[i].request),
						 MSG_NOSIGNAL);
				if (n == (ssize_t)strlen(jobs[i].request))
					jobs[i].sent = 1;
				else if (n >= 0 || (errno != EAGAIN &&
						    errno != EWOULDBLOCK)) {
					reply(client,
					      503,
					      "VPN manager request failed");
					done = 1;
				}
			}
			if (!done && jobs[i].sent) {
				char *buffer = malloc(VPN_LIMIT + 1);
				if (!buffer) {
					reply(client, 500, "Out of memory");
					done = 1;
				} else {
					ssize_t n = recv(jobs[i].fd,
							 buffer,
							 VPN_LIMIT,
							 MSG_TRUNC);
					if (n > 0) {
						if (n > (ssize_t)VPN_LIMIT)
							reply(client,
							      502,
							      "VPN manager "
							      "response "
							      "exceeds limit");
						else {
							buffer[n] = 0;
							struct mg_str
								j = mg_str_n(
									buffer,
									(size_t)n),
								data = mg_json_get_tok(
									j,
									"$."
									"data");
							long status =
								mg_json_get_long(
									j,
									"$."
									"statu"
									"s",
									502);
							if (status < 200 ||
							    status > 599 ||
							    !data.len)
								reply(client,
								      502,
								      "Invalid "
								      "VPN "
								      "manager "
								      "respons"
								      "e");
							else
								mg_http_reply(
									client,
									(int)status,
									"Conten"
									"t-"
									"Type: "
									"applic"
									"ation/"
									"json\r"
									"\nCach"
									"e-"
									"Contro"
									"l: "
									"no-"
									"store"
									"\r\n",
									"%.*s",
									(int)data
										.len,
									data.buf);
						}
						done = 1;
					} else if (!n ||
						   (n < 0 && errno != EAGAIN &&
						    errno != EWOULDBLOCK)) {
						reply(client,
						      503,
						      "VPN manager "
						      "disconnected");
						done = 1;
					}
					free(buffer);
				}
			}
			if (!done && mg_millis() >= jobs[i].deadline) {
				reply(client,
				      504,
				      "VPN manager timed out; refresh status "
				      "before retrying");
				done = 1;
			}
			if (done) {
				close(jobs[i].fd);
				memset(jobs[i].request,
				       0,
				       strlen(jobs[i].request));
				free(jobs[i].request);
				memset(&jobs[i], 0, sizeof jobs[i]);
			}
		}
}

int vpn_http_headers(struct mg_connection *c, int event, void *data)
{
	if (event == MG_EV_HTTP_HDRS) {
		struct mg_http_message *m = data;
		if (vpn_http_is_api(m->uri)) {
			struct mg_str *length =
				mg_http_get_header(m, "Content-Length");
			unsigned long size = 0;
			int invalid = 0;
			if (length)
				for (size_t i = 0; i < length->len; i++) {
					if (length->buf[i] < '0' ||
					    length->buf[i] > '9' ||
					    size > VPN_LIMIT / 10) {
						invalid = 1;
						break;
					}
					size = size * 10 +
					       (unsigned)(length->buf[i] - '0');
				}
			if (invalid || size > VPN_LIMIT - 256) {
				reply(c, 413, "VPN request exceeds limit");
				c->is_draining = 1;
				return 1;
			}
			if (mg_http_get_header(m, "Transfer-Encoding")) {
				reply(c,
				      411,
				      "A bounded Content-Length is required");
				c->is_draining = 1;
				return 1;
			}
		}
	}
	if (event == MG_EV_READ && c->recv.len > VPN_LIMIT + 8192) {
		struct mg_http_message m;
		if (mg_http_parse((char *)c->recv.buf, c->recv.len, &m) > 0 &&
		    vpn_http_is_api(m.uri)) {
			reply(c, 413, "VPN request exceeds limit");
			c->is_draining = 1;
			return 1;
		}
	}
	return 0;
}
