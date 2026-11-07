#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "music_resource_ipc.h"
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
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

static uint64_t rendezvous_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

/* Qualification override for unprivileged host tests; production always
 * uses the compiled default. Mirrors the SBWEBSERVER_CONFIG_DIR precedent. */
static const char *rendezvous_dir(void)
{
	const char *e = getenv("SB_MUSIC_IPC_DIR");
	return e && *e ? e : SB_RESOURCE_RENDEZVOUS_DIR;
}

static int rendezvous_path(char out[108])
{
	if (snprintf(out,
		     108,
		     "%s/music.sock",
		     rendezvous_dir()) >= 108)
		return 0;
	return 1;
}

static int rendezvous_address(struct sockaddr_un *address)
{
	char path[108];
	memset(address, 0, sizeof *address);
	address->sun_family = AF_UNIX;
	if (!rendezvous_path(path) ||
	    strlen(path) >= sizeof address->sun_path)
		return 0;
	strcpy(address->sun_path, path);
	return 1;
}

static int rendezvous_directory(void)
{
	const char *dir = rendezvous_dir();
	struct stat st;
	if (mkdir(dir, 0700) && errno != EEXIST)
		return 0;
	if (stat(dir, &st) || !S_ISDIR(st.st_mode) ||
	    st.st_uid != geteuid() || (st.st_mode & 0077))
		return 0;
	return 1;
}

int sb_resource_rendezvous_listen(void)
{
	struct sockaddr_un address;
	if (!rendezvous_address(&address) || !rendezvous_directory())
		return -1;
	/* A live listener means another music webserver; refuse duplicates.
	 * A stale socket from a dead listener is unlinked and replaced. */
	int probe = socket(AF_UNIX, SOCK_STREAM, 0);
	if (probe < 0 || fcntl(probe, F_SETFD, FD_CLOEXEC) < 0) {
		if (probe >= 0)
			close(probe);
		return -1;
	}
	if (!connect(probe,
		     (struct sockaddr *)&address,
		     (socklen_t)sizeof address)) {
		close(probe);
		errno = EADDRINUSE;
		return -1;
	}
	int error = errno;
	close(probe);
	if (error != ENOENT && error != ECONNREFUSED)
		return -1;
	char stale[108];
	if (!rendezvous_path(stale))
		return -1;
	unlink(stale);
	int listener = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listener < 0 || fcntl(listener, F_SETFD, FD_CLOEXEC) < 0) {
		if (listener >= 0)
			close(listener);
		return -1;
	}
	if (bind(listener,
		 (struct sockaddr *)&address,
		 (socklen_t)sizeof address) ||
	    listen(listener, 1)) {
		close(listener);
		return -1;
	}
	return listener;
}

int sb_resource_rendezvous_accept(int listen_fd, unsigned timeout_ms)
{
	if (listen_fd < 0)
		return -1;
	uint64_t until = rendezvous_ms() + timeout_ms;
	for (;;) {
		uint64_t now = rendezvous_ms();
		if (now >= until)
			return -1;
		unsigned wait = until - now > 1000 ? 1000 :
							 (unsigned)(until - now);
		struct pollfd p = {listen_fd, POLLIN, 0};
		int n = poll(&p, 1, (int)wait);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			continue;
		int conn = accept(listen_fd, NULL, NULL);
		if (conn < 0 && errno == EINTR)
			continue;
		if (conn < 0 || fcntl(conn, F_SETFD, FD_CLOEXEC) < 0) {
			if (conn >= 0)
				close(conn);
			return -1;
		}
		struct ucred peer;
		socklen_t length = sizeof peer;
		if (getsockopt(conn,
			       SOL_SOCKET,
			       SO_PEERCRED,
			       &peer,
			       &length) ||
		    length != sizeof peer || peer.uid != geteuid()) {
			close(conn);
			continue;
		}
		return conn;
	}
}

