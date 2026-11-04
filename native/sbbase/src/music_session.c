/* Explicit manual session supervisor; never installed/enabled at boot. */
#define _POSIX_C_SOURCE 200809L
#include "../../common/music_resource_ipc.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static volatile sig_atomic_t stopping;

static void stop_session(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}

static int executable(const char *path)
{
	struct stat st;
	return path && path[0] == '/' && !lstat(path, &st) &&
	       S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
	       !(st.st_mode & 0022) && (st.st_mode & 0100);
}

static pid_t launch(const char *path, char *const args[], int keep, int drop)
{
	pid_t pid = fork();
	if (pid)
		return pid;
	close(drop);
	int flags = fcntl(keep, F_GETFD);
	if (flags < 0 || fcntl(keep, F_SETFD, flags & ~FD_CLOEXEC) < 0)
		_exit(126);
	execv(path, args);
	_exit(127);
}

int main(int argc, char **argv)
{
	const char *base = NULL, *web = NULL, *helper = NULL, *root = NULL,
		   *database = NULL, *token = NULL, *catalog = NULL,
		   *config = NULL, *assets = NULL, *port = NULL;
	for (int i = 1; i < argc; i++) {
		const char **target = NULL;
		if (!strcmp(argv[i], "--base"))
			target = &base;
		else if (!strcmp(argv[i], "--web"))
			target = &web;
		else if (!strcmp(argv[i], "--helper"))
			target = &helper;
		else if (!strcmp(argv[i], "--root"))
			target = &root;
		else if (!strcmp(argv[i], "--database"))
			target = &database;
		else if (!strcmp(argv[i], "--token"))
			target = &token;
		else if (!strcmp(argv[i], "--catalog"))
			target = &catalog;
		else if (!strcmp(argv[i], "--web-config"))
			target = &config;
		else if (!strcmp(argv[i], "--web-root"))
			target = &assets;
		else if (!strcmp(argv[i], "--port"))
			target = &port;
		if (!target || *target || i + 1 >= argc || !argv[i + 1][0] ||
		    strlen(argv[i + 1]) > 1023)
			return 2;
		*target = argv[++i];
	}
	if (!executable(base) || !executable(web) || !executable(helper) ||
	    !root || !database || !token || !catalog || !config || !assets ||
	    !port)
		return 2;
	char *end;
	errno = 0;
	long parsed = strtol(port, &end, 10);
	if (errno || *end || parsed < 1 || parsed > 65535)
		return 2;
	int pair[2];
	if (!sb_resource_pair(pair))
		return 2;
	char left[24], right[24];
	snprintf(left, sizeof left, "%d", pair[0]);
	snprintf(right, sizeof right, "%d", pair[1]);
	char *web_args[] = {(char *)web,
			    "--music-ipc",
			    right,
			    "--port",
			    (char *)port,
			    "--config-dir",
			    (char *)config,
			    "--web-root",
			    (char *)assets,
			    NULL};
	char *base_args[] = {(char *)base,
			     "--ownership-token",
			     (char *)token,
			     "--config",
			     (char *)catalog,
			     "--music-root",
			     (char *)root,
			     "--music-database",
			     (char *)database,
			     "--music-helper",
			     (char *)helper,
			     "--music-resource-fd",
			     left,
			     "--music-http-port",
			     (char *)port,
			     "--music-scan",
			     NULL};
	struct sigaction action = {0};
	action.sa_handler = stop_session;
	sigemptyset(&action.sa_mask);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	pid_t web_pid = launch(web, web_args, pair[1], pair[0]);
	pid_t base_pid =
		web_pid > 0 ? launch(base, base_args, pair[0], pair[1]) : -1;
	close(pair[0]);
	close(pair[1]);
	if (web_pid < 0 || base_pid < 0) {
		if (web_pid > 0)
			kill(web_pid, SIGTERM);
		if (base_pid > 0)
			kill(base_pid, SIGTERM);
		return 2;
	}
	fprintf(stderr,
		"MUSIC_SESSION base=%ld web=%ld; no automatic restart\n",
		(long)base_pid,
		(long)web_pid);
	int status = 0;
	pid_t ended;
	do {
		ended = waitpid(-1, &status, 0);
	} while (ended < 0 && errno == EINTR && !stopping);
	if (ended != base_pid)
		kill(base_pid, SIGTERM);
	if (ended != web_pid)
		kill(web_pid, SIGTERM);
	while (waitpid(-1, NULL, 0) > 0 || errno == EINTR) {
	}
	return stopping ? 0
			: (ended > 0 && WIFEXITED(status) ? WEXITSTATUS(status)
							  : 1);
}
