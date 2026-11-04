#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "music_stream.h"
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

typedef struct {
	sb_music_resource resource;
	uint64_t sequence;
	uint64_t device, inode, mtime_seconds, mtime_nanoseconds;
	int used, fd, prepared;
} resource;

typedef struct {
	uint64_t id, sequence, offset, size;
	uint64_t device, inode, mtime_seconds, mtime_nanoseconds;
	unsigned slot, wanted, length;
	int state, fd, cancelled, status;
	unsigned char data[SB_STREAM_BUFFER];
} read_job;

struct sb_stream_registry {
	int ipc, disconnected, stopping, exited;
	uint64_t sequence, counter;
	unsigned cursor;
	resource resources[SB_MUSIC_RESOURCE_MAX]; /* event-loop owned */
	sb_resource_message ack;
	int ack_pending;
	pthread_t worker;
	pthread_mutex_t lock;
	pthread_cond_t wake;
	read_job jobs[SB_STREAM_JOBS];
};

static void *reader(void *opaque)
{
	sb_stream_registry *s = opaque;
	pthread_mutex_lock(&s->lock);
	for (;;) {
		read_job *j = NULL;
		for (unsigned n = 0; n < SB_STREAM_JOBS; n++) {
			unsigned index = (s->cursor + n) % SB_STREAM_JOBS;
			if (s->jobs[index].state == 1) {
				j = &s->jobs[index];
				s->cursor = (index + 1) % SB_STREAM_JOBS;
				break;
			}
		}
		if (s->stopping)
			break;
		if (!j) {
			pthread_cond_wait(&s->wake, &s->lock);
			continue;
		}
		j->state = 2;
		pthread_mutex_unlock(&s->lock);
		struct stat before, after;
		int status = 200;
		ssize_t length = 0;
		if (fstat(j->fd, &before) || !S_ISREG(before.st_mode) ||
		    !before.st_nlink || before.st_size < 0 ||
		    (uint64_t)before.st_size != j->size ||
		    (uint64_t)before.st_dev != j->device ||
		    (uint64_t)before.st_ino != j->inode ||
		    (uint64_t)before.st_mtime != j->mtime_seconds ||
		    (uint64_t)before.st_mtim.tv_nsec != j->mtime_nanoseconds)
			status = 410;
		else if (j->wanted) {
			do {
				length = pread(j->fd,
					       j->data,
					       j->wanted,
					       (off_t)j->offset);
			} while (length < 0 && errno == EINTR);
			if (length <= 0)
				status = 500;
			else if (fstat(j->fd, &after) ||
				 before.st_dev != after.st_dev ||
				 before.st_ino != after.st_ino ||
				 before.st_size != after.st_size ||
				 before.st_mtime != after.st_mtime ||
				 before.st_mtim.tv_nsec !=
					 after.st_mtim.tv_nsec ||
				 before.st_ctime != after.st_ctime ||
				 !after.st_nlink)
				status = 410;
		}
		close(j->fd);
		pthread_mutex_lock(&s->lock);
		j->fd = -1;
		j->length = length > 0 ? (unsigned)length : 0;
		j->status = status;
		j->state = j->cancelled ? 0 : 3;
	}
	for (unsigned n = 0; n < SB_STREAM_JOBS; n++) {
		if (s->jobs[n].state == 1 && s->jobs[n].fd >= 0)
			close(s->jobs[n].fd);
		s->jobs[n].state = 0;
	}
	s->exited = 1;
	pthread_mutex_unlock(&s->lock);
	return NULL;
}

sb_stream_registry *sb_stream_registry_start(int enabled, int ipc)
{
	if (!enabled || ipc < 0 || !sb_resource_adopt(ipc))
		return NULL;
	sb_stream_registry *s = calloc(1, sizeof *s);
	if (!s)
		return NULL;
	s->ipc = ipc;
	for (unsigned n = 0; n < SB_MUSIC_RESOURCE_MAX; n++)
		s->resources[n].fd = -1;
	if (pthread_mutex_init(&s->lock, NULL)) {
		free(s);
		return NULL;
	}
	if (pthread_cond_init(&s->wake, NULL)) {
		pthread_mutex_destroy(&s->lock);
		free(s);
		return NULL;
	}
	pthread_attr_t attr;
	if (pthread_attr_init(&attr))
		goto fail;
	int error = pthread_attr_setstacksize(&attr, 256 * 1024);
	if (!error)
		error = pthread_create(&s->worker, &attr, reader, s);
	pthread_attr_destroy(&attr);
	if (!error)
		return s;
fail:
	pthread_cond_destroy(&s->wake);
	pthread_mutex_destroy(&s->lock);
	free(s);
	return NULL;
}

