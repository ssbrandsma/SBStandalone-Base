#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "music_file.h"
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct sb_music_file {
	sb_storage_root root;
	pthread_t worker;
	pthread_mutex_t lock;
	pthread_cond_t wake;
	int state, cancelled, stop, exited;
	int pipe[2];
	uint64_t request, last_request;
	sb_ml_track track;
	sb_resource_message result;
	sb_ml_status status;
};

static void *open_worker(void *opaque)
{
	sb_music_file *f = opaque;
	pthread_mutex_lock(&f->lock);
	for (;;) {
		while (!f->state && !f->stop)
			pthread_cond_wait(&f->wake, &f->lock);
		if (f->stop)
			break;
		if (f->state != 1) {
			pthread_cond_wait(&f->wake, &f->lock);
			continue;
		}
		f->state = 2;
		pthread_mutex_unlock(&f->lock);
		int fd = -1;
		sb_ml_status status = SB_ML_IO;
		sb_music_media media = {
			f->track.identity.source, f->track.media_generation, 1};
		if (sb_storage_open_relative(&f->root,
					     &media,
					     f->track.path,
					     0,
					     NULL,
					     NULL,
					     &fd) == SB_STORAGE_OK) {
			struct stat st;
			if (!fstat(fd, &st) && S_ISREG(st.st_mode) &&
			    st.st_nlink && st.st_size >= 0 &&
			    (uint64_t)st.st_size == f->track.file_size &&
			    st.st_mtime == f->track.mtime &&
			    sb_music_resource_entropy(
				    f->result.resource.token)) {
				f->result.device = (uint64_t)st.st_dev;
				f->result.inode = (uint64_t)st.st_ino;
				f->result.mtime_seconds = (uint64_t)st.st_mtime;
				f->result.mtime_nanoseconds =
					(uint64_t)st.st_mtim.tv_nsec;
				status = SB_ML_OK;
			}
		}
		pthread_mutex_lock(&f->lock);
		if (f->cancelled || f->stop || status != SB_ML_OK) {
			if (fd >= 0)
				close(fd);
			f->result.fd = -1;
		} else
			f->result.fd = fd;
		f->status = status;
		f->state = f->cancelled ? 0 : 3;
		if (f->state == 3) {
			unsigned char byte = 1;
			ssize_t n = write(f->pipe[1], &byte, 1);
			(void)n;
		}
	}
	if (f->result.fd >= 0) {
		close(f->result.fd);
		f->result.fd = -1;
	}
	f->exited = 1;
	pthread_mutex_unlock(&f->lock);
	return NULL;
}

sb_music_file *sb_music_file_start(int enabled, const sb_storage_root *root)
{
	if (!enabled || !root || root->fd < 0 || !root->media.online)
		return NULL;
	sb_music_file *f = calloc(1, sizeof *f);
	if (!f)
		return NULL;
	f->root = *root;
	f->result.fd = -1;
	f->root.fd = fcntl(root->fd, F_DUPFD, 3);
	if (f->root.fd < 0) {
		free(f);
		return NULL;
	}
	if (fcntl(f->root.fd, F_SETFD, FD_CLOEXEC) < 0)
		goto fail_fd;
	if (pipe(f->pipe))
		goto fail_fd;
	for (unsigned n = 0; n < 2; n++)
		if (fcntl(f->pipe[n], F_SETFD, FD_CLOEXEC) < 0 ||
		    fcntl(f->pipe[n], F_SETFL, O_NONBLOCK) < 0)
			goto fail_pipe;
	if (pthread_mutex_init(&f->lock, NULL))
		goto fail_pipe;
	if (pthread_cond_init(&f->wake, NULL))
		goto fail_lock;
	pthread_attr_t attr;
	if (pthread_attr_init(&attr))
		goto fail_cond;
	int error = pthread_attr_setstacksize(&attr, 256 * 1024);
	if (!error)
		error = pthread_create(&f->worker, &attr, open_worker, f);
	pthread_attr_destroy(&attr);
	if (!error)
		return f;
fail_cond:
	pthread_cond_destroy(&f->wake);
fail_lock:
	pthread_mutex_destroy(&f->lock);
fail_pipe:
	close(f->pipe[0]);
	close(f->pipe[1]);
fail_fd:
	close(f->root.fd);
	free(f);
	return NULL;
}

