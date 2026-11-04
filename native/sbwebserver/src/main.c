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
#include <errno.h>
#include <limits.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief Run the program entry point.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
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
 if (!music_ready || s.port < 1 || s.port > 65535) return 2;
#endif
	printf("SBWebserver listening on 0.0.0.0:%d\n", s.port);
	return http_server_run(s.port, root);
}
