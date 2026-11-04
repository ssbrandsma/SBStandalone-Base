#define _POSIX_C_SOURCE 200809L
#include "music_resource_ipc.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int sb_resource_adopt(int socket)
{
	struct sockaddr_storage address = {0};
	socklen_t length = sizeof address, type_length = sizeof(int);
	int type, flags;
	if (getpeername(socket, (struct sockaddr *)&address, &length) ||
	    address.ss_family != AF_UNIX ||
	    getsockopt(socket, SOL_SOCKET, SO_TYPE, &type, &type_length) ||
	    type != SOCK_SEQPACKET)
		return 0;
	flags = fcntl(socket, F_GETFL);
	return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0 &&
	       fcntl(socket, F_SETFD, FD_CLOEXEC) == 0;
}

int sb_music_resource_entropy(unsigned char token[SB_MUSIC_TOKEN_BYTES])
{
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	size_t used = 0;
	while (used < SB_MUSIC_TOKEN_BYTES) {
		ssize_t n = read(fd, token + used, SB_MUSIC_TOKEN_BYTES - used);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		used += (size_t)n;
	}
	close(fd);
	return used == SB_MUSIC_TOKEN_BYTES;
}

static void put(unsigned char *p, uint64_t n)
{
	for (unsigned i = 0; i < 8; i++)
		p[i] = (unsigned char)(n >> (56 - i * 8));
}

static uint64_t get(const unsigned char *p)
{
	uint64_t n = 0;
	for (unsigned i = 0; i < 8; i++)
		n = (n << 8) | p[i];
	return n;
}

int sb_resource_pair(int pair[2])
{
	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair))
		return 0;
	for (unsigned i = 0; i < 2; i++) {
		int flags = fcntl(pair[i], F_GETFL);
		if (flags < 0 ||
		    fcntl(pair[i], F_SETFL, flags | O_NONBLOCK) < 0 ||
		    fcntl(pair[i], F_SETFD, FD_CLOEXEC) < 0) {
			close(pair[0]);
			close(pair[1]);
			return 0;
		}
	}
	return 1;
}

