#define _GNU_SOURCE
#include "music_metadata.h"
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
int main(int argc, char **argv) {
    (void)argc;
    struct rlimit core = {0, 0};
    setrlimit(RLIMIT_CORE, &core);
    if (strstr(argv[0], "hang")) {
        for (;;)
            pause();
    }
    if (strstr(argv[0], "crash")) {
        raise(SIGSEGV);
        return 1;
    }
    sb_metadata_result result;
    sb_music_metadata(3, &result);
    int after = strstr(argv[0], "after") != NULL;
    int fd = after ? -1 : open("/proc/self/fd/3", O_WRONLY | O_APPEND);
    if (!after) {
        if (fd < 0)
            return 2;
        if (write(fd, "changed", 7) != 7)
            return 2;
        close(fd);
    }
    const char *p = (const char *)&result;
    size_t left = sizeof result;
    while (left) {
        ssize_t n = write(1, p, left);
        if (n <= 0)
            return 3;
        p += n;
        left -= (size_t)n;
    }
    if (after)
        for (;;)
            pause();
    return 0;
}
