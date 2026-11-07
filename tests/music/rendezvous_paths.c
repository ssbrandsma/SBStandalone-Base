/* Rendezvous pairing path shim (test-only): redirects the hardcoded
 * production music paths, the mount table and the disk-alias tree into a
 * disposable directory so the production bootstrap runs unprivileged.
 * Controlled by SB_RV_ROOT; without it every call passes through untouched. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *root(void)
{
	const char *r = getenv("SB_RV_ROOT");
	return r && *r ? r : NULL;
}

/* Rewrite /A... to <root>/A... for the redirected absolute prefixes. */
static int redirect(const char *path, char *out, size_t cap)
{
	const char *r = root();
	if (!r || !path || path[0] != '/')
		return 0;
	static const char *prefixes[] = {
		"/dev/standalone-music-test", "/media/", "/media", "/dev/disk",
	};
	for (unsigned i = 0; i < 4; i++) {
		size_t n = strlen(prefixes[i]);
		if (!strncmp(path, prefixes[i], n) &&
		    (path[n] == 0 || path[n] == '/')) {
			if ((size_t)snprintf(out, cap, "%s/root%s", r, path) >= cap)
				return 0;
			return 1;
		}
	}
	if (!strcmp(path, "/proc/self/mountinfo") ||
	    !strcmp(path, "/proc/mounts")) {
		if ((size_t)snprintf(out, cap, "%s/mountinfo", r) >= cap)
			return 0;
		return 1;
	}
	return 0;
}

static int redirect_root(const char *path, int flags, char *out, size_t cap)
{
	/* The descriptor walk opens the filesystem root read-only. */
	if (strcmp(path, "/") || (flags & O_ACCMODE) != O_RDONLY ||
	    !(flags & O_DIRECTORY))
		return 0;
	const char *r = root();
	if (!r)
		return 0;
	if ((size_t)snprintf(out, cap, "%s/root", r) >= cap)
		return 0;
	return 1;
}

static int open_common(const char *path, int flags, mode_t mode, int at,
		       int dirfd)
{
	static int (*real_open)(const char *, int, ...) = NULL;
	static int (*real_openat)(int, const char *, int, ...) = NULL;
	if (!real_open) {
		real_open = dlsym(RTLD_NEXT, "open");
		real_openat = dlsym(RTLD_NEXT, "openat");
	}
	char mapped[512];
	/* Relative opens inside a redirected walk resolve through the already
	 * redirected parent descriptor; absolute paths map explicitly. */
	if (!at && (redirect(path, mapped, sizeof mapped) ||
		    redirect_root(path, flags, mapped, sizeof mapped)))
		path = mapped;
	if (at)
		return real_openat(dirfd, path, flags, mode);
	return real_open(path, flags, mode);
}

int open(const char *path, int flags, ...)
{
	va_list ap;
	va_start(ap, flags);
	mode_t mode = va_arg(ap, mode_t);
	va_end(ap);
	return open_common(path, flags, mode, 0, 0);
}

int open64(const char *path, int flags, ...)
{
	va_list ap;
	va_start(ap, flags);
	mode_t mode = va_arg(ap, mode_t);
	va_end(ap);
	return open_common(path, flags, mode, 0, 0);
}

int openat(int dirfd, const char *path, int flags, ...)
{
	va_list ap;
	va_start(ap, flags);
	mode_t mode = va_arg(ap, mode_t);
	va_end(ap);
	if (path && path[0] == '/')
		return open_common(path, flags, mode, 0, 0);
	return open_common(path, flags, mode, 1, dirfd);
}

int openat64(int dirfd, const char *path, int flags, ...)
{
	va_list ap;
	va_start(ap, flags);
	mode_t mode = va_arg(ap, mode_t);
	va_end(ap);
	if (path && path[0] == '/')
		return open_common(path, flags, mode, 0, 0);
	return open_common(path, flags, mode, 1, dirfd);
}

/* Ownership/permission checks use stat, not open. */
static int stat_common(const char *path, struct stat *st, int lstat_call)
{
	static int (*real_stat)(const char *, struct stat *) = NULL;
	static int (*real_lstat)(const char *, struct stat *) = NULL;
	if (!real_stat) {
		real_stat = dlsym(RTLD_NEXT, "stat");
		real_lstat = dlsym(RTLD_NEXT, "lstat");
	}
	char mapped[512];
	if (redirect(path, mapped, sizeof mapped))
		path = mapped;
	return lstat_call ? real_lstat(path, st) : real_stat(path, st);
}

int stat(const char *path, struct stat *st)
{
	return stat_common(path, st, 0);
}

int stat64(const char *path, struct stat64 *st)
{
	return stat_common(path, (struct stat *)st, 0);
}

int lstat(const char *path, struct stat *st)
{
	return stat_common(path, st, 1);
}

int lstat64(const char *path, struct stat64 *st)
{
	return stat_common(path, (struct stat *)st, 1);
}

/* glibc opendir issues its open internally, bypassing any open/openat
 * interposition; map the directory itself. */
DIR *opendir(const char *path)
{
	static DIR *(*real_opendir)(const char *) = NULL;
	if (!real_opendir)
		real_opendir = dlsym(RTLD_NEXT, "opendir");
	char mapped[512];
	if (redirect(path, mapped, sizeof mapped))
		path = mapped;
	return real_opendir(path);
}

/* glibc stdio may open without calling the public open(); map here too. */
static FILE *fopen_common(const char *path, const char *mode)
{
	static FILE *(*real_fopen)(const char *, const char *) = NULL;
	if (!real_fopen)
		real_fopen = dlsym(RTLD_NEXT, "fopen");
	char mapped[512];
	if (redirect(path, mapped, sizeof mapped))
		path = mapped;
	return real_fopen(path, mode);
}

FILE *fopen(const char *path, const char *mode)
{
	return fopen_common(path, mode);
}

FILE *fopen64(const char *path, const char *mode)
{
	return fopen_common(path, mode);
}
