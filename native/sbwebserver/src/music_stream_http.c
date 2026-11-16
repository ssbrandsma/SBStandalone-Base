#define _POSIX_C_SOURCE 200809L
#include "music_stream_http.h"
#include "music_stream.h"
#include "../../common/music_http.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

#define HTTP_STREAMS 4u

typedef struct {
	struct mg_connection *connection;
	uint64_t job, offset, remaining, deadline;
	unsigned char token[32];
	sb_music_resource resource;
	sb_music_http_range range;
	int used, head, headers, retiring;
} stream_client;

static sb_stream_registry *registry;
static stream_client clients[HTTP_STREAMS];
static uint64_t test_now_ms;
static int test_now_set;

/* IPC expiry uses Base CLOCK_MONOTONIC, not Mongoose MONOTONIC_RAW. */
static uint64_t music_now(void)
{
	struct timespec t;
	if (test_now_set)
		return test_now_ms;
	if (clock_gettime(CLOCK_MONOTONIC, &t))
		return UINT64_MAX;
	return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void sb_web_music_test_now(uint64_t now_ms, int set)
{
	test_now_ms = now_ms;
	test_now_set = set;
}

int sb_web_music_init(int fd)
{
	if (registry || fd < 0)
		return 0;
	struct sockaddr_un address;
	socklen_t size = sizeof address;
	int type;
	socklen_t type_size = sizeof type;
	if (getpeername(fd, (struct sockaddr *)&address, &size) ||
	    address.sun_family != AF_UNIX ||
	    getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_size) ||
	    type != SOCK_SEQPACKET)
		return 0;
	registry = sb_stream_registry_start(1, fd);
	return registry != NULL;
}

int sb_web_music_enabled(void)
{
	return registry != NULL;
}
int sb_web_music_shutdown(void)
{
 if(!registry)return 1;
 test_now_set=0;
 uint64_t deadline=music_now()+5000;
 struct timespec pause={0,1000000};
 while(!sb_stream_registry_stop(registry)){if(music_now()>=deadline)return 0;nanosleep(&pause,NULL);}
 while(!sb_stream_registry_destroy(registry)){if(music_now()>=deadline)return 0;nanosleep(&pause,NULL);}
 registry=NULL;memset(clients,0,sizeof clients);return 1;
}

static void retire(stream_client *s)
{
	s->connection = NULL;
	s->retiring = 1;
	if (sb_stream_cancel(registry, s->job))
		memset(s, 0, sizeof *s);
}

void sb_web_music_tick(void)
{
	if (!registry)
		return;
	sb_stream_registry_tick(registry, music_now());
	for (unsigned i = 0; i < HTTP_STREAMS; i++)
		if (clients[i].retiring)
			retire(&clients[i]);
}

static void error(struct mg_connection *c, int status, uint64_t size)
{
	(void)size;
	mg_http_reply(c,
		      status,
		      "Cache-Control: no-store\r\nConnection: close\r\n",
		      "");
	c->is_draining = 1;
}

static void range_error(struct mg_connection *c, uint64_t size)
{
	mg_printf(c,
		  "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes "
		  "*/%" PRIu64
		  "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
		  size);
	c->is_draining = 1;
}

static int same(struct mg_str a, const char *b)
{
	return a.len == strlen(b) && !memcmp(a.buf, b, a.len);
}

static int start(struct mg_connection *c, struct mg_http_message *m)
{
	static const char prefix[] = "/music/stream/";
	if (m->uri.len < sizeof prefix - 1 ||
	    memcmp(m->uri.buf, prefix, sizeof prefix - 1))
		return 0;
	unsigned char token[32];
	if (m->query.len ||
	    !sb_music_token_decode(m->uri.buf + sizeof prefix - 1,
				   m->uri.len - (sizeof prefix - 1),
				   token)) {
		error(c, 404, 0);
		return 1;
	}
	int head = same(m->method, "HEAD");
	if (!head && !same(m->method, "GET")) {
		error(c, 405, 0);
		return 1;
	}
	struct mg_str *length = mg_http_get_header(m, "Content-Length");
	if (mg_http_get_header(m, "Transfer-Encoding") ||
	    (length && !same(*length, "0"))) {
		error(c, 400, 0);
		return 1;
	}
	sb_music_resource resource;
	int status = sb_stream_info(registry,
				    token,
				    head ? SB_RESOURCE_HEAD : SB_RESOURCE_GET,
				    music_now(),
				    &resource);
	if (status != 200) {
		error(c, status, 0);
		return 1;
	}
	char range[64];
	const char *selected = NULL;
	unsigned ranges = 0;
	for (unsigned n = 0; n < MG_MAX_HTTP_HEADERS; n++)
		if (mg_strcasecmp(m->headers[n].name, mg_str("Range")) == 0)
			ranges++;
	struct mg_str *header = mg_http_get_header(m, "Range");
	if (ranges > 1 || (header && header->len >= sizeof range)) {
		range_error(c, resource.size);
		return 1;
	}
	if (header) {
		memcpy(range, header->buf, header->len);
		range[header->len] = 0;
		selected = range;
	}
	if(resource.streaming&&header){range_error(c,resource.size);return 1;}
	sb_music_http_range r = sb_music_http_select(resource.size, selected);
	if (r.status == 416) {
		range_error(c, resource.size);
		return 1;
	}
	stream_client *s = NULL;
	for (unsigned n = 0; n < HTTP_STREAMS; n++)
		if (!clients[n].used) {
			s = &clients[n];
			break;
		}
	if (!s) {
		error(c, 503, 0);
		return 1;
	}
	memset(s, 0, sizeof *s);
	s->used = 1;
	s->connection = c;
	memcpy(s->token, token, 32);
	s->resource = resource;
	s->range = r;
	s->head = head;
	s->offset = r.first;
	s->remaining = r.length;
	/* WP4a.6 F1: a paused decoder holds the socket without dying. Bound
	 * the stream by the resource TTL, not a fixed 15 s tripwire; the
	 * registry expiry check in pump() still retires it. */
	s->deadline = s->resource.expires_ms;
	return 1;
}