int sb_music_file_fd(const sb_music_file *f)
{
	return f ? f->pipe[0] : -1;
}

sb_ml_status sb_music_file_submit(sb_music_file *f,
				  uint64_t request,
				  const sb_ml_track *track,
				  uint64_t owner,
				  uint64_t epoch,
				  uint64_t expires)
{
	if (!f || !track || !request || !owner || !epoch || !expires ||
	    !sb_music_library_track_valid(track) ||
	    (strcmp(track->codec, "mp3") && strcmp(track->codec, "flac")) ||
	    !sb_music_source_equal(&f->root.media.source,
				   &track->identity.source) ||
	    f->root.media.generation != track->media_generation)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&f->lock))
		return SB_ML_BUSY;
	sb_ml_status status = SB_ML_BUSY;
	if (!f->stop && !f->state && request > f->last_request) {
		f->request = f->last_request = request;
		f->track = *track;
		memset(&f->result, 0, sizeof f->result);
		f->result.fd = -1;
		f->result.operation = SB_RESOURCE_REGISTER;
		f->result.codec = !strcmp(track->codec, "mp3") ? 'm' : 'f';
		f->result.resource.kind = SB_RESOURCE_AUDIO;
		f->result.resource.media = f->root.media;
		f->result.resource.owner_generation = owner;
		f->result.resource.stream_epoch = epoch;
		f->result.resource.expires_ms = expires;
		f->result.resource.size = track->file_size;
		f->result.resource.methods = SB_RESOURCE_GET | SB_RESOURCE_HEAD;
		f->cancelled = 0;
		f->state = 1;
		pthread_cond_signal(&f->wake);
		status = SB_ML_OK;
	}
	pthread_mutex_unlock(&f->lock);
	return status;
}

sb_ml_status sb_music_file_result(sb_music_file *f,
				  uint64_t request,
				  sb_resource_message *out)
{
	if (!f || !out)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&f->lock))
		return SB_ML_BUSY;
	sb_ml_status status = SB_ML_BUSY;
	if (request != f->request)
		status = SB_ML_STALE;
	else if (f->state == 3) {
		unsigned char byte;
		ssize_t n = read(f->pipe[0], &byte, 1);
		(void)n;
		status = f->status;
		*out = f->result;
		f->result.fd = -1;
		f->state = 0;
	}
	pthread_mutex_unlock(&f->lock);
	return status;
}

sb_ml_status sb_music_file_cancel(sb_music_file *f, uint64_t request)
{
	if (!f)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&f->lock))
		return SB_ML_BUSY;
	if (f->request == request) {
		if (f->state == 2)
			f->cancelled = 1;
		else {
			if (f->state == 3) {
				unsigned char byte;
				ssize_t n = read(f->pipe[0], &byte, 1);
				(void)n;
			}
			if (f->result.fd >= 0)
				close(f->result.fd);
			f->result.fd = -1;
			f->state = 0;
		}
	}
	pthread_mutex_unlock(&f->lock);
	return SB_ML_OK;
}

sb_ml_status sb_music_file_stop(sb_music_file *f)
{
	if (!f)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&f->lock))
		return SB_ML_BUSY;
	f->stop = 1;
	pthread_cond_signal(&f->wake);
	pthread_mutex_unlock(&f->lock);
	return SB_ML_OK;
}

sb_ml_status sb_music_file_destroy(sb_music_file *f)
{
	if (!f)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&f->lock))
		return SB_ML_BUSY;
	int done = f->exited;
	pthread_mutex_unlock(&f->lock);
	if (!done)
		return SB_ML_BUSY;
	pthread_join(f->worker, NULL);
	close(f->root.fd);
	close(f->pipe[0]);
	close(f->pipe[1]);
	pthread_cond_destroy(&f->wake);
	pthread_mutex_destroy(&f->lock);
	free(f);
	return SB_ML_OK;
}