static void resource_revoke(resource *r)
{
	r->resource.revoked = 1;
	if (r->fd >= 0) {
		close(r->fd);
		r->fd = -1;
	}
}

int sb_stream_registry_tick(sb_stream_registry *s, uint64_t now)
{
	if (!s)
		return 0;
	for (unsigned n = 0; n < SB_MUSIC_RESOURCE_MAX; n++)
		if (s->resources[n].used &&
		    now >= s->resources[n].resource.expires_ms)
			resource_revoke(&s->resources[n]);
	if (s->disconnected)
		return 0;
	for (unsigned n = 0; n < SB_MUSIC_RESOURCE_MAX; n++) {
		if (s->ack_pending) {
			int sent = sb_resource_send(s->ipc, &s->ack);
			if (!sent)
				return 1;
			if (sent < 0)
				goto disconnected;
			s->ack_pending = 0;
		}
		sb_resource_message m;
		int received = sb_resource_receive(s->ipc, &m);
		if (!received)
			return 1;
		if (received < 0)
			goto disconnected;
		unsigned status = 0;
		if (m.sequence <= s->sequence) {
			if (m.fd >= 0)
				close(m.fd);
			goto disconnected;
		}
		s->sequence = m.sequence;
		if (m.operation == SB_RESOURCE_REGISTER ||
		    m.operation == SB_RESOURCE_RESERVE) {
			resource *target = NULL;
			for (unsigned i = 0; i < SB_MUSIC_RESOURCE_MAX; i++)
				if (s->resources[i].used &&
				    !memcmp(s->resources[i].resource.token,
					    m.resource.token,
					    32)) {
					status = 1;
					break; /* a token is never
						  renewed/reused */
				}
			if (!status)
				for (unsigned i = 0; i < SB_MUSIC_RESOURCE_MAX;
				     i++)
					if (!s->resources[i].used ||
					    s->resources[i].resource.revoked) {
						target = &s->resources[i];
						break;
					}
			if (!status &&
			    (!target || m.resource.size > INT64_MAX ||
			     now >= m.resource.expires_ms))
				status = 2;
			if (status)
				close(m.fd);
			else {
				if (target->used)
					resource_revoke(target);
				target->resource = m.resource;
				target->device = m.device;
				target->inode = m.inode;
				target->mtime_seconds = m.mtime_seconds;
				target->mtime_nanoseconds = m.mtime_nanoseconds;
				target->sequence = m.sequence;
				target->fd = m.fd;
				target->used = 1;
				target->prepared =
					m.operation == SB_RESOURCE_RESERVE;
			}
		} else if (m.operation == SB_RESOURCE_ACTIVATE) {
			status = 1;
			for (unsigned i = 0; i < SB_MUSIC_RESOURCE_MAX; i++) {
				resource *r = &s->resources[i];
				if (r->used && r->prepared &&
				    !r->resource.revoked &&
				    now < r->resource.expires_ms &&
				    !memcmp(r->resource.token,
					    m.resource.token,
					    32) &&
				    m.resource.owner_generation &&
				    r->resource.media.generation ==
					    m.resource.media.generation &&
				    !memcmp(r->resource.media.source.bytes,
					    m.resource.media.source.bytes,
					    16) &&
				    r->resource.stream_epoch ==
					    m.resource.stream_epoch) {
					r->resource.owner_generation =
						m.resource.owner_generation;
					r->prepared = 0;
					status = 0;
					break;
				}
			}
		} else if (m.operation == SB_RESOURCE_REVOKE ||
			   m.operation == SB_RESOURCE_RESET) {
			for (unsigned i = 0; i < SB_MUSIC_RESOURCE_MAX; i++) {
				resource *r = &s->resources[i];
				if (r->used &&
				    (m.operation == SB_RESOURCE_RESET ||
				     !memcmp(r->resource.token,
					     m.resource.token,
					     32)))
					resource_revoke(r);
			}
		} else
			goto disconnected;
		memset(&s->ack, 0, sizeof s->ack);
		s->ack.operation = SB_RESOURCE_ACK;
		s->ack.sequence = m.sequence;
		s->ack.status = status;
		s->ack.fd = -1;
		s->ack_pending = 1;
	}
	return 1;
disconnected:
	s->disconnected = 1;
	for (unsigned i = 0; i < SB_MUSIC_RESOURCE_MAX; i++)
		resource_revoke(&s->resources[i]);
	return 0;
}

int sb_stream_info(sb_stream_registry *s,
		   const unsigned char token[32],
		   unsigned method,
		   uint64_t now,
		   sb_music_resource *out)
{
	if (!s || !token)
		return 404;
	for (unsigned n = 0; n < SB_MUSIC_RESOURCE_MAX; n++) {
		resource *r = &s->resources[n];
		if (!r->used || memcmp(r->resource.token, token, 32))
			continue;
		if (r->resource.revoked || now >= r->resource.expires_ms ||
		    s->disconnected)
			return 410;
		if (r->prepared)
			return 404;
		if (!(r->resource.methods & method) ||
		    (method != SB_RESOURCE_GET && method != SB_RESOURCE_HEAD))
			return 405;
		if (out)
			*out = r->resource;
		return 200;
	}
	return 404;
}