static void pump(stream_client *s)
{
	struct mg_connection *c = s->connection;
	uint64_t now = music_now();
	if (sb_stream_info(registry,
			   s->token,
			   s->head ? SB_RESOURCE_HEAD : SB_RESOURCE_GET,
			   now,
			   NULL) != 200) {
		if (!s->headers)
			error(c, 410, 0);
		else
			c->is_closing = 1;
		retire(s);
		return;
	}
	if (now >= s->deadline) {
		c->is_closing = 1;
		retire(s);
		return;
	}
	if (s->job) {
		unsigned char data[SB_STREAM_BUFFER];
		unsigned length;
		int status;
		if (!sb_stream_result(
			    registry, s->job, now, data, &length, &status))
			return;
		s->job = 0;
		if (status != 200) {
			if (!s->headers)
				error(c, status, 0);
			else
				c->is_closing = 1;
			retire(s);
			return;
		}
		if (!s->headers) {
			mg_printf(c,
				  "HTTP/1.1 %d %s\r\nContent-Type: "
				  "%s\r\nContent-Length: %" PRIu64
				  "\r\nAccept-Ranges: %s\r\nCache-Control: "
				  "no-store\r\nConnection: close\r\n",
				  s->range.status,
				  s->range.status == 206 ? "Partial Content"
							 : "OK",
				  s->resource.mime,
				  s->range.length,s->resource.streaming?"none":"bytes");
			if (s->range.status == 206)
				mg_printf(c,
					  "Content-Range: bytes %" PRIu64
					  "-%" PRIu64 "/%" PRIu64 "\r\n",
					  s->range.first,
					  s->range.first + s->range.length - 1,
					  s->resource.size);
			mg_send(c, "\r\n", 2);
			s->headers = 1;
		} else if (length) {
			if (length > s->remaining ||
			    !mg_send(c, data, length)) {
				c->is_closing = 1;
				retire(s);
				return;
			}
			s->offset += length;
			s->remaining -= length;
		}
	}
	if (s->headers && (s->head || !s->remaining)) {
		c->is_draining = 1;
		retire(s);
		return;
	}
	if (!s->job && c->send.len <= SB_STREAM_BUFFER)
		s->job = sb_stream_read(registry,
					s->token,
					s->offset,
					!s->headers ? 0
					: s->remaining > SB_STREAM_BUFFER
						? SB_STREAM_BUFFER
						: (unsigned)s->remaining,
					now);
}

int sb_web_music_event(struct mg_connection *c, int event, void *data)
{
	if (!registry)
		return 0;
	stream_client *s = NULL;
	for (unsigned n = 0; n < HTTP_STREAMS; n++)
		if (clients[n].used && clients[n].connection == c) {
			s = &clients[n];
			break;
		}
	if (event == MG_EV_CLOSE) {
		if (s)
			retire(s);
		return 0;
	}
	if (s) {
		if (event == MG_EV_POLL)
			pump(s);
		/* Pipelined requests cannot select a second resource on this
		 * stream. */
		return 1;
	}
	if (event == MG_EV_HTTP_HDRS)
		return start(c, data);
	if (event == MG_EV_HTTP_MSG) {
		struct mg_http_message *m = data;
		if (m->uri.len >= 14 &&
		    !memcmp(m->uri.buf, "/music/stream/", 14))
			return 1;
	}
	return 0;
}
