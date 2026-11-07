/*
 * Management webserver process entry point.
 *
 * Loads settings, applies command-line overrides and starts http_server.c.
 * The Lua applet supervises this sbwebserver process alongside the local
 * LMS-compatible sbbase service and the sbproxy compatibility proxy.
 */

#include "http_server.h"
#include "settings.h"
#include "backup.h"
#if defined(SB_MUSIC_PLAYBACK_TESTING) || defined(SB_MUSIC_RUNTIME)
#include "music_stream_http.h"
#include "../../common/music_resource_ipc.h"
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void web_fatal(int sig)
{
	const char *m = "SBWEB_FATAL signal\n";
	if (sig == SIGSEGV)
		m = "SBWEB_FATAL SIGSEGV\n";
	else if (sig == SIGABRT)
		m = "SBWEB_FATAL SIGABRT\n";
	else if (sig == SIGBUS)
		m = "SBWEB_FATAL SIGBUS\n";
	else if (sig == SIGILL)
		m = "SBWEB_FATAL SIGILL\n";
	else if (sig == SIGFPE)
		m = "SBWEB_FATAL SIGFPE\n";
	(void)write(STDERR_FILENO, m, strlen(m));
	signal(sig, SIG_DFL);
	raise(sig);
}

static void web_exit(void)
{
	fprintf(stderr, "SBWEB_EXIT\n");
}

/**
 * @brief Run the program entry point.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
	signal(SIGSEGV, web_fatal);
	signal(SIGABRT, web_fatal);
	signal(SIGBUS, web_fatal);
	signal(SIGILL, web_fatal);
	signal(SIGFPE, web_fatal);
	atexit(web_exit);
	if (argc >= 3 && !strcmp(argv[1], "--apply-backup"))
		return backup_apply(argv[2], argc == 4 && !strcmp(argv[3], "--restart-jive"));
#ifdef SB_MUSIC_RUNTIME
 int music_ready = 0;
#endif
	Settings s;
	settings_load(&s);
	const char *root = "/usr/share/jive/applets/SBWebserver/web";
	for (int i = 1; i < argc; i++)
#if defined(SB_MUSIC_PLAYBACK_TESTING) || defined(SB_MUSIC_RUNTIME)
		if (
#ifdef SB_MUSIC_RUNTIME
            !strcmp(argv[i], "--music-ipc")
#else
            !strcmp(argv[i], "--test-music-ipc")
#endif
            && i + 1 < argc) {
#ifdef SB_MUSIC_RUNTIME
            if (music_ready) return 2;
#endif
			char *end;
			errno = 0;
			long fd = strtol(argv[++i], &end, 10);
			if (errno || *end || fd < 3 || fd > INT_MAX ||
			    !sb_web_music_init((int)fd))
				return 2;
#ifdef SB_MUSIC_RUNTIME
            music_ready = 1;
#endif
		} else
#endif
			if (!strcmp(argv[i], "--port") && i + 1 < argc)
			s.port = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--web-root") && i + 1 < argc)
			root = argv[++i];
		else if (!strcmp(argv[i], "--config-dir") && i + 1 < argc) {
#ifdef _WIN32
			_putenv_s("SBWEBSERVER_CONFIG_DIR", argv[++i]);
#else
			setenv("SBWEBSERVER_CONFIG_DIR", argv[++i], 1);
#endif
		}
#ifdef SB_MUSIC_RUNTIME
        else return 2;
 if (!music_ready) {
	/* Separately supervised base: rendezvous instead of an inherited
	 * descriptor. Absence serves the UI without music, never a
	 * half-wired stream registry. */
	int listener = sb_resource_rendezvous_listen();
	int conn = listener >= 0 ?
		sb_resource_rendezvous_accept(listener, 180000) : -1;
	int fd = conn >= 0 ? sb_resource_take_fd(conn, 10000) : -1;
	if (conn >= 0)
		close(conn);
	if (listener >= 0) {
		close(listener);
		sb_resource_rendezvous_cleanup();
	}
	if (fd < 0 || !sb_web_music_init(fd)) {
		if (fd >= 0)
			close(fd);
		fprintf(stderr, "SBWebserver: music rendezvous unavailable; UI without music\n");
	} else
		music_ready = 1;
 }
 if (s.port < 1 || s.port > 65535) return 2;
#endif
	printf("SBWebserver listening on 0.0.0.0:%d\n", s.port);
	return http_server_run(s.port, root);
}