int sb_resource_send(int socket, const sb_resource_message *m)
{
	if (!m || !m->sequence || m->operation < SB_RESOURCE_REGISTER ||
	    m->operation > SB_RESOURCE_ACTIVATE ||
	    ((m->operation == SB_RESOURCE_REGISTER ||
	      m->operation == SB_RESOURCE_RESERVE) != (m->fd >= 0)))
		return -1;
	unsigned char wire[SB_RESOURCE_WIRE] = {0};
	memcpy(wire, "SBM4", 4);
	wire[4] = 2;
	wire[5] = (unsigned char)m->operation;
	wire[6] = (unsigned char)m->codec;
	wire[7] = (unsigned char)(m->operation == SB_RESOURCE_ACK
					  ? m->status
					  : m->resource.methods);
	put(wire + 8, m->sequence);
	memcpy(wire + 16, m->resource.token, 32);
	memcpy(wire + 48, m->resource.media.source.bytes, 16);
	put(wire + 64, m->resource.media.generation);
	put(wire + 72, m->resource.owner_generation);
	put(wire + 80, m->resource.stream_epoch);
	put(wire + 88, m->resource.expires_ms);
	put(wire + 96, m->resource.size);
	put(wire + 104, m->device);
	put(wire + 112, m->inode);
	put(wire + 120, m->mtime_seconds);
	put(wire + 128, m->mtime_nanoseconds);
	struct iovec io = {wire, sizeof wire};

	union {
		struct cmsghdr align;
		unsigned char data[CMSG_SPACE(sizeof(int))];
	} control;

	memset(&control, 0, sizeof control);
	struct msghdr msg = {0};
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	if (m->fd >= 0) {
		msg.msg_control = control.data;
		msg.msg_controllen = sizeof control.data;
		struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
		c->cmsg_level = SOL_SOCKET;
		c->cmsg_type = SCM_RIGHTS;
		c->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(c), &m->fd, sizeof(int));
	}
	ssize_t n = sendmsg(socket, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (n < 0 &&
	    (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		return 0;
	return n == (ssize_t)sizeof wire ? 1 : -1;
}

int sb_resource_receive(int socket, sb_resource_message *out)
{
	unsigned char wire[SB_RESOURCE_WIRE];

	union {
		struct cmsghdr align;
		unsigned char data[CMSG_SPACE(16 * sizeof(int))];
	} control;
	struct iovec io = {wire, sizeof wire};
	struct msghdr msg = {0};
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = control.data;
	msg.msg_controllen = sizeof control.data;
	ssize_t n = recvmsg(socket, &msg, MSG_DONTWAIT);
	if (n < 0 &&
	    (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		return 0;
	if (n < 0)
		return -1;
	int fd = -1, count = 0, invalid = 0;
	for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c;
	     c = CMSG_NXTHDR(&msg, c)) {
		if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS ||
		    c->cmsg_len < CMSG_LEN(0)) {
			invalid = 1;
			continue;
		}
		size_t bytes = c->cmsg_len - CMSG_LEN(0);
		if (bytes % sizeof(int))
			invalid = 1;
		for (size_t i = 0; i + sizeof(int) <= bytes; i += sizeof(int)) {
			int received;
			memcpy(&received,
			       (unsigned char *)CMSG_DATA(c) + i,
			       sizeof received);
			if (!count)
				fd = received;
			else
				close(received);
			count++;
		}
	}
	if (n != SB_RESOURCE_WIRE ||
	    (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) || invalid ||
	    memcmp(wire, "SBM4", 4) || wire[4] != 2 || !get(wire + 8) ||
	    wire[5] < SB_RESOURCE_REGISTER || wire[5] > SB_RESOURCE_ACTIVATE ||
	    count != (wire[5] == SB_RESOURCE_REGISTER ||
				      wire[5] == SB_RESOURCE_RESERVE
			      ? 1
			      : 0))
		goto fail;
	if (fd >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
		goto fail;
	sb_resource_message m = {0};
	m.fd = fd;
	m.operation = wire[5];
	m.codec = wire[6];
	m.sequence = get(wire + 8);
	m.status = wire[7];
	m.resource.methods = wire[7];
	memcpy(m.resource.token, wire + 16, 32);
	memcpy(m.resource.media.source.bytes, wire + 48, 16);
	m.resource.media.generation = get(wire + 64);
	m.resource.media.online = 1;
	m.resource.owner_generation = get(wire + 72);
	m.resource.stream_epoch = get(wire + 80);
	m.resource.expires_ms = get(wire + 88);
	m.resource.size = get(wire + 96);
	m.device = get(wire + 104);
	m.inode = get(wire + 112);
	m.mtime_seconds = get(wire + 120);
	m.mtime_nanoseconds = get(wire + 128);
	m.resource.kind = SB_RESOURCE_AUDIO;
	if (m.codec == 'm')
		strcpy(m.resource.mime, "audio/mpeg");
	else if (m.codec == 'f')
		strcpy(m.resource.mime, "audio/flac");
	if ((m.operation == SB_RESOURCE_REGISTER ||
	     m.operation == SB_RESOURCE_RESERVE) &&
	    (!m.resource.mime[0] || !m.resource.media.generation ||
	     !m.resource.owner_generation || !m.resource.stream_epoch ||
	     !m.resource.expires_ms || !m.resource.methods ||
	     (m.resource.methods & ~(SB_RESOURCE_GET | SB_RESOURCE_HEAD))))
		goto fail;
	if (m.operation == SB_RESOURCE_REGISTER ||
	    m.operation == SB_RESOURCE_RESERVE) {
		if (!m.inode || m.mtime_nanoseconds >= 1000000000)
			goto fail;
		unsigned token = 0, source = 0;
		for (unsigned i = 0; i < 32; i++)
			token |= m.resource.token[i];
		for (unsigned i = 0; i < 16; i++)
			source |= m.resource.media.source.bytes[i];
		if (!token || !source)
			goto fail;
	}
	if (!out)
		goto fail;
	*out = m;
	return 1;
fail:
	if (fd >= 0)
		close(fd);
	return -1;
}