uint64_t sb_stream_read(sb_stream_registry *s,
			const unsigned char token[32],
			uint64_t offset,
			unsigned length,
			uint64_t now)
{
	if (!s || length > SB_STREAM_BUFFER || s->counter == UINT64_MAX ||
	    s->stopping)
		return 0;
	unsigned slot;
	for (slot = 0; slot < SB_MUSIC_RESOURCE_MAX; slot++) {
		resource *r = &s->resources[slot];
		if (r->used && !r->prepared && !r->resource.revoked &&
		    now < r->resource.expires_ms &&
		    !memcmp(r->resource.token, token, 32) &&
		    offset <= r->resource.size &&
		    length <= r->resource.size - offset)
			break;
	}
	if (slot == SB_MUSIC_RESOURCE_MAX || pthread_mutex_trylock(&s->lock))
		return 0;
	uint64_t id = 0;
	for (unsigned n = 0; n < SB_STREAM_JOBS; n++)
		if (!s->jobs[n].state) {
			resource *r = &s->resources[slot];
			int fd = fcntl(r->fd, F_DUPFD, 3);
			if (fd < 0)
				break;
			if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
				close(fd);
				break;
			}
			read_job *j = &s->jobs[n];
			memset(j, 0, sizeof *j);
			j->id = id = ++s->counter;
			j->slot = slot;
			j->sequence = r->sequence;
			j->fd = fd;
			j->offset = offset;
			j->wanted = length;
			j->size = r->resource.size;
			j->device = r->device;
			j->inode = r->inode;
			j->mtime_seconds = r->mtime_seconds;
			j->mtime_nanoseconds = r->mtime_nanoseconds;
			j->state = 1;
			pthread_cond_signal(&s->wake);
			break;
		}
	pthread_mutex_unlock(&s->lock);
	return id;
}

int sb_stream_result(sb_stream_registry *s,
		     uint64_t id,
		     uint64_t now,
		     unsigned char out[SB_STREAM_BUFFER],
		     unsigned *length,
		     int *status)
{
	if (!s || !id || pthread_mutex_trylock(&s->lock))
		return 0;
	int ready = 0;
	for (unsigned n = 0; n < SB_STREAM_JOBS; n++) {
		read_job *j = &s->jobs[n];
		if (j->id != id || j->state != 3)
			continue;
		resource *r = &s->resources[j->slot];
		*status = r->sequence != j->sequence || r->resource.revoked ||
					  now >= r->resource.expires_ms
				  ? 410
				  : j->status;
		*length = *status == 200 ? j->length : 0;
		if (*length)
			memcpy(out, j->data, *length);
		j->state = 0;
		ready = 1;
		break;
	}
	pthread_mutex_unlock(&s->lock);
	return ready;
}

int sb_stream_cancel(sb_stream_registry *s, uint64_t id)
{
	if (!s || !id)
		return 1;
	/* Event caller retries cancellation when a worker owns this tiny lock.
	 */
	if (pthread_mutex_trylock(&s->lock))
		return 0;
	for (unsigned n = 0; n < SB_STREAM_JOBS; n++) {
		read_job *j = &s->jobs[n];
		if (j->id != id || !j->state)
			continue;
		if (j->state == 2)
			j->cancelled = 1;
		else {
			if (j->state == 1)
				close(j->fd);
			j->state = 0;
		}
		break;
	}
	pthread_mutex_unlock(&s->lock);
	return 1;
}

int sb_stream_registry_stop(sb_stream_registry *s)
{
	if (!s)
		return 1;
	for (unsigned n = 0; n < SB_MUSIC_RESOURCE_MAX; n++)
		resource_revoke(&s->resources[n]);
	if (pthread_mutex_trylock(&s->lock))
		return 0;
	s->stopping = 1;
	pthread_cond_signal(&s->wake);
	pthread_mutex_unlock(&s->lock);
	return 1;
}

int sb_stream_registry_destroy(sb_stream_registry *s)
{
	if (!s)
		return 1;
	if (pthread_mutex_trylock(&s->lock))
		return 0;
	int done = s->exited;
	pthread_mutex_unlock(&s->lock);
	if (!done)
		return 0;
	pthread_join(s->worker, NULL);
	close(s->ipc);
	pthread_cond_destroy(&s->wake);
	pthread_mutex_destroy(&s->lock);
	free(s);
	return 1;
}
