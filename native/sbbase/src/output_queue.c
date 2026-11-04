#define _POSIX_C_SOURCE 200809L
#include "output_queue.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

void sb_output_clear(sb_output *q)
{
	if (q) {
		free(q->data);
		memset(q, 0, sizeof *q);
	}
}

int sb_output_expired(const sb_output *q, uint64_t now)
{
	return q && q->length && now >= q->since_ms &&
	       now - q->since_ms >= SB_OUTPUT_TIMEOUT_MS;
}

int sb_output_flush(sb_output *q, int fd, uint64_t now)
{
	size_t budget = SB_OUTPUT_BUDGET;
	if (!q || fd < 0 || sb_output_expired(q, now))
		return -1;
	for (unsigned calls = 0; q->length && budget && calls < 4; calls++) {
		size_t n = q->length < budget ? q->length : budget;
		ssize_t sent = send(fd,
				    q->data + q->offset,
				    n,
				    MSG_NOSIGNAL | MSG_DONTWAIT);
		if (sent > 0) {
			q->offset += (size_t)sent;
			q->length -= (size_t)sent;
			budget -= (size_t)sent;
		} else if (sent < 0 && errno == EINTR)
			continue;
		else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;
		else
			return -1;
	}
	if (!q->length) {
		free(q->data);
		q->data = NULL;
		q->offset = 0;
		q->since_ms = 0;
		return 1;
	}
	return 0;
}

int sb_output_enqueue(
	sb_output *q, int fd, const void *data, size_t n, uint64_t now)
{
	const unsigned char *p = data;
	if (!q || fd < 0 || (!data && n) ||
	    n > SB_OUTPUT_CAPACITY - q->length || sb_output_expired(q, now))
		return 0;
	if (!n)
		return 1;
	if (!q->length) {
		size_t limit = n < SB_OUTPUT_BUDGET ? n : SB_OUTPUT_BUDGET;
		ssize_t sent = send(fd, p, limit, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (sent > 0) {
			p += sent;
			n -= (size_t)sent;
		} else if (sent == 0 || (errno != EINTR && errno != EAGAIN &&
					 errno != EWOULDBLOCK))
			return 0;
		if (!n)
			return 1;
		q->since_ms = now;
	}
	if (!q->data) {
		q->data = malloc(SB_OUTPUT_CAPACITY);
		if (!q->data)
			return 0;
	}
	if (q->offset + q->length + n > SB_OUTPUT_CAPACITY) {
		memmove(q->data, q->data + q->offset, q->length);
		q->offset = 0;
	}
	memcpy(q->data + q->offset + q->length, p, n);
	q->length += n;
	return 1;
}

int sb_output_scope(sb_output *q, uint64_t c, uint64_t o, uint64_t e)
{
	if (!q || !c || !o || !e || q->length)
		return 0;
	q->connection = c;
	q->owner_generation = o;
	q->stream_epoch = e;
	return 1;
}

int sb_output_scoped(sb_output *q,
		     int fd,
		     uint64_t c,
		     uint64_t o,
		     uint64_t e,
		     const void *p,
		     size_t n,
		     uint64_t now)
{
	if (!q || !c || !o || !e || q->connection != c ||
	    q->owner_generation != o || q->stream_epoch != e)
		return 0;
	return sb_output_enqueue(q, fd, p, n, now);
}
