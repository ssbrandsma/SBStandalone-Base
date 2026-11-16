/* Private, atomic state files; all file names are manager-generated. */
#include "vpn.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

int vpn_id(const char *s)
{
	return s && strlen(s) == 32 && strspn(s, "0123456789abcdef") == 32;
}

static int trusted_dir(const char *path, int private)
{
	if (!path || path[0] != '/' || strlen(path) > 511)
		return -1;
	char copy[512];
	/* Firmware compatibility alias, not a general symlink exception.  Both
	 * physical ancestor trees remain subject to the normal trust checks. */
	const char *alias = "/mnt/storage/standalonebase";
	size_t alias_len = strlen(alias);
	struct stat link;
	if (!strncmp(path, alias, alias_len) &&
	    (path[alias_len] == '/' || !path[alias_len]) &&
	    !lstat(alias, &link) && S_ISLNK(link.st_mode)) {
		char target[128];
		ssize_t n = readlink(alias, target, sizeof target - 1);
		if (link.st_uid != 0 || n <= 0 ||
		    n >= (ssize_t)sizeof target - 1 ||
		    trusted_dir("/mnt/storage", 0))
			return -1;
		target[n] = 0;
		if (strcmp(target, "/etc/standalonebase") ||
		    snprintf(copy,
			     sizeof copy,
			     "%s%s",
			     target,
			     path + alias_len) >= (int)sizeof copy)
			return -1;
	} else {
		strcpy(copy, path);
	}
	char *save = NULL, *component = strtok_r(copy, "/", &save);
	int parent = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (parent < 0)
		return -1;
	while (component) {
		char *next = strtok_r(NULL, "/", &save);
		if (!strcmp(component, ".") || !strcmp(component, "..")) {
			close(parent);
			return -1;
		}
		if (!next && private && mkdirat(parent, component, 0700) &&
		    errno != EEXIST) {
			close(parent);
			return -1;
		}
		int fd =
			openat(parent,
			       component,
			       O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		close(parent);
		if (fd < 0)
			return -1;
		struct stat st;
		if (fstat(fd, &st) || !S_ISDIR(st.st_mode) ||
		    (st.st_uid != geteuid() && st.st_uid != 0) ||
		    ((st.st_mode & 0022) &&
		     !(st.st_uid == 0 && (st.st_mode & S_ISVTX))) ||
		    (!next && private &&
		     (st.st_uid != geteuid() || (st.st_mode & 0077)))) {
			close(fd);
			return -1;
		}
		parent = fd;
		component = next;
	}
	close(parent);
	return 0;
}

int vpn_private_dir(const char *path)
{
	return trusted_dir(path, 1);
}

int vpn_trusted_dir(const char *path)
{
	return trusted_dir(path, 0);
}

int vpn_store_open(const char *p)
{
	if (vpn_private_dir(p))
		return -1;
	return open(p, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

char *vpn_store_read(int dir, const char *name, size_t limit)
{
	if (strchr(name, '/') || strstr(name, ".."))
		return NULL;
	int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	struct stat st;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    (st.st_mode & 0077) || st.st_size < 0 ||
	    (uint64_t)st.st_size > limit) {
		close(fd);
		return NULL;
	}
	char *s = calloc(1, (size_t)st.st_size + 1);
	if (!s) {
		close(fd);
		return NULL;
	}
	size_t n = 0;
	while (n < (size_t)st.st_size) {
		ssize_t k = read(fd, s + n, (size_t)st.st_size - n);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			break;
		n += (size_t)k;
	}
	close(fd);
	if (n != (size_t)st.st_size) {
		free(s);
		return NULL;
	}
	return s;
}

int vpn_store_write(int dir, const char *name, const char *data)
{
	size_t bytes = strlen(data);
	struct statvfs space;
	if (bytes > 8192 &&
	    (fstatvfs(dir, &space) ||
	     (uint64_t)space.f_bavail * space.f_frsize < bytes + 1024u * 1024u))
		return -1;
	if (strchr(name, '/') || strstr(name, "..") || strlen(data) > VPN_LIMIT)
		return -1;
	char temp[80];
	unsigned char random[12];
	mg_random(random, sizeof random);
	snprintf(temp,
		 sizeof temp,
		 ".tmp-%02x%02x%02x%02x%02x%02x",
		 random[0],
		 random[1],
		 random[2],
		 random[3],
		 random[4],
		 random[5]);
	int fd = openat(dir,
			temp,
			O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
			0600);
	if (fd < 0)
		return -1;
	size_t n = 0, len = strlen(data);
	while (n < len) {
		ssize_t k = write(fd, data + n, len - n);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			break;
		n += (size_t)k;
	}
	int failed = n != len || fsync(fd);
	if (close(fd))
		failed = 1;
	if (!failed && renameat(dir, temp, dir, name))
		failed = 1;
	if (!failed && fsync(dir))
		failed = 1;
	if (failed)
		unlinkat(dir, temp, 0);
	return failed ? -1 : 0;
}

int vpn_store_delete(int dir, const char *name)
{
	if (strchr(name, '/') || strstr(name, ".."))
		return -1;
	int r = unlinkat(dir, name, 0);
	if (r && errno != ENOENT)
		return -1;
	return fsync(dir);
}

char *vpn_public_profile(struct mg_str object, const char *id)
{
	const char *keys[] = {"name",
			      "country",
			      "country_code",
			      "provider",
			      "city",
			      "description",
			      "server",
			      "authentication",
			      "created_at",
			      "updated_at",
			      "last_success"};
	char *out = calloc(1, 4096);
	if (!out)
		return NULL;
	size_t used = mg_snprintf(out, 4096, "{\"id\":%m", MG_ESC(id));
	for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) {
		char path[64];
		snprintf(path, sizeof path, "$.%s", keys[i]);
		char *v = mg_json_get_str(object, path);
		used += mg_snprintf(out + used,
				    4096 - used,
				    ",%m:%m",
				    MG_ESC(keys[i]),
				    MG_ESC(v ? v : ""));
		free(v);
		if (used > 3900) {
			free(out);
			return NULL;
		}
	}
	char *password = mg_json_get_str(object, "$.password");
	used += mg_snprintf(out + used,
			    4096 - used,
			    ",\"credentials_configured\":%s}",
			    password && *password ? "true" : "false");
	if (password) {
		memset(password, 0, strlen(password));
		free(password);
	}
	return out;
}

int vpn_rpc_connect(const char *path, int nonblock)
{
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	if (strlen(path) >= sizeof sa.sun_path)
		return -1;
	strcpy(sa.sun_path, path);
	int fd = vpn_socket(AF_UNIX, SOCK_SEQPACKET, 0, 0);
	if (fd < 0)
		return -1;
	if (nonblock)
		fcntl(fd, F_SETFL, O_NONBLOCK);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) &&
	    errno != EINPROGRESS) {
		close(fd);
		return -1;
	}
	struct ucred peer;
	socklen_t length = sizeof peer;
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) ||
	    peer.uid != geteuid()) {
		close(fd);
		return -1;
	}
	return fd;
}

char *vpn_rpc(const char *path, const char *request)
{
	int fd = vpn_rpc_connect(path, 1);
	if (fd < 0)
		return NULL;
	struct pollfd p = {fd, POLLOUT, 0};
	if (poll(&p, 1, 3000) <= 0 ||
	    send(fd, request, strlen(request), MSG_NOSIGNAL) !=
		    (ssize_t)strlen(request)) {
		close(fd);
		return NULL;
	}
	p.events = POLLIN;
	if (poll(&p, 1, 3000) <= 0) {
		close(fd);
		return NULL;
	}
	char *out = calloc(1, VPN_LIMIT + 1);
	if (!out) {
		close(fd);
		return NULL;
	}
	ssize_t n = recv(fd, out, VPN_LIMIT, MSG_TRUNC);
	close(fd);
	if (n <= 0 || n > (ssize_t)VPN_LIMIT) {
		free(out);
		return NULL;
	}
	return out;
}

int vpn_socket(int family, int type, int protocol, int nonblock)
{
	int fd = socket(family, type, protocol);
	if (fd >= 0) {
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		if (nonblock)
			fcntl(fd, F_SETFL, O_NONBLOCK);
		int size = VPN_LIMIT * 2;
		setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
	}
	return fd;
}