int sb_resource_rendezvous_connect(unsigned timeout_ms)
{
	struct sockaddr_un address;
	if (!rendezvous_address(&address) || !rendezvous_directory())
		return -1;
	uint64_t until = rendezvous_ms() + timeout_ms;
	for (;;) {
		int conn = socket(AF_UNIX, SOCK_STREAM, 0);
		if (conn < 0)
			return -1;
		if (fcntl(conn, F_SETFD, FD_CLOEXEC) < 0) {
			close(conn);
			return -1;
		}
		if (!connect(conn,
			     (struct sockaddr *)&address,
			     (socklen_t)sizeof address))
			return conn;
		int error = errno;
		close(conn);
		if (error != ENOENT && error != ECONNREFUSED)
			return -1;
		if (rendezvous_ms() >= until)
			return -1;
		struct timespec pause = {0, 200000000};
		nanosleep(&pause, NULL);
	}
}

int sb_resource_push_fd(int conn, int fd)
{
	if (conn < 0 || fd < 0)
		return 0;
	unsigned char byte = 1;
	struct iovec io = {&byte, 1};
	union {
		struct cmsghdr align;
		unsigned char data[CMSG_SPACE(sizeof(int))];
	} control;
	memset(&control, 0, sizeof control);
	struct msghdr msg = {0};
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = control.data;
	msg.msg_controllen = sizeof control.data;
	struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(c), &fd, sizeof(int));
	ssize_t n;
	do {
		n = sendmsg(conn, &msg, MSG_NOSIGNAL);
	} while (n < 0 && errno == EINTR);
	return n == 1;
}

int sb_resource_take_fd(int conn, unsigned timeout_ms)
{
	if (conn < 0)
		return -1;
	struct pollfd p = {conn, POLLIN, 0};
	int n = poll(&p, 1, (int)timeout_ms);
	if (n <= 0)
		return -1;
	unsigned char byte = 0;
	struct iovec io = {&byte, 1};
	union {
		struct cmsghdr align;
		unsigned char data[CMSG_SPACE(sizeof(int))];
	} control;
	memset(&control, 0, sizeof control);
	struct msghdr msg = {0};
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = control.data;
	msg.msg_controllen = sizeof control.data;
	ssize_t got;
	do {
		got = recvmsg(conn, &msg, 0);
	} while (got < 0 && errno == EINTR);
	if (got != 1 || byte != 1 ||
	    (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)))
		return -1;
	int fd = -1, count = 0;
	for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c;
	     c = CMSG_NXTHDR(&msg, c)) {
		if (c->cmsg_level != SOL_SOCKET ||
		    c->cmsg_type != SCM_RIGHTS ||
		    c->cmsg_len != CMSG_LEN(sizeof(int)))
			return -1;
		if (!count)
			memcpy(&fd, CMSG_DATA(c), sizeof(int));
		count++;
	}
	if (count != 1 || fd < 0)
		return -1;
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

void sb_resource_rendezvous_cleanup(void)
{
	char path[108];
	if (rendezvous_path(path))
		unlink(path);
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
	int ok = n == (ssize_t)sizeof wire;
	fprintf(stderr, "MUSIC_IPC_SEND op=%u seq=%llu fd=%d result=%d\n",
		m->operation, (unsigned long long)m->sequence,
		m->fd >= 0, ok ? 1 : -errno);
	return ok ? 1 : -1;
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
	if (n < 0) {
		fprintf(stderr, "MUSIC_IPC_RECV_ERROR result=%d\n", -errno);
		return -1;
	}
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
	fprintf(stderr, "MUSIC_IPC_RECV op=%u seq=%llu fd=%d\n",
		m.operation, (unsigned long long)m.sequence, m.fd >= 0);
	return 1;
fail:
	fprintf(stderr, "MUSIC_IPC_RECV_INVALID n=%zd flags=%u fds=%d\n",
		n, (unsigned)msg.msg_flags, count);
	if (fd >= 0)
		close(fd);
	return -1;
}
