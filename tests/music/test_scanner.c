#define _GNU_SOURCE
#include "music_metadata.h"
#include "music_scanner.h"
#include "third_party/sqlite/sqlite3.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
static uint64_t clock_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static unsigned fd_count(void) {
    DIR *dir = opendir("/proc/self/fd");
    assert(dir);
    unsigned n = 0;
    struct dirent *e;
    while ((e = readdir(dir)))
        if (e->d_name[0] != '.')
            n++;
    closedir(dir);
    return n - 1;
}
static unsigned peak_rss(void) {
    FILE *f = fopen("/proc/self/status", "r");
    assert(f);
    char line[256];
    unsigned n = 0;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "VmHWM: %u kB", &n) == 1)
            break;
    fclose(f);
    return n;
}
static sb_storage_status readonly_mounts(void *context, sb_storage_mounts *table) {
    (void)context;
    sb_storage_status status = sb_storage_read_mounts(NULL, table);
    if (status == SB_STORAGE_OK)
        for (unsigned i = 0; i < table->count; i++)
            if (!strcmp(table->rows[i].path, "/dev/shm"))
                table->rows[i].read_only = 1;
    return status;
}
static uint64_t write_bytes(void) {
    FILE *f = fopen("/proc/self/io", "r");
    if (!f)
        return 0;
    char line[256];
    unsigned long long n = 0;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "write_bytes: %llu", &n) == 1)
            break;
    fclose(f);
    return n;
}
static ssize_t fail_write(int fd, const void *data, size_t n) {
    (void)fd;
    (void)data;
    (void)n;
    errno = EIO;
    return -1;
}
static ssize_t fail_pwrite(int fd, const void *data, size_t n, off_t at) {
    (void)at;
    return fail_write(fd, data, n);
}
static void pause_ms(unsigned ms) {
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000};
    nanosleep(&t, NULL);
}
static sb_scan_code request_scan(sb_music_scanner *s, uint64_t id, int initial) {
    sb_scan_code code;
    while ((code = sb_music_scan_request(s, id, initial)) == SB_SCAN_BUSY)
        pause_ms(1);
    return code;
}
static void json(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            putchar('\\');
        if (c >= 32)
            putchar(c);
    }
    putchar('"');
}
static sb_ml_result wait_db(sb_music_library *h) {
    sb_ml_result r;
    for (;;) {
        sb_ml_status status = sb_music_library_receive(h, &r);
        if (status == SB_ML_OK)
            return r;
        assert(status == SB_ML_BUSY);
        struct pollfd p = {sb_music_library_fd(h), POLLIN, 0};
        assert(poll(&p, 1, 10000) > 0);
    }
}
static sb_ml_result exchange(sb_music_library *h, sb_ml_request *r) {
    sb_ml_result result = {0};
    sb_ml_status status;
    while ((status = sb_music_library_exchange(h, r, &result)) == SB_ML_BUSY)
        pause_ms(1);
    assert(status == result.status);
    return result;
}
typedef struct {
    sb_music_library *library;
    sb_music_media media;
    pthread_mutex_t lock;
    int stop;
    unsigned count, conflicts, total;
    uint64_t random;
    double samples[4096];
} query_loop;
static void *queries(void *context) {
    query_loop *q = context;
    for (;;) {
        pthread_mutex_lock(&q->lock);
        int stop = q->stop;
        pthread_mutex_unlock(&q->lock);
        if (stop)
            break;
        sb_ml_request r = {0};
        r.operation = SB_ML_QUERY;
        r.data.query.media = q->media;
        r.data.query.limit = 1;
        r.data.query.kind = SB_QUERY_TRACKS;
        uint64_t start = clock_ms();
        sb_ml_result result = exchange(q->library, &r);
        if (result.status == SB_ML_OK) {
            double elapsed = (double)(clock_ms() - start);
            q->total++;
            if (q->count < 4096)
                q->samples[q->count++] = elapsed;
            else {
                q->random = q->random * UINT64_C(6364136223846793005) + 1;
                unsigned at = (unsigned)(q->random % q->total);
                if (at < 4096)
                    q->samples[at] = elapsed;
            }
        } else if (result.status == SB_ML_CONFLICT)
            q->conflicts++;
        else
            assert(result.status == SB_ML_OK || result.status == SB_ML_CANCELLED ||
                   result.status == SB_ML_OFFLINE);
        pause_ms(10);
    }
    return NULL;
}
static int compare(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
int main(int argc, char **argv) {
    assert(argc >= 3);
    unsigned fd_baseline = fd_count(), fd_peak = fd_baseline;
    uint64_t writes_before = write_bytes();
    if (!strcmp(argv[1], "probe")) {
        int fd = open(argv[2], O_RDONLY);
        assert(fd >= 0);
        sb_metadata_result r;
        sb_music_metadata(fd, &r);
        close(fd);
        printf("{\"status\":%d,\"warnings\":%u,\"bytes\":%llu,\"title\":", r.status, r.warnings,
               (unsigned long long)r.bytes_read);
        json(r.track.title);
        printf(",\"album\":");
        json(r.track.album);
        printf(",\"album_artist\":");
        json(r.track.album_artist);
        printf(",\"codec\":");
        json(r.track.codec);
        printf(",\"artists\":[");
        for (unsigned i = 0; i < r.track.artist_count; i++) {
            if (i)
                putchar(',');
            json(r.track.artists[i]);
        }
        printf("],\"genres\":[");
        for (unsigned i = 0; i < r.track.genre_count; i++) {
            if (i)
                putchar(',');
            json(r.track.genres[i]);
        }
        printf("],\"duration_ms\":%llu,\"rate\":%u,\"channels\":%u,\"bitrate\":%u,\"bit_depth\":%u,"
               "\"track\":%u,\"track_total\":%u,\"disc\":%u,\"disc_total\":%u,\"year\":%u,"
               "\"compilation\":%d,\"art\":",
               (unsigned long long)r.track.duration_ms, r.track.sample_rate, r.track.channels,
               r.track.bitrate, r.track.bit_depth, r.track.track, r.track.track_total, r.track.disc,
               r.track.disc_total, r.track.year, r.track.compilation);
        json(r.track.artwork_ref);
        printf(",\"error\":");
        json(r.error);
        puts("}");
        return 0;
    }
    assert(argc >= 6 && !strncmp(argv[2], "/dev/shm/", 9));
    sb_storage_mounts *table = malloc(sizeof *table);
    assert(table);
    assert(sb_storage_read_mounts(NULL, table) == SB_STORAGE_OK);
    sb_storage_mount mount = {0};
    for (unsigned i = 0; i < table->count; i++)
        if (!strcmp(table->rows[i].path, "/dev/shm"))
            mount = table->rows[i];
    free(table);
    assert(mount.mount_id);
    sb_music_media media = {{{42, 43}}, 1, 1};
    if (!strcmp(argv[1], "generation2"))
        media.generation = 2;
    sb_storage_mount_reader reader = NULL;
    if (!strcmp(argv[1], "readonly-media")) {
        mount.read_only = 1;
        reader = readonly_mounts;
    }
    sb_storage_root whole, root;
    assert(sb_storage_validate("/dev", &mount, &media, reader, NULL, &whole) == SB_STORAGE_OK);
    assert(sb_storage_root_child(&whole, &media, argv[2] + 9, reader, NULL, &root) ==
           SB_STORAGE_OK);
    sb_storage_root_close(&whole);
    sb_ml_options options = {0};
    snprintf(options.path, sizeof options.path, "%s", argv[3]);
    options.create = 1;
    options.query_ms = 5000;
    options.busy_ms = 100;
    if (!strcmp(argv[1], "readonly-db")) {
        options.create = 0;
        options.read_only = 1;
    }
    sb_music_library *library = sb_music_library_start(&options);
    assert(library);
    sb_ml_result opened = wait_db(library);
    if (opened.status != SB_ML_OK) {
        fprintf(stderr, "open: %s\n", opened.error);
        abort();
    }
    sb_ml_request request = {0};
    request.operation = SB_ML_SOURCE;
    request.data.media = media;
    assert(exchange(library, &request).status == SB_ML_OK);
    sqlite3_vfs *fault_vfs = NULL;
    if (!strcmp(argv[1], "db-fail")) {
        fault_vfs = sqlite3_vfs_find(NULL);
        assert(fault_vfs->xSetSystemCall);
        assert(fault_vfs->xSetSystemCall(fault_vfs, "write", (sqlite3_syscall_ptr)fail_write) ==
               SQLITE_OK);
        assert(fault_vfs->xSetSystemCall(fault_vfs, "pwrite", (sqlite3_syscall_ptr)fail_pwrite) ==
               SQLITE_OK);
        assert(fault_vfs->xSetSystemCall(fault_vfs, "pwrite64", (sqlite3_syscall_ptr)fail_pwrite) ==
               SQLITE_OK);
    }
    sb_scan_options scan = {0};
    scan.enabled = scan.approved_database = 1;
    scan.library = library;
    scan.root = root;
    scan.mount_reader = reader;
    scan.inode_identity = 1;
    scan.batch_files = 16;
    scan.helper_timeout_ms = 250;
    scan.work_duty_percent = 100; /* unthrottled controlled host benchmark */
    const char *timeout = getenv("SB_SCAN_TEST_TIMEOUT_MS");
    if (timeout)
        scan.helper_timeout_ms = (unsigned)strtoul(timeout, NULL, 10);
    snprintf(scan.helper, sizeof scan.helper, "%s", argv[4]);
    sb_scan_code code;
    sb_scan_options disabled = scan;
    disabled.enabled = 0;
    assert(!sb_music_scan_create(&disabled, &code) && code == SB_SCAN_DISABLED);
    disabled = scan;
    disabled.approved_database = 0;
    assert(!sb_music_scan_create(&disabled, &code) && code == SB_SCAN_POLICY);
    sb_music_scanner *scanner = sb_music_scan_create(&scan, &code);
    assert(scanner && code == SB_SCAN_OK);
    if (!strcmp(argv[1], "stale")) {
        sb_music_media stale = media;
        stale.generation++;
        while (sb_music_scan_media(scanner, &stale) == SB_SCAN_BUSY)
            pause_ms(1);
        while (sb_music_scan_media(scanner, &media) == SB_SCAN_BUSY)
            pause_ms(1); /* a stale snapshot cannot undo the revocation latch */
    }
    assert(sb_music_scan_destroy(scanner) == SB_SCAN_BUSY);
    query_loop *query = calloc(1, sizeof *query);
    assert(query);
    query->library = library;
    query->media = media;
    pthread_mutex_init(&query->lock, NULL);
    pthread_t thread;
    int concurrent = argc > 6 && !strcmp(argv[6], "queries");
    if (concurrent)
        assert(!pthread_create(&thread, NULL, queries, query));
    uint64_t start = clock_ms();
    assert(request_scan(scanner, 1, atoi(argv[5])) == SB_SCAN_OK);
    assert(sb_music_scan_request(scanner, 2, 0) == SB_SCAN_BUSY);
    sb_scan_status status = {0};
    int acted = 0;
    unsigned pass = 0;
again:
    for (;;) {
        unsigned fds = fd_count();
        if (fds > fd_peak)
            fd_peak = fds;
        if (sb_music_scan_status(scanner, &status) == SB_SCAN_OK &&
            status.state >= SB_SCAN_COMPLETED)
            break;
        if (!acted && argc > 6 && clock_ms() - start >= 20) {
            if (!strcmp(argv[6], "cancel")) {
                while (sb_music_scan_cancel(scanner, 1) == SB_SCAN_BUSY)
                    pause_ms(1);
                acted = 1;
            } else if (!strcmp(argv[6], "remove")) {
                sb_music_media offline = media;
                offline.online = 0;
                while (sb_music_scan_media(scanner, &offline) == SB_SCAN_BUSY)
                    pause_ms(1);
                acted = 1;
            } else if (!strcmp(argv[6], "stop")) {
                while (sb_music_scan_stop(scanner) == SB_SCAN_BUSY)
                    pause_ms(1);
                acted = 1;
            }
        }
        struct pollfd p = {sb_music_scan_fd(scanner), POLLIN, 0};
        poll(&p, 1, 10);
        assert(clock_ms() - start < 600000);
    }
    if (argc > 6 && !strcmp(argv[6], "twice") && !pass++) {
        assert(status.state == SB_SCAN_COMPLETED);
        assert(request_scan(scanner, 2, 0) == SB_SCAN_OK);
        goto again;
    }
    if (concurrent) {
        pthread_mutex_lock(&query->lock);
        query->stop = 1;
        pthread_mutex_unlock(&query->lock);
        pthread_join(thread, NULL);
    }
    uint64_t stopped = clock_ms();
    while (sb_music_scan_stop(scanner) == SB_SCAN_BUSY)
        pause_ms(1);
    while (sb_music_scan_destroy(scanner) == SB_SCAN_BUSY)
        pause_ms(1);
    uint64_t stop_ms = clock_ms() - stopped;
    if (fault_vfs)
        for (unsigned i = 0; i < 3; i++)
            assert(fault_vfs->xSetSystemCall(fault_vfs,
                                             i == 0   ? "write"
                                             : i == 1 ? "pwrite"
                                                      : "pwrite64",
                                             NULL) == SQLITE_OK);
    request.operation = SB_ML_STATS;
    request.scan_token = 0;
    request.transaction = 0;
    sb_ml_result measured=exchange(library,&request);
    assert(measured.status==SB_ML_OK || status.state!=SB_SCAN_COMPLETED);
    sb_ml_stats statistics={0};
    if(measured.status==SB_ML_OK)statistics=measured.data.stats;
    request.operation = SB_ML_CLOSE;
    assert(exchange(library, &request).status == SB_ML_OK);
    assert(sb_music_library_destroy(library) == SB_ML_OK);
    sb_storage_root_close(&root);
    qsort(query->samples, query->count, sizeof(double), compare);
    struct rusage self, children;
    getrusage(RUSAGE_SELF, &self);
    getrusage(RUSAGE_CHILDREN, &children);
    printf("{\"state\":%d,\"visited\":%llu,\"recognized\":%llu,\"added\":%llu,\"updated\":%llu,"
           "\"unchanged\":%llu,\"renamed\":%llu,\"removed\":%llu,\"errors\":%llu,\"recoverable\":"
           "%llu,\"fatal\":%llu,\"unsupported\":%llu,\"corrupt\":%llu,\"limited\":%llu,"
           "\"candidates\":%llu,\"directories\":%llu,\"elapsed_ms\":%"
           "llu,\"parser_bytes\":%llu,\"revision\":%llu,\"epoch\":%llu,\"fd_bound\":%u,\"rss_kib\":"
           "%ld,\"cpu_ms\":%.3f,\"child_cpu_ms\":%.3f,\"shutdown_ms\":%llu,\"queries\":%u,\"query_"
           "conflicts\":%u,\"query_p95_ms\":%.3f,\"error_detail\":[",
           status.state, (unsigned long long)status.visited, (unsigned long long)status.recognized,
           (unsigned long long)status.added, (unsigned long long)status.updated,
           (unsigned long long)status.unchanged, (unsigned long long)status.renamed,
           (unsigned long long)status.removed, (unsigned long long)status.errors,
           (unsigned long long)status.recoverable_errors, (unsigned long long)status.fatal_errors,
           (unsigned long long)status.unsupported, (unsigned long long)status.corrupt,
           (unsigned long long)status.limited, (unsigned long long)status.candidates,
           (unsigned long long)status.directories,
           (unsigned long long)status.elapsed_ms, (unsigned long long)status.parser_bytes,
           (unsigned long long)status.library_revision, (unsigned long long)status.epoch,
           status.max_descriptors, (long)peak_rss(),
           (self.ru_utime.tv_sec + self.ru_stime.tv_sec) * 1000. +
               (self.ru_utime.tv_usec + self.ru_stime.tv_usec) / 1000.,
           (children.ru_utime.tv_sec + children.ru_stime.tv_sec) * 1000. +
               (children.ru_utime.tv_usec + children.ru_stime.tv_usec) / 1000.,
           (unsigned long long)stop_ms, query->total, query->conflicts,
           query->count ? query->samples[query->count * 95 / 100] : 0);
    for (unsigned i = 0; i < status.error_count; i++) {
        if (i)
            putchar(',');
        json(status.error[i].message);
    }
    printf("],\"error_class\":[");
    for (unsigned i = 0; i < status.error_count; i++) {
        if (i)
            putchar(',');
        printf("{\"path\":");
        json(status.error[i].path);
        printf(",\"code\":%d,\"recoverable\":%d,\"message\":", status.error[i].code,
               status.error[i].recoverable);
        json(status.error[i].message);
        putchar('}');
    }
    printf("],\"parser_peak_rss_kib\":%u,\"fd_baseline\":%u,\"fd_peak_sampled\":%u,\"fd_final\":%u,"
           "\"write_bytes\":%llu,\"query_p50_ms\":%.3f,\"query_p99_ms\":%.3f,"
           "\"metadata_parses\":%llu,\"db_bytes\":%llu,\"sql_inserts\":%llu,\"sql_updates\":%llu,"
           "\"sql_deletes\":%llu,\"track_updates\":%llu,\"stats_available\":%s}\n",
           status.parser_peak_rss_kib, fd_baseline, fd_peak, fd_count(),
           (unsigned long long)(write_bytes() - writes_before),
           query->count?query->samples[query->count*50/100]:0,
           query->count?query->samples[query->count*99/100]:0,
           (unsigned long long)status.metadata_parses,(unsigned long long)statistics.db_bytes,
           (unsigned long long)statistics.sql_inserts,(unsigned long long)statistics.sql_updates,
           (unsigned long long)statistics.sql_deletes,(unsigned long long)statistics.track_updates,
           measured.status==SB_ML_OK?"true":"false");
    assert(fd_count() == fd_baseline);
    pthread_mutex_destroy(&query->lock);
    free(query);
    return 0;
}
