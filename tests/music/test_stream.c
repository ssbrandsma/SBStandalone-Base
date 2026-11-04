#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "music_stream.h"
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

static void ack(sb_stream_registry *s, int peer, uint64_t seq, unsigned status)
{
	sb_resource_message m;
	for (unsigned n = 0; n < 1000; n++) {
		assert(sb_stream_registry_tick(s, 100));
		int result = sb_resource_receive(peer, &m);
		assert(result >= 0);
		if (result) {
			assert(m.operation == SB_RESOURCE_ACK &&
			       m.sequence == seq && m.status == status);
			return;
		}
		rest();
	}
	assert(!"ACK deadline");
}

static uint64_t submit(sb_stream_registry *s,
		       const unsigned char *token,
		       uint64_t offset,
		       unsigned length)
{
	for (unsigned n = 0; n < 1000; n++) {
		uint64_t id = sb_stream_read(s, token, offset, length, 100);
		if (id)
			return id;
		rest();
	}
	assert(!"read admission deadline");
	return 0;
}

static unsigned
result(sb_stream_registry *s, uint64_t id, int expected, unsigned char *data)
{
	unsigned length;
	int status;
	for (unsigned n = 0; n < 1000; n++) {
		if (sb_stream_result(s, id, 100, data, &length, &status)) {
			assert(status == expected);
			return length;
		}
		rest();
	}
	assert(!"read completion deadline");
	return 0;
}

int main(void)
{
	int pair[2];
	assert(sb_resource_pair(pair));
	assert(fcntl(pair[1], F_SETFD, 0) == 0);
	assert(!sb_stream_registry_start(0, pair[1]));
	sb_stream_registry *s = sb_stream_registry_start(1, pair[1]);
	assert(s);
	assert(fcntl(pair[1], F_GETFD) & FD_CLOEXEC);
	char path[] = "/tmp/sb-music-stream-XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	const uint64_t offset = UINT64_C(4294967296);
	assert(!ftruncate(fd, (off_t)(offset + 8)));
	assert(pwrite(fd, "encoded!", 8, (off_t)offset) == 8);
	sb_resource_message m = {0};
	m.operation = SB_RESOURCE_REGISTER;
	m.sequence = 1;
	m.codec = 'f';
	m.fd = fd;
	m.resource.token[0] = 42;
	m.resource.media.source.bytes[0] = 1;
	m.resource.media.generation = 3;
	m.resource.owner_generation = 5;
	m.resource.stream_epoch = 8;
	m.resource.expires_ms = 1000;
	m.resource.size = offset + 8;
	struct stat version;
	assert(!fstat(fd, &version));
	m.device = (uint64_t)version.st_dev;
	m.inode = (uint64_t)version.st_ino;
	m.mtime_seconds = (uint64_t)version.st_mtime;
	m.mtime_nanoseconds = (uint64_t)version.st_mtim.tv_nsec;
	m.resource.methods = SB_RESOURCE_GET | SB_RESOURCE_HEAD;
	assert(sb_resource_send(pair[0], &m) == 1);
	ack(s, pair[0], 1, 0);
	sb_music_resource info;
	assert(sb_stream_info(
		       s, m.resource.token, SB_RESOURCE_GET, 100, &info) ==
	       200);
	assert(info.size == offset + 8);
	unsigned char data[SB_STREAM_BUFFER];
	uint64_t id = submit(s, m.resource.token, offset, 8);
	assert(result(s, id, 200, data) == 8 && !memcmp(data, "encoded!", 8));
	assert(!sb_stream_read(s, m.resource.token, offset + 8, 1, 100));
	/* Revoke after admission: even a ready result cannot escape. */
	id = submit(s, m.resource.token, offset, 8);
	m.operation = SB_RESOURCE_REVOKE;
	m.sequence = 2;
	m.fd = -1;
	assert(sb_resource_send(pair[0], &m) == 1);
	ack(s, pair[0], 2, 0);
	assert(sb_stream_info(
		       s, m.resource.token, SB_RESOURCE_GET, 100, NULL) == 410);
	assert(result(s, id, 410, data) == 0);
	/* Register a fresh token; disappearance is checked on the worker. */
	m.operation = SB_RESOURCE_REGISTER;
	m.sequence = 3;
	m.fd = fd;
	m.resource.token[0] = 43;
	assert(sb_resource_send(pair[0], &m) == 1);
	ack(s, pair[0], 3, 0);
	assert(sb_stream_info(
		       s, m.resource.token, SB_RESOURCE_GET, 1000, NULL) ==
	       410);
	assert(!unlink(path));
	id = submit(s, m.resource.token, offset, 8);
	assert(result(s, id, 410, data) == 0);
	close(fd);
	close(pair[0]);
	assert(!sb_stream_registry_tick(s, 100));
	assert(sb_stream_info(
		       s, m.resource.token, SB_RESOURCE_HEAD, 100, NULL) ==
	       410);
	while (!sb_stream_registry_stop(s))
		rest();
	for (unsigned n = 0; !sb_stream_registry_destroy(s); n++) {
		assert(n < 1000);
		rest();
	}
	puts("Private descriptor IPC, 64-bit pread, revocation, removal and "
	     "shutdown passed");
	return 0;
}
