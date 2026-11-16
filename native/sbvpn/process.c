#include "vpn.h"
#include "../common/process_spawn.h"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

const char *vpn_program(const char *name)
{
	static char path[512];
	const char *dirs[] = {"/usr/sbin", "/sbin", "/usr/bin", "/bin"};
	for (unsigned i = 0; i < 4; i++) {
		snprintf(path, sizeof path, "%s/%s", dirs[i], name);
		if (!access(path, X_OK))
			return path;
	}
	return NULL;
}

/* Only callers constructing fixed argument vectors may use this function. */
int vpn_exec(const char *path,
	     char *const argv[],
	     char *out,
	     size_t cap,
	     unsigned timeout)
{
	unsigned limit;
	if (!sb_process_fd_limit(&limit))
		return -1;
	int pipes[2];
	if (!path || pipe(pipes))
		return -1;
	pid_t pid = fork();
	if (pid < 0) {
		close(pipes[0]);
		close(pipes[1]);
		return -1;
	}
	if (!pid) {
		dup2(pipes[1], STDOUT_FILENO);
		dup2(pipes[1], STDERR_FILENO);
		int null = open("/dev/null", O_RDONLY);
		dup2(null, STDIN_FILENO);
		close(pipes[0]);
		close(pipes[1]);
		sb_process_close_fds(limit, -1);
		execv(path, argv);
		_exit(127);
	}
	close(pipes[1]);
	fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	size_t n = 0;
	uint64_t end = mg_millis() + timeout;
	int status = 0, done = 0;
	while (mg_millis() < end) {
		char buf[1024];
		ssize_t r = read(pipes[0], buf, sizeof buf);
		if (r > 0 && out && n + 1 < cap) {
			size_t add = (size_t)r;
			if (add > cap - 1 - n)
				add = cap - 1 - n;
			memcpy(out + n, buf, add);
			n += add;
		}
		if (waitpid(pid, &status, WNOHANG) == pid) {
			done = 1;
			break;
		}
		struct pollfd p = {pipes[0], POLLIN, 0};
		poll(&p, 1, 10);
	}
	if (!done) {
		kill(pid, SIGKILL);
		waitpid(pid, &status, 0);
	} /* Drain the bounded tail after child exit. */
	for (;;) {
		char buf[1024];
		ssize_t r = read(pipes[0], buf, sizeof buf);
		if (r <= 0)
			break;
		if (out && n + 1 < cap) {
			size_t add = (size_t)r;
			if (add > cap - 1 - n)
				add = cap - 1 - n;
			memcpy(out + n, buf, add);
			n += add;
		}
	}
	if (out && cap)
		out[n] = 0;
	close(pipes[0]);
	return done && WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
}
