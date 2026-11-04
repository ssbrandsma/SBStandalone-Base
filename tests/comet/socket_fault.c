#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>

/* Host-only deterministic socket pressure; never linked into a product. */
int accept(int fd, struct sockaddr *address, socklen_t *length)
{
	static int (*real_accept)(int, struct sockaddr *, socklen_t *);
	if (!real_accept)
		*(void **)(&real_accept) = dlsym(RTLD_NEXT, "accept");
	int result = real_accept(fd, address, length), size = 1024;
	if (result >= 0)
		setsockopt(result, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
	return result;
}

ssize_t send(int fd, const void *buffer, size_t length, int flags)
{
	static ssize_t (*real_send)(int, const void *, size_t, int);
	static unsigned calls;
	if (!real_send)
		*(void **)(&real_send) = dlsym(RTLD_NEXT, "send");
	if (getenv("SB_TEST_PARTIAL_SEND")) {
		if (++calls % 3 == 0) {
			errno = EAGAIN;
			return -1;
		}
		if (length > 37)
			length = 37;
	}
	return real_send(fd, buffer, length, flags);
}
