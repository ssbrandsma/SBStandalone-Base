#define _POSIX_C_SOURCE 200809L
#include "music_async.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { FREE, WAITING, RUNNING, READY };

typedef struct {
	int state, cancelled;
	uint64_t id, deadline;
	sb_ml_request request;
	sb_ml_result result;
	unsigned attempts;
} job;

struct sb_music_async {
	sb_music_library *library;
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t wake;
	int pipe[2], stop, exited;
	uint64_t counter;
	unsigned cursor, peak;
	job jobs[SB_ASYNC_SLOTS];
};

static uint64_t now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void drain(sb_music_async *a)
{
	unsigned char byte;
	ssize_t n = read(a->pipe[0], &byte, 1);
	(void)n;
}

static void notify(sb_music_async *a)
{
	unsigned char byte = 1;
	ssize_t n = write(a->pipe[1], &byte, 1);
	(void)n;
}

static void *worker(void *opaque)
{
	sb_music_async *a = opaque;
	pthread_mutex_lock(&a->lock);
	for (;;) {
		job *j = NULL;
		for (unsigned n = 0; n < SB_ASYNC_SLOTS; n++) {
			unsigned index = (a->cursor + n) % SB_ASYNC_SLOTS;
			if (a->jobs[index].state == WAITING) {
				j = &a->jobs[index];
				a->cursor = (index + 1) % SB_ASYNC_SLOTS;
				break;
			}
		}
		if (a->stop)
			break;
		if (!j) {
			pthread_cond_wait(&a->wake, &a->lock);
			continue;
		}
		j->state = RUNNING;
		sb_ml_request request = j->request;
		request.browse_token = j->id;
		uint64_t deadline = j->deadline;
		pthread_mutex_unlock(&a->lock);
		sb_ml_result result = {0};
		sb_ml_status status =
			now_ms() >= deadline
				? SB_ML_CANCELLED
				: sb_music_library_exchange_browse(
					  a->library, &request, &result);
		pthread_mutex_lock(&a->lock);
		if (j->cancelled)
			j->state = FREE;
		else if ((status == SB_ML_BUSY || status == SB_ML_CONFLICT) &&
			 now_ms() < deadline && ++j->attempts < 500)
			j->state = WAITING;
		else {
			j->result = result;
			j->result.request_id = j->id;
			j->result.operation = j->request.operation;
			j->result.status = status;
			j->state = READY;
			notify(a);
		}
		/* Guaranteed gap between browse broker calls, also after
		 * success. */
		pthread_mutex_unlock(&a->lock);
		struct timespec rest = {0, 2000000};
		nanosleep(&rest, NULL);
		pthread_mutex_lock(&a->lock);
	}
	a->exited = 1;
	notify(a);
	pthread_mutex_unlock(&a->lock);
	return NULL;
}

sb_music_async *sb_music_async_start(sb_music_library *library, int enabled)
{
	if (!enabled || !library || !sb_music_library_browse_attach(library))
		return NULL;
	sb_music_async *a = calloc(1, sizeof *a);
	if (!a) {
		sb_music_library_browse_detach(library);
		return NULL;
	}
	a->library = library;
	if (pthread_mutex_init(&a->lock, NULL)) {
		free(a);
		sb_music_library_browse_detach(library);
		return NULL;
	}
	if (pthread_cond_init(&a->wake, NULL)) {
		pthread_mutex_destroy(&a->lock);
		free(a);
		sb_music_library_browse_detach(library);
		return NULL;
	}
	if (pipe(a->pipe))
		goto fail;
	for (unsigned i = 0; i < 2; i++)
		if (fcntl(a->pipe[i], F_SETFL, O_NONBLOCK) < 0 ||
		    fcntl(a->pipe[i], F_SETFD, FD_CLOEXEC) < 0)
			goto fail_pipe;
	pthread_attr_t attr;
	if (pthread_attr_init(&attr))
		goto fail_pipe;
	int rc = pthread_attr_setstacksize(&attr, 256 * 1024);
	if (!rc)
		rc = pthread_create(&a->thread, &attr, worker, a);
	pthread_attr_destroy(&attr);
	if (rc)
		goto fail_pipe;
	return a;
fail_pipe:
	close(a->pipe[0]);
	close(a->pipe[1]);
fail:
	pthread_cond_destroy(&a->wake);
	pthread_mutex_destroy(&a->lock);
	free(a);
	sb_music_library_browse_detach(library);
	return NULL;
}

int sb_music_async_fd(const sb_music_async *a)
{
	return a ? a->pipe[0] : -1;
}

