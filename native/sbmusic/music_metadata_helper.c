#define _POSIX_C_SOURCE 200809L
#include "music_metadata.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    char *end;
    long fd = strtol(argv[1], &end, 10);
    if (*end || fd < 0 || fd > 1024)
        return 2;
    struct rlimit memory = {16 * 1024 * 1024, 16 * 1024 * 1024}, cpu = {2, 2}, core = {0, 0},
                  stack = {512 * 1024, 512 * 1024}, files = {64, 64};
    if (setrlimit(RLIMIT_AS, &memory) || setrlimit(RLIMIT_CPU, &cpu) ||
        setrlimit(RLIMIT_CORE, &core) || setrlimit(RLIMIT_STACK, &stack) ||
        setrlimit(RLIMIT_NOFILE, &files))
        return 3;
    DIR *fds = opendir("/proc/self/fd");
    if (!fds)
        return 3;
    struct dirent *entry;
    while ((entry = readdir(fds))) {
        char *tail;
        long inherited = strtol(entry->d_name, &tail, 10);
        if (!*tail && inherited > 2 && inherited != fd && inherited != dirfd(fds))
            close((int)inherited);
    }
    closedir(fds);
    (void)setpriority(PRIO_PROCESS, 0, 5);
    sb_metadata_result result;
    sb_music_metadata((int)fd, &result);
    FILE *status = fopen("/proc/self/status", "r");
    if (status) {
        char line[256];
        while (fgets(line, sizeof line, status))
            if (sscanf(line, "VmHWM: %u kB", &result.peak_rss_kib) == 1)
                break;
        fclose(status);
    }
    const char *p = (const char *)&result;
    size_t left = sizeof result;
    while (left) {
        ssize_t n = write(STDOUT_FILENO, p, left);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return 4;
        p += n;
        left -= (size_t)n;
    }
    return 0;
}
