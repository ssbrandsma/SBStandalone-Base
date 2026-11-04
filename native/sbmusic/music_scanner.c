#define _GNU_SOURCE
#include "music_scanner.h"
#include "music_metadata.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
typedef struct {
    DIR *dir;
    char path[SB_STORAGE_PATH], cover[512];
    dev_t dev;
    ino_t ino;
    struct stat observed;
} directory;
struct sb_music_scanner {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    sb_scan_options options;
    sb_music_media current;
    sb_scan_status published, working;
    int notify[2], wake[2], queued, cancel, stop, exited, initial, root_lost, media_revoked;
    uint64_t last, started, last_publish;
    directory stack[SB_SCAN_DEPTH];
    sb_ml_track records[SB_SCAN_BATCH];
    unsigned stamp_only[SB_SCAN_BATCH];
    unsigned rename_record[SB_SCAN_BATCH];
    unsigned record_count;
};
static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void notify_fd(int fd) {
    char c = 1;
    ssize_t n;
    do {
        n = write(fd, &c, 1);
    } while (n < 0 && errno == EINTR);
}
static int make_pipe(int fds[2]) {
    if (pipe(fds))
        return 0;
    for (unsigned i = 0; i < 2; i++)
        if (fcntl(fds[i], F_SETFD, FD_CLOEXEC) || fcntl(fds[i], F_SETFL, O_NONBLOCK)) {
            close(fds[0]);
            close(fds[1]);
            return 0;
        }
    return 1;
}
static int cancelled(sb_music_scanner *s) {
    pthread_mutex_lock(&s->lock);
    int cancel = s->cancel || s->stop || s->media_revoked ||
                 !sb_music_media_current(&s->current, &s->options.root.media);
    pthread_mutex_unlock(&s->lock);
    return cancel;
}
static int root_current(sb_music_scanner *s) {
    if (cancelled(s))
        return 0;
    int okay =
        sb_storage_root_check(&s->options.root, &s->options.root.media, s->options.mount_reader,
                              s->options.mount_context) == SB_STORAGE_OK;
    if (!okay)
        s->root_lost = 1;
    return okay;
}
static void publish(sb_music_scanner *s, int force) {
    uint64_t now = now_ms();
    s->working.elapsed_ms = now - s->started;
    if (!force && now - s->last_publish < 250)
        return;
    pthread_mutex_lock(&s->lock);
    s->published = s->working;
    pthread_mutex_unlock(&s->lock);
    s->last_publish = now;
    notify_fd(s->notify[1]);
}
static void error(sb_music_scanner *s, const char *path, int code, const char *message) {
    s->working.errors++;
    if (s->working.error_count < SB_SCAN_ERRORS) {
        sb_scan_error *e = &s->working.error[s->working.error_count++];
        snprintf(e->path, sizeof e->path, "%.127s", path);
        snprintf(e->message, sizeof e->message, "%.127s", message);
        e->code = code;
    }
}
static sb_ml_status db(sb_music_scanner *s, sb_ml_request *request, sb_ml_result *result) {
    memset(result, 0, sizeof *result);
    request->scan_token = s->working.epoch;
    sb_ml_status status = sb_music_library_exchange(s->options.library, request, result);
    if (status == SB_ML_OK)
        s->working.library_revision = result->revision;
    else if (result->status == SB_ML_OK) {
        result->status = status;
        snprintf(result->error, sizeof result->error, "Library broker unavailable");
    }
    return status;
}
static void identity(sb_music_scanner *s, sb_ml_track *t) {
    t->identity.source = s->options.root.media.source;
    t->media_generation = s->options.root.media.generation;
    t->scan_epoch = s->working.epoch;
}
static uint64_t hash_bytes(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}
static int fingerprint(int fd, const struct stat *st, const char *cover, char out[128]) {
    unsigned char b[256];
    uint64_t h = UINT64_C(14695981039346656037);
    size_t count = st->st_size < 256 ? (size_t)st->st_size : 256;
    ssize_t n = pread(fd, b, count, 0);
    if (n != (ssize_t)count)
        return 0;
    h = hash_bytes(h, b, count);
    if (st->st_size > 256) {
        n = pread(fd, b, 256, st->st_size - 256);
        if (n != 256)
            return 0;
        h = hash_bytes(h, b, 256);
    }
    h = hash_bytes(h, cover, strlen(cover));
    snprintf(out, 128, "v1:%llx:%llx:%llx:%llx:%lx:%llx", (unsigned long long)st->st_dev,
             (unsigned long long)st->st_ino, (unsigned long long)st->st_size,
             (unsigned long long)st->st_mtime, (unsigned long)st->st_mtim.tv_nsec,
             (unsigned long long)h);
    return 1;
}
static int metadata(sb_music_scanner *s, int fd, sb_metadata_result *result) {
    int pipefd[2];
    if (pipe(pipefd))
        return 0;
    for (unsigned i = 0; i < 2; i++)
        fcntl(pipefd[i], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    /* Dup to high CLOEXEC descriptors before spawn, avoiding fd3/stdio aliasing. */
    int input = fcntl(fd, F_DUPFD, 10), output = fcntl(pipefd[1], F_DUPFD, 10);
    if (input < 0 || output < 0) {
        if (input >= 0)
            close(input);
        if (output >= 0)
            close(output);
        close(pipefd[0]);
        close(pipefd[1]);
        return 0;
    }
    fcntl(input, F_SETFD, FD_CLOEXEC);
    fcntl(output, F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, input, 3);
    posix_spawn_file_actions_adddup2(&actions, output, 1);
    char *argv[] = {s->options.helper, "3", NULL};
    pid_t pid;
    int rc = posix_spawn(&pid, s->options.helper, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(input);
    close(output);
    close(pipefd[1]);
    if (rc) {
        close(pipefd[0]);
        return 0;
    }
    size_t got = 0;
    uint64_t deadline = now_ms() + s->options.helper_timeout_ms;
    int okay = 1;
    while (got < sizeof *result) {
        if (cancelled(s) || now_ms() >= deadline) {
            okay = 0;
            break;
        }
        struct pollfd p[2] = {{pipefd[0], POLLIN, 0}, {s->wake[0], POLLIN, 0}};
        int wait = (int)(deadline - now_ms());
        if (wait < 0)
            wait = 0;
        do {
            rc = poll(p, 2, wait);
        } while (rc < 0 && errno == EINTR);
        if (rc <= 0) {
            okay = 0;
            break;
        }
        if (p[1].revents) {
            char bytes[32];
            ssize_t n = read(s->wake[0], bytes, sizeof bytes);
            (void)n;
        }
        if (p[0].revents) {
            ssize_t n = read(pipefd[0], (char *)result + got, sizeof *result - got);
            if (n > 0)
                got += (size_t)n;
            else if (!n || (errno != EAGAIN && errno != EINTR)) {
                okay = 0;
                break;
            }
        }
    }
    if (!okay)
        kill(pid, SIGKILL);
    int status = 0;
    int killed = !okay;
    for (;;) {
        pid_t reaped = waitpid(pid, &status, WNOHANG);
        if (reaped == pid)
            break;
        if (reaped < 0) {
            if (errno == EINTR)
                continue;
            okay = 0;
            break;
        }
        if (!killed && (cancelled(s) || now_ms() >= deadline)) {
            kill(pid, SIGKILL);
            killed = 1;
            okay = 0;
        }
        int remaining = killed ? 10 : (int)(deadline - now_ms());
        if (remaining < 0)
            remaining = 0;
        struct pollfd wait[2] = {{pipefd[0], POLLIN, 0}, {s->wake[0], POLLIN, 0}};
        poll(wait, 2, pipefd[0] < 0 ? 1 : remaining);
        if (pipefd[0] >= 0 && (wait[0].revents & POLLIN)) {
            char extra;
            ssize_t n = read(pipefd[0], &extra, 1);
            if (n > 0) {
                okay = 0;
                kill(pid, SIGKILL);
                killed = 1;
            }
        }
        if (pipefd[0] >= 0 && (wait[0].revents & POLLHUP)) {
            close(pipefd[0]);
            pipefd[0] = -1;
        }
        char bytes[128];
        ssize_t n = read(s->wake[0], bytes, sizeof bytes);
        (void)n;
    }
    if (pipefd[0] >= 0)
        close(pipefd[0]);
    return okay && got == sizeof *result && WIFEXITED(status) && !WEXITSTATUS(status) &&
           result->magic == 0x53424d32;
}
static int ignored(const char *name) {
    static const char *names[] = {
        ".StandaloneBase", ".Squeezebox", ".Trash",       ".Trashes",
        ".Spotlight-V100", ".fseventsd",  "$RECYCLE.BIN", "System Volume Information",
        "lost+found",      ".git"};
    if (!strcmp(name, ".") || !strcmp(name, ".."))
        return 1;
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcasecmp(name, names[i]))
            return 1;
    return 0;
}
static int audio_name(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot &&
           (!strcasecmp(dot, ".mp3") || !strcasecmp(dot, ".flac") || !strcasecmp(dot, ".ogg"));
}
static int safe_open(sb_music_scanner *s, const char *path, int dir, int *fd) {
    return !cancelled(s) && sb_storage_open_relative(&s->options.root, &s->options.root.media, path,
                                                     dir, s->options.mount_reader,
                                                     s->options.mount_context, fd) == SB_STORAGE_OK;
}
static void cover(sb_music_scanner *s, directory *d) {
    static const char *names[] = {"cover.jpg", "folder.jpg", "Cover.jpg",  "Folder.jpg",
                                  "cover.png", "folder.png", "cover.jpeg", "folder.jpeg"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        char path[SB_STORAGE_PATH];
        if (snprintf(path, sizeof path, "%s%s%s", d->path, d->path[0] ? "/" : "", names[i]) >=
            (int)sizeof path)
            continue;
        struct stat st;
        if (fstatat(dirfd(d->dir), names[i], &st, AT_SYMLINK_NOFOLLOW)) {
            if (errno != ENOENT)
                error(s, path, errno, "Cannot inspect sidecar");
            continue;
        }
        if (!S_ISREG(st.st_mode))
            continue;
        int fd = -1;
        if (!safe_open(s, path, 0, &fd)) {
            error(s, path, errno, "Cannot open sidecar");
            continue;
        }
        unsigned char magic[8];
        ssize_t n = pread(fd, magic, sizeof magic, 0);
        char fp[128];
        int valid = fingerprint(fd, &st, "", fp);
        close(fd);
        const char *mime = n == 8 && !memcmp(magic, "\x89PNG\r\n\x1a\n", 8) ? "image/png"
                           : n >= 3 && magic[0] == 255 && magic[1] == 216 && magic[2] == 255
                               ? "image/jpeg"
                               : NULL;
        if (!valid) {
            error(s, path, errno, "Sidecar changed/unreadable");
            continue;
        }
        if (!mime)
            continue;
        if (snprintf(d->cover, sizeof d->cover, "sidecar:%zu:%s:%s:%s", strlen(path), path, mime,
                     fp) >= (int)sizeof d->cover) {
            d->cover[0] = 0;
            error(s, path, SB_META_LIMIT, "Sidecar reference exceeds bound");
        }
        return;
    }
}
static int flush(sb_music_scanner *s) {
    if (!s->record_count)
        return 1;
    if (!root_current(s))
        return 0;
    sb_ml_request r = {0};
    sb_ml_result result;
    r.operation = SB_ML_BEGIN;
    r.transaction = s->working.epoch;
    if (db(s, &r, &result) != SB_ML_OK) {
        error(s, "", result.status, "Cannot begin ingestion batch");
        return 0;
    }
    int ok = 1;
    for (unsigned i = 0; i < s->record_count; i++) {
        if (cancelled(s)) {
            ok = 0;
            break;
        }
        r.operation = s->stamp_only[i] ? SB_ML_SCAN_TOUCH : SB_ML_UPSERT;
        r.data.track = s->records[i];
        if (db(s, &r, &result) != SB_ML_OK) {
            error(s, r.data.track.path, result.status, result.error);
            ok = 0;
            break;
        }
    }
    if (ok)
        ok = root_current(s);
    r.operation = ok ? SB_ML_COMMIT : SB_ML_ROLLBACK;
    if (db(s, &r, &result) != SB_ML_OK) {
        error(s, "", result.status, "Batch completion failed");
        ok = 0;
    }
    if (ok)
        for (unsigned i = 0; i < s->record_count; i++) {
            if (s->stamp_only[i])
                continue;
            if (s->rename_record[i])
                s->working.renamed++;
            if (s->records[i].identity.track_id)
                s->working.updated++;
            else
                s->working.added++;
        }
    s->record_count = 0;
    return ok;
}
static int process_file(sb_music_scanner *s, const char *path, const char *art) {
    int fd = -1;
    if (!safe_open(s, path, 0, &fd)) {
        error(s, path, errno, "Audio file inaccessible");
        return 1;
    }
    struct stat st, after;
    if (fstat(fd, &st)) {
        close(fd);
        error(s, path, errno, "Audio stat failed");
        return 1;
    }
    sb_ml_request r = {0};
    sb_ml_result old;
    identity(s, &r.data.track);
    strcpy(r.data.track.path, path);
    r.operation = SB_ML_SCAN_LOOKUP;
    sb_ml_status status = db(s, &r, &old);
    if (status != SB_ML_OK && status != SB_ML_NOT_FOUND) {
        close(fd);
        error(s, path, status, "Library lookup failed");
        return 0;
    }
    char fp[128];
    if (!fingerprint(fd, &st, art, fp)) {
        close(fd);
        error(s, path, errno, "Fingerprint read failed");
        return 1;
    }
    if (fstat(fd, &after) || st.st_size != after.st_size ||
        st.st_mtim.tv_sec != after.st_mtim.tv_sec || st.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        st.st_ctim.tv_sec != after.st_ctim.tv_sec || st.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
        close(fd);
        error(s, path, SB_META_IO, "File changed during fingerprint");
        return 1;
    }
    if (s->options.inode_identity)
        for (unsigned i = 0; i < s->record_count; i++)
            if (!strcmp(fp, s->records[i].fingerprint)) {
                close(fd);
                s->working.unchanged++;
                s->working.recognized++;
                return 1;
            }
    uint64_t id = status == SB_ML_OK ? old.record_id : 0;
    int renamed = 0;
    if (!s->initial && id && !strcmp(old.data.track.fingerprint, fp)) {
        close(fd);
        s->stamp_only[s->record_count] = 1;
        s->records[s->record_count] = old.data.track;
        identity(s, &s->records[s->record_count++]);
        s->working.unchanged++;
        s->working.recognized++;
        return s->record_count < s->options.batch_files || flush(s);
    }
    if (!id && s->options.inode_identity && st.st_nlink >= 1) {
        r.data.track.path[0] = 0;
        strcpy(r.data.track.fingerprint, fp);
        status = db(s, &r, &old);
        if (status == SB_ML_OK) {
            if (old.data.track.scan_epoch == s->working.epoch) {
                close(fd);
                s->working.unchanged++;
                s->working.recognized++;
                return 1;
            }
            int previous = -1;
            errno = 0;
            sb_storage_status old_status = sb_storage_open_relative(
                &s->options.root, &s->options.root.media, old.data.track.path, 0,
                s->options.mount_reader, s->options.mount_context, &previous);
            if (st.st_nlink == 1 && old_status == SB_STORAGE_IO && errno == ENOENT &&
                root_current(s)) {
                id = old.record_id;
                renamed = 1;
            }
            if (previous >= 0)
                close(previous);
        } else if (status != SB_ML_NOT_FOUND && status != SB_ML_CONFLICT) {
            close(fd);
            error(s, path, status, "Rename evidence lookup failed");
            return 0;
        }
    }
    sb_metadata_result result;
    int parsed = metadata(s, fd, &result);
    int changed =
        fstat(fd, &after) || st.st_size != after.st_size ||
        st.st_mtim.tv_sec != after.st_mtim.tv_sec || st.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        st.st_ctim.tv_sec != after.st_ctim.tv_sec || st.st_ctim.tv_nsec != after.st_ctim.tv_nsec;
    close(fd);
    if (cancelled(s))
        return 0;
    if (!parsed || result.status != SB_META_OK || changed) {
        error(s, path, parsed ? result.status : SB_META_IO,
              changed  ? "File changed during probe"
              : parsed ? result.error
                       : "Metadata helper crash/timeout/protocol failure");
        return 1;
    }
    s->stamp_only[s->record_count] = 0;
    s->rename_record[s->record_count] = renamed;
    sb_ml_track *t = &s->records[s->record_count++];
    *t = result.track;
    identity(s, t);
    t->identity.track_id = id;
    strcpy(t->path, path);
    strcpy(t->fingerprint, fp);
    if (t->file_size != (uint64_t)st.st_size || result.bytes_read > SB_META_READ_MAX ||
        !sb_music_library_track_valid(t)) {
        s->record_count--;
        error(s, path, SB_META_INVALID, "Untrusted helper DTO failed WP1 validation");
        return 1;
    }
    if (strcmp(t->codec, "mp3") && strcmp(t->codec, "flac") && strcmp(t->codec, "ogg")) {
        s->record_count--;
        error(s, path, SB_META_UNSUPPORTED, "Helper codec unsupported");
        return 1;
    }
    s->working.recognized++;
    s->working.parser_bytes += result.bytes_read;
    if (result.peak_rss_kib > s->working.parser_peak_rss_kib)
        s->working.parser_peak_rss_kib = result.peak_rss_kib;
    if (!t->title[0]) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (strlen(base) >= sizeof t->title) {
            s->record_count--;
            error(s, path, SB_META_LIMIT, "Filename fallback exceeds metadata bound");
            return 1;
        }
        strcpy(t->title, base);
    }
    if (!t->artist_count) {
        strcpy(t->artists[0], "(Unknown artist)");
        t->artist_count = 1;
    }
    if (!t->artwork_ref[0] && art[0])
        strcpy(t->artwork_ref, art);
    if (t->artwork_ref[0]) {
        size_t len = strlen(t->artwork_ref);
        if (snprintf(t->artwork_ref + len, sizeof t->artwork_ref - len, "@%s", fp) >=
            (int)(sizeof t->artwork_ref - len)) {
            s->record_count--;
            error(s, path, SB_META_LIMIT, "Artwork revision reference bound");
            return 1;
        }
    }
    return s->record_count < s->options.batch_files || flush(s);
}
static void scan(sb_music_scanner *s) {
    memset(&s->working, 0, sizeof s->working);
    s->root_lost = 0;
    s->working.state = SB_SCAN_SCANNING;
    s->working.request_id = s->last;
    s->working.generation = s->options.root.media.generation;
    s->started = now_ms();
    char stale_wake[128];
    ssize_t drained = read(s->wake[0], stale_wake, sizeof stale_wake);
    (void)drained;
    s->record_count = 0;
    publish(s, 1);
    if (!root_current(s)) {
        error(s, "", SB_STORAGE_STALE, "Validated root/media unavailable");
        goto done;
    }
    sb_ml_request r = {0};
    sb_ml_result result;
    r.operation = SB_ML_SCAN_START;
    identity(s, &r.data.track);
    strcpy(r.data.track.path, s->options.root.relative);
    if (db(s, &r, &result) != SB_ML_OK) {
        error(s, "", result.status, result.error);
        goto done;
    }
    s->working.epoch = result.record_id;
    publish(s, 1);
    unsigned depth = 0;
    directory *first = &s->stack[0];
    memset(first, 0, sizeof *first);
    int root_flags = fcntl(s->options.root.fd, F_GETFL);
    int fd = root_flags < 0 ? -1 : openat(s->options.root.fd, ".",
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | (root_flags & O_NOATIME));
    if (fd < 0) {
        error(s, "", errno, "Root descriptor open failed");
        goto abort;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    first->dir = fdopendir(fd);
    if (!first->dir) {
        close(fd);
        error(s, "", errno, "Root directory open failed");
        goto abort;
    }
    struct stat st;
    if (fstat(fd, &st)) {
        closedir(first->dir);
        first->dir = NULL;
        error(s, "", errno, "Root stat failed");
        goto abort;
    }
    first->dev = st.st_dev;
    first->ino = st.st_ino;
    first->observed = st;
    depth = 1;
    cover(s, first);
    while (depth && !cancelled(s) && s->working.errors < 64) {
        directory *d = &s->stack[depth - 1];
        errno = 0;
        struct dirent *entry = readdir(d->dir);
        if (!entry) {
            if (errno)
                error(s, d->path, errno, "Directory read failed");
            struct stat observed;
            if (fstat(dirfd(d->dir), &observed) ||
                observed.st_mtim.tv_sec != d->observed.st_mtim.tv_sec ||
                observed.st_mtim.tv_nsec != d->observed.st_mtim.tv_nsec ||
                observed.st_ctim.tv_sec != d->observed.st_ctim.tv_sec ||
                observed.st_ctim.tv_nsec != d->observed.st_ctim.tv_nsec)
                error(s, d->path, SB_META_IO, "Directory changed during traversal");
            closedir(d->dir);
            d->dir = NULL;
            depth--;
            continue;
        }
        if (ignored(entry->d_name))
            continue;
        s->working.visited++;
        if (s->working.visited > 1000000) {
            error(s, d->path, SB_META_LIMIT, "File count bound exceeded");
            break;
        }
        char path[SB_STORAGE_PATH];
        if (snprintf(path, sizeof path, "%s%s%s", d->path, d->path[0] ? "/" : "", entry->d_name) >=
            (int)sizeof path) {
            error(s, d->path, SB_META_LIMIT, "Path exceeds secure root bound");
            continue;
        }
        if (fstatat(dirfd(d->dir), entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) {
            error(s, path, errno, "Entry stat failed");
            continue;
        }
        if (S_ISLNK(st.st_mode))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (!(st.st_mode & 0444) || !(st.st_mode & 0111)) {
                error(s, path, EACCES, "Directory permission mask denies traversal");
                continue;
            }
            if (depth == SB_SCAN_DEPTH) {
                error(s, path, SB_META_LIMIT, "Directory depth bound exceeded");
                continue;
            }
            int loop = 0;
            for (unsigned i = 0; i < depth; i++)
                loop |= s->stack[i].dev == st.st_dev && s->stack[i].ino == st.st_ino;
            if (loop) {
                error(s, path, SB_META_INVALID, "Directory loop detected");
                continue;
            }
            int child = -1;
            if (!safe_open(s, path, 1, &child)) {
                error(s, path, errno, "Directory inaccessible");
                continue;
            }
            directory *next = &s->stack[depth];
            memset(next, 0, sizeof *next);
            next->dir = fdopendir(child);
            if (!next->dir) {
                close(child);
                error(s, path, errno, "Directory stream failed");
                continue;
            }
            if (fstat(child, &next->observed)) {
                closedir(next->dir);
                next->dir = NULL;
                error(s, path, errno, "Directory descriptor stat failed");
                continue;
            }
            strcpy(next->path, path);
            next->dev = next->observed.st_dev;
            next->ino = next->observed.st_ino;
            depth++;
            cover(s, next);
        } else if (S_ISREG(st.st_mode) && audio_name(entry->d_name)) {
            if (!(st.st_mode & 0444)) {
                error(s, path, EACCES, "Audio permission mask denies reading");
                continue;
            }
            uint64_t began = now_ms(), before = s->working.parser_bytes;
            if (!process_file(s, path, d->cover))
                break;
            uint64_t work = now_ms() - began;
            uint64_t rest =
                work * (100 - s->options.work_duty_percent) / s->options.work_duty_percent;
            if (s->options.io_bytes_per_second) {
                uint64_t target = (s->working.parser_bytes - before + 512) * 1000 /
                                  s->options.io_bytes_per_second;
                if (target > work && target - work > rest)
                    rest = target - work;
            }
            if (rest < s->options.yield_ms)
                rest = s->options.yield_ms;
            while (rest && !cancelled(s)) {
                unsigned part = rest > 1000 ? 1000 : (unsigned)rest;
                struct pollfd wake = {s->wake[0], POLLIN, 0};
                poll(&wake, 1, (int)part);
                char bytes[128];
                ssize_t n = read(s->wake[0], bytes, sizeof bytes);
                (void)n;
                rest -= part;
            }
        }
        if (depth + 12 > s->working.max_descriptors)
            s->working.max_descriptors = depth + 12;
        publish(s, 0);
    }
    while (depth) {
        directory *d = &s->stack[--depth];
        if (d->dir) {
            closedir(d->dir);
            d->dir = NULL;
        }
    }
    if (!cancelled(s) && flush(s) && !s->working.errors && root_current(s)) {
        s->working.state = SB_SCAN_COMMITTING;
        publish(s, 1);
        r.operation = SB_ML_SCAN_FINISH;
        r.transaction = 0;
        if (db(s, &r, &result) == SB_ML_OK) {
            s->working.removed = result.record_id;
            s->working.state = SB_SCAN_COMPLETED;
            publish(s, 1);
            return;
        }
        error(s, "", result.status, "Missing-track transaction failed");
    }
abort:
    r.operation = SB_ML_SCAN_ABORT;
    r.transaction = 0;
    (void)db(s, &r, &result);
done:
    pthread_mutex_lock(&s->lock);
    int removed = s->media_revoked || !sb_music_media_current(&s->current, &s->options.root.media),
        cancel = s->cancel || s->stop;
    pthread_mutex_unlock(&s->lock);
    s->working.state = removed || s->root_lost ? SB_SCAN_MEDIA_REMOVED
                       : cancel                ? SB_SCAN_CANCELLED
                                               : SB_SCAN_FAILED;
    publish(s, 1);
}
static void *worker(void *context) {
    sb_music_scanner *s = context;
    pthread_mutex_lock(&s->lock);
    for (;;) {
        while (!s->queued && !s->stop)
            pthread_cond_wait(&s->condition, &s->lock);
        if (s->stop)
            break;
        s->queued = 0;
        pthread_mutex_unlock(&s->lock);
        scan(s);
        pthread_mutex_lock(&s->lock);
    }
    s->exited = 1;
    pthread_mutex_unlock(&s->lock);
    notify_fd(s->notify[1]);
    return NULL;
}
sb_music_scanner *sb_music_scan_create(const sb_scan_options *options, sb_scan_code *code) {
    if (code)
        *code = SB_SCAN_INVALID;
    if (!options)
        return NULL;
    if (!options->enabled) {
        if (code)
            *code = SB_SCAN_DISABLED;
        return NULL;
    }
    if (!options->approved_database) {
        if (code)
            *code = SB_SCAN_POLICY;
        return NULL;
    }
    if (!options->library || options->root.fd < 0 || !options->root.media.online ||
        !memchr(options->helper, 0, sizeof options->helper) || options->helper[0] != '/' ||
        options->helper_timeout_ms > 5000 || options->yield_ms > 1000 ||
        options->work_duty_percent > 100 || options->batch_files > SB_SCAN_BATCH)
        return NULL;
    if (options->inode_identity && strcmp(options->root.mount.fs, "ext2") &&
        strcmp(options->root.mount.fs, "ext3") && strcmp(options->root.mount.fs, "tmpfs"))
        return NULL;
    sb_music_scanner *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->options = *options;
    s->options.root.fd = dup(options->root.fd);
    if (s->options.root.fd < 0) {
        free(s);
        return NULL;
    }
    fcntl(s->options.root.fd, F_SETFD, FD_CLOEXEC);
    if (!s->options.helper_timeout_ms)
        s->options.helper_timeout_ms = 3000;
    if (!s->options.batch_files)
        s->options.batch_files = SB_SCAN_BATCH;
    if (!s->options.work_duty_percent)
        s->options.work_duty_percent = 50;
    s->current = options->root.media;
    s->published.state = SB_SCAN_IDLE;
    if (!make_pipe(s->notify)) {
        close(s->options.root.fd);
        free(s);
        return NULL;
    }
    if (!make_pipe(s->wake)) {
        close(s->notify[0]);
        close(s->notify[1]);
        close(s->options.root.fd);
        free(s);
        return NULL;
    }
    if (pthread_mutex_init(&s->lock, NULL)) {
        for (unsigned i = 0; i < 2; i++) {
            close(s->notify[i]);
            close(s->wake[i]);
        }
        close(s->options.root.fd);
        free(s);
        return NULL;
    }
    if (pthread_cond_init(&s->condition, NULL)) {
        for (unsigned i = 0; i < 2; i++) {
            close(s->notify[i]);
            close(s->wake[i]);
        }
        close(s->options.root.fd);
        pthread_mutex_destroy(&s->lock);
        free(s);
        return NULL;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    int rc = pthread_create(&s->thread, &attr, worker, s);
    pthread_attr_destroy(&attr);
    if (rc) {
        for (unsigned i = 0; i < 2; i++) {
            close(s->notify[i]);
            close(s->wake[i]);
        }
        close(s->options.root.fd);
        pthread_cond_destroy(&s->condition);
        pthread_mutex_destroy(&s->lock);
        free(s);
        return NULL;
    }
    if (code)
        *code = SB_SCAN_OK;
    return s;
}
sb_scan_code sb_music_scan_request(sb_music_scanner *s, uint64_t id, int initial) {
    if (!s)
        return SB_SCAN_DISABLED;
    if (pthread_mutex_trylock(&s->lock))
        return SB_SCAN_BUSY;
    sb_scan_state state = s->published.state;
    sb_scan_code code = s->stop || !id || id <= s->last ? SB_SCAN_INVALID
                        : s->queued || state == SB_SCAN_SCANNING || state == SB_SCAN_COMMITTING ||
                                state == SB_SCAN_QUEUED
                            ? SB_SCAN_BUSY
                            : SB_SCAN_OK;
    if (code == SB_SCAN_OK) {
        s->last = id;
        s->initial = !!initial;
        s->cancel = 0;
        s->queued = 1;
        s->published.state = SB_SCAN_QUEUED;
        s->published.request_id = id;
        pthread_cond_signal(&s->condition);
    }
    pthread_mutex_unlock(&s->lock);
    return code;
}
sb_scan_code sb_music_scan_cancel(sb_music_scanner *s, uint64_t id) {
    if (!s)
        return SB_SCAN_DISABLED;
    if (pthread_mutex_trylock(&s->lock))
        return SB_SCAN_BUSY;
    sb_scan_code code = id != s->last                              ? SB_SCAN_INVALID
                        : s->published.state == SB_SCAN_COMMITTING ? SB_SCAN_BUSY
                                                                   : SB_SCAN_OK;
    uint64_t token = s->published.epoch;
    if (code == SB_SCAN_OK)
        s->cancel = 1;
    pthread_mutex_unlock(&s->lock);
    if (code == SB_SCAN_OK) {
        notify_fd(s->wake[1]);
        if (token)
            (void)sb_music_library_cancel_scan(s->options.library, token);
    }
    return code;
}
sb_scan_code sb_music_scan_media(sb_music_scanner *s, const sb_music_media *media) {
    if (!s)
        return SB_SCAN_DISABLED;
    if (!media)
        return SB_SCAN_INVALID;
    if (pthread_mutex_trylock(&s->lock))
        return SB_SCAN_BUSY;
    s->current = *media;
    uint64_t token = s->published.epoch;
    int removed = !sb_music_media_current(media, &s->options.root.media);
    if (removed)
        s->media_revoked = 1;
    pthread_mutex_unlock(&s->lock);
    notify_fd(s->wake[1]);
    if (removed && token)
        (void)sb_music_library_cancel_scan(s->options.library, token);
    return SB_SCAN_OK;
}
sb_scan_code sb_music_scan_status(sb_music_scanner *s, sb_scan_status *status) {
    if (!s)
        return SB_SCAN_DISABLED;
    if (!status)
        return SB_SCAN_INVALID;
    if (pthread_mutex_trylock(&s->lock))
        return SB_SCAN_BUSY;
    char b[128];
    ssize_t n = read(s->notify[0], b, sizeof b);
    (void)n;
    *status = s->published;
    pthread_mutex_unlock(&s->lock);
    return SB_SCAN_OK;
}
int sb_music_scan_fd(const sb_music_scanner *s) { return s ? s->notify[0] : -1; }
sb_scan_code sb_music_scan_stop(sb_music_scanner *s) {
    if (!s)
        return SB_SCAN_DISABLED;
    if (pthread_mutex_trylock(&s->lock))
        return SB_SCAN_BUSY;
    s->stop = 1;
    pthread_cond_signal(&s->condition);
    pthread_mutex_unlock(&s->lock);
    notify_fd(s->wake[1]);
    return SB_SCAN_OK;
}
sb_scan_code sb_music_scan_destroy(sb_music_scanner *s) {
    if (!s)
        return SB_SCAN_DISABLED;
    if (pthread_mutex_trylock(&s->lock))
        return SB_SCAN_BUSY;
    int done = s->exited;
    pthread_mutex_unlock(&s->lock);
    if (!done)
        return SB_SCAN_BUSY;
    pthread_join(s->thread, NULL);
    for (unsigned i = 0; i < 2; i++) {
        close(s->notify[i]);
        close(s->wake[i]);
    }
    close(s->options.root.fd);
    pthread_cond_destroy(&s->condition);
    pthread_mutex_destroy(&s->lock);
    free(s);
    return SB_SCAN_OK;
}