sb_ml_status sb_music_async_submit(sb_music_async *a,
				   const sb_ml_request *request,
				   uint64_t deadline,
				   uint64_t *id)
{
	uint64_t now = now_ms();
	if (!a || !request || !id || deadline <= now || deadline - now > 5000 ||
	    (request->operation != SB_ML_BROWSE &&
	     request->operation != SB_ML_DETAIL &&
	     request->operation != SB_ML_BROWSE_SUMMARY &&
	     request->operation != SB_ML_SOURCE))
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&a->lock))
		return SB_ML_BUSY;
	sb_ml_status s = SB_ML_BUSY;
	unsigned count = 0;
	for (unsigned i = 0; i < SB_ASYNC_SLOTS; i++)
		if (a->jobs[i].state)
			count++;
	if (!a->stop && a->counter < UINT64_MAX)
		for (unsigned i = 0; i < SB_ASYNC_SLOTS; i++)
			if (!a->jobs[i].state) {
				job *j = &a->jobs[i];
				memset(j, 0, sizeof *j);
				j->request = *request;
				j->id = ++a->counter;
				j->deadline = deadline;
				j->state = WAITING;
				*id = j->id;
				s = SB_ML_OK;
				if (count + 1 > a->peak)
					a->peak = count + 1;
				pthread_cond_signal(&a->wake);
				break;
			}
	pthread_mutex_unlock(&a->lock);
	return s;
}

sb_ml_status
sb_music_async_poll(sb_music_async *a, uint64_t id, sb_ml_result *out)
{
	if (!a || !id || !out)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&a->lock))
		return SB_ML_BUSY;
	sb_ml_status s = SB_ML_STALE;
	uint64_t cancel = 0;
	for (unsigned i = 0; i < SB_ASYNC_SLOTS; i++)
		if (a->jobs[i].state && a->jobs[i].id == id) {
			job *j = &a->jobs[i];
			s = SB_ML_BUSY;
			if (j->cancelled) {
				s = SB_ML_STALE;
				break;
			}
			if (j->state == READY && now_ms() < j->deadline) {
				*out = j->result;
				j->state = FREE;
				drain(a);
				s = SB_ML_OK;
			} else if (now_ms() >= j->deadline) {
				if (j->state == READY)
					drain(a);
				memset(out, 0, sizeof *out);
				out->request_id = id;
				out->status = SB_ML_CANCELLED;
				out->operation = j->request.operation;
				if (j->state == RUNNING) {
					j->cancelled = 1;
					cancel = id;
				} else
					j->state = FREE;
				s = SB_ML_OK;
			}
			break;
		}
	pthread_mutex_unlock(&a->lock);
	if (cancel)
		(void)sb_music_library_cancel_browse(a->library, cancel);
	return s;
}

sb_ml_status sb_music_async_cancel(sb_music_async *a, uint64_t id)
{
	if (!a || !id)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&a->lock))
		return SB_ML_BUSY;
	sb_ml_status s = SB_ML_STALE;
	int active = 0;
	for (unsigned i = 0; i < SB_ASYNC_SLOTS; i++)
		if (a->jobs[i].state && a->jobs[i].id == id) {
			job *j = &a->jobs[i];
			active = j->state == RUNNING;
			if (active)
				j->cancelled = 1;
			else {
				if (j->state == READY)
					drain(a);
				j->state = FREE;
			}
			s = SB_ML_OK;
			break;
		}
	pthread_mutex_unlock(&a->lock);
	if (active)
		(void)sb_music_library_cancel_browse(a->library, id);
	return s;
}

sb_ml_status sb_music_async_stop(sb_music_async *a)
{
	if (!a)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&a->lock))
		return SB_ML_BUSY;
	a->stop = 1;
	uint64_t cancel = 0;
	for (unsigned i = 0; i < SB_ASYNC_SLOTS; i++) {
		job *j = &a->jobs[i];
		if (j->state == RUNNING) {
			j->cancelled = 1;
			cancel = j->id;
		} else
			j->state = FREE;
	}
	pthread_cond_signal(&a->wake);
	pthread_mutex_unlock(&a->lock);
	if (cancel)
		(void)sb_music_library_cancel_browse(a->library, cancel);
	return SB_ML_OK;
}

sb_ml_status sb_music_async_destroy(sb_music_async *a)
{
	if (!a)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&a->lock))
		return SB_ML_BUSY;
	int safe = a->exited;
	pthread_mutex_unlock(&a->lock);
	if (!safe)
		return SB_ML_BUSY;
	pthread_join(a->thread, NULL);
	close(a->pipe[0]);
	close(a->pipe[1]);
	pthread_cond_destroy(&a->wake);
	pthread_mutex_destroy(&a->lock);
	sb_music_library_browse_detach(a->library);
	free(a);
	return SB_ML_OK;
}

unsigned sb_music_async_peak(sb_music_async *a)
{
	if (!a || pthread_mutex_trylock(&a->lock))
		return 0;
	unsigned n = a->peak;
	pthread_mutex_unlock(&a->lock);
	return n;
}
