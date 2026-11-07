#define _POSIX_C_SOURCE 200809L
#include "music_resource_ipc.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void pause_ms(unsigned ms)
{
	struct timespec t = {0, (long)ms * 1000000L};
	nanosleep(&t, NULL);
}

static void happy_path(void)
{
	pid_t child = fork();
	assert(child >= 0);
	if (!child) {
		int conn = sb_resource_rendezvous_connect(5000);
		assert(conn >= 0);
		int pair[2];
		assert(sb_resource_pair(pair));
		assert(sb_resource_push_fd(conn, pair[1]));
		close(pair[1]);
		close(conn);
		sb_resource_message out = {0};
		out.operation = SB_RESOURCE_RESET;
		out.sequence = 7;
		out.fd = -1;
		assert(sb_resource_send(pair[0], &out) == 1);
		sb_resource_message ack = {0};
		for (unsigned i = 0; i < 100; i++) {
			int got = sb_resource_receive(pair[0], &ack);
			if (got)
				break;
			pause_ms(10);
		}
		assert(ack.operation == SB_RESOURCE_ACK && ack.sequence == 7);
		close(pair[0]);
		_exit(0);
	}
	int listener = sb_resource_rendezvous_listen();
	assert(listener >= 0);
	int conn = sb_resource_rendezvous_accept(listener, 5000);
	assert(conn >= 0);
	int fd = sb_resource_take_fd(conn, 2000);
	assert(fd >= 0);
	assert(sb_resource_adopt(fd));
	sb_resource_message in = {0};
	for (unsigned i = 0; i < 100; i++) {
		int got = sb_resource_receive(fd, &in);
		if (got)
			break;
		pause_ms(10);
	}
	assert(in.operation == SB_RESOURCE_RESET && in.sequence == 7);
	sb_resource_message ack = {0};
	ack.operation = SB_RESOURCE_ACK;
	ack.sequence = 7;
	ack.fd = -1;
	assert(sb_resource_send(fd, &ack) == 1);
	close(fd);
	close(conn);
	close(listener);
	sb_resource_rendezvous_cleanup();
	int status = 0;
	assert(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
	       !WEXITSTATUS(status));
}

static void duplicate_refused(void)
{
	int first = sb_resource_rendezvous_listen();
	assert(first >= 0);
	errno = 0;
	assert(sb_resource_rendezvous_listen() < 0 &&
	       errno == EADDRINUSE);
	close(first);
	sb_resource_rendezvous_cleanup();
}

static void stale_replaced(void)
{
	int first = sb_resource_rendezvous_listen();
	assert(first >= 0);
	close(first); /* no cleanup: stale socket left behind */
	int second = sb_resource_rendezvous_listen();
	assert(second >= 0);
	close(second);
	sb_resource_rendezvous_cleanup();
}

static void timeouts(void)
{
	uint64_t start = 0;
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	start = (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
	assert(sb_resource_rendezvous_connect(300) < 0);
	clock_gettime(CLOCK_MONOTONIC, &t);
	uint64_t elapsed =
		(uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000 - start;
	assert(elapsed >= 300 && elapsed < 5000);
	int listener = sb_resource_rendezvous_listen();
	assert(listener >= 0);
	assert(sb_resource_rendezvous_accept(listener, 200) < 0);
	int conn = sb_resource_rendezvous_connect(2000);
	assert(conn >= 0);
	int peer = sb_resource_rendezvous_accept(listener, 2000);
	assert(peer >= 0);
	assert(sb_resource_take_fd(peer, 200) < 0);
	close(conn);
	close(peer);
	close(listener);
	sb_resource_rendezvous_cleanup();
}

static void loose_directory_refused(void)
{
	char path[256];
	const char *dir = getenv("SB_MUSIC_IPC_DIR");
	assert(dir && strlen(dir) < 200);
	snprintf(path, sizeof path, "%s/music.sock", dir);
	unlink(path);
	assert(chmod(dir, 0755) == 0);
	assert(sb_resource_rendezvous_listen() < 0);
	assert(chmod(getenv("SB_MUSIC_IPC_DIR"), 0700) == 0);
}

int main(void)
{
	char template[] = "/tmp/sb-ipc-test-XXXXXX";
	assert(mkdtemp(template));
	char dir[256];
	snprintf(dir, sizeof dir, "%s/ipc", template);
	assert(strlen(dir) < 200);
	assert(setenv("SB_MUSIC_IPC_DIR", dir, 1) == 0);
	happy_path();
	duplicate_refused();
	stale_replaced();
	timeouts();
	loose_directory_refused();
	char path[256];
	snprintf(path, sizeof path, "%s/music.sock", dir);
	unlink(path);
	rmdir(dir);
	rmdir(template);
	puts("resource rendezvous listen/accept/connect/fd-passing/refusal tests passed");
	return 0;
}
