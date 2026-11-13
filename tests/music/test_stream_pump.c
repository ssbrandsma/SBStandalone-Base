#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "music_stream_http.h"
#include "music_stream.h"
#include "../../common/music_http.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

static void rest(void)
{
	struct timespec t = {0, 1000000};
	nanosleep(&t, NULL);
}

/* Drive the pump until the send buffer holds a full HTTP response head. */
static void until_head(struct mg_connection *c, const char *want)
{
	for (unsigned n = 0; n < 2000; n++) {
		sb_web_music_tick();
		sb_web_music_event(c, MG_EV_POLL, NULL);
		if (c->send.len &&
		    memmem(c->send.buf, c->send.len, want, strlen(want)))
			return;
		rest();
	}
	assert(!"response head deadline");
}

int main(void)
{
	int pair[2];
	assert(sb_resource_pair(pair));
	assert(sb_web_music_init(pair[1]));
	assert(sb_web_music_enabled());

	char path[] = "/tmp/sb-music-pump-XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	/* Multi-chunk body: the pump must stream past one buffer, and the
	 * stream must survive the 50-poll pause window below without
	 * reaching natural completion (50 * 16 KiB = 800 KiB). */
	static unsigned char body[2 * 1024 * 1024];
	for (unsigned i = 0; i < sizeof body; i++)
		body[i] = (unsigned char)(i * 31 + 7);
	assert(pwrite(fd, body, sizeof body, 0) == (ssize_t)sizeof body);

	const uint64_t start_ms = 100000;
	const uint64_t ttl_ms = 300000;
	sb_web_music_test_now(start_ms, 1);

	sb_resource_message m = {0};
	m.operation = SB_RESOURCE_REGISTER;
	m.sequence = 1;
	m.codec = 'm';
	m.fd = fd;
	m.resource.token[0] = 42;
	m.resource.token[31] = 9;
	m.resource.media.source.bytes[0] = 1;
	m.resource.media.generation = 3;
	m.resource.owner_generation = 5;
	m.resource.stream_epoch = 8;
	m.resource.expires_ms = start_ms + ttl_ms;
	m.resource.size = sizeof body;
	struct stat version;
	assert(!fstat(fd, &version));
	m.device = (uint64_t)version.st_dev;
	m.inode = (uint64_t)version.st_ino;
	m.mtime_seconds = (uint64_t)version.st_mtime;
	m.mtime_nanoseconds = (uint64_t)version.st_mtim.tv_nsec;
	m.resource.methods = SB_RESOURCE_GET | SB_RESOURCE_HEAD;
	assert(sb_resource_send(pair[0], &m) == 1);
	/* Drain the ACK. */
	for (unsigned n = 0; n < 1000; n++) {
		sb_resource_message ack;
		sb_web_music_tick();
		int result = sb_resource_receive(pair[0], &ack);
		assert(result >= 0);
		if (result) {
			assert(ack.operation == SB_RESOURCE_ACK &&
			       ack.sequence == 1 && ack.status == 0);
			break;
		}
		rest();
	}

	char hex[65];
	sb_music_token_encode(m.resource.token, hex);
	char uri[128];
	snprintf(uri, sizeof uri, "/music/stream/%s", hex);
	struct mg_connection c;
	memset(&c, 0, sizeof c);
	struct mg_http_message msg;
	memset(&msg, 0, sizeof msg);
	msg.method = mg_str("GET");
	msg.uri = mg_str(uri);
	assert(sb_web_music_event(&c, MG_EV_HTTP_HDRS, &msg) == 1);
	assert(!c.is_draining);
	until_head(&c, "200 OK");

	/* Steady flow: drain like a consuming decoder until real file
	 * bytes arrive. The drain wipes the headers, so anchor on the
	 * known binary body prefix (non-ASCII: cannot match headers). */
	size_t body_seen = 0;
	for (unsigned n = 0; n < 2000 && !body_seen; n++) {
		sb_web_music_tick();
		sb_web_music_event(&c, MG_EV_POLL, NULL);
		if (c.send.len >= 64 &&
		    memmem(c.send.buf, c.send.len, body, 64))
			body_seen = 1;
		c.send.len = 0; /* decoder consumed everything */
		rest();
	}
	assert(body_seen);

	/* WP4a.6 F1: a paused decoder stops draining the socket without
	 * dying. Freezing the send buffer past the old 15 s tripwire must
	 * not close the stream; the TTL still bounds it. */
	c.send.len = 128; /* partial chunk queued, decoder holding */
	sb_web_music_test_now(start_ms + 16000, 1);
	for (unsigned n = 0; n < 50; n++) {
		sb_web_music_tick();
		sb_web_music_event(&c, MG_EV_POLL, NULL);
		rest();
	}
	assert(!c.is_closing && !c.is_draining);

	/* Resume: the decoder drains again and the stream continues. */
	c.send.len = 0;
	sb_web_music_tick();
	sb_web_music_event(&c, MG_EV_POLL, NULL);
	assert(!c.is_closing && !c.is_draining);

	/* Expiry still retires the stream. */
	sb_web_music_test_now(start_ms + ttl_ms + 1, 1);
	sb_web_music_tick();
	sb_web_music_event(&c, MG_EV_POLL, NULL);
	assert(c.is_closing);

	sb_web_music_event(&c, MG_EV_CLOSE, NULL);
	close(fd);
	close(pair[0]);
	assert(!unlink(path));
	puts("Pump TTL deadline: pause survival, resume and expiry passed");
	return 0;
}
