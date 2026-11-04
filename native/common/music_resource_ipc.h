#ifndef SB_MUSIC_RESOURCE_IPC_H
#define SB_MUSIC_RESOURCE_IPC_H
#include "music_contracts.h"
/* Inherited private AF_UNIX SOCK_SEQPACKET pair, no pathname/listener. Wire
 * fields explicitly encoded, never sizeof(native struct) across ARM/host. */
#define SB_RESOURCE_WIRE 136u

enum {
	SB_RESOURCE_REGISTER = 1,
	SB_RESOURCE_REVOKE,
	SB_RESOURCE_RESET,
	SB_RESOURCE_ACK,
	SB_RESOURCE_RESERVE,
	SB_RESOURCE_ACTIVATE
};

typedef struct {
	unsigned operation, codec, status;
	uint64_t sequence;
	sb_music_resource resource;
	uint64_t device, inode, mtime_seconds, mtime_nanoseconds;
	int fd; /* REGISTER transfers one duplicate; receiver always owns it */
} sb_resource_message;

int sb_resource_pair(int pair[2]);
int sb_resource_adopt(
	int socket); /* connected Unix seqpacket; NONBLOCK/CLOEXEC */
/* 1 success, 0 EAGAIN, -1 closed/malformed/error. Sender retains fd. */
int sb_resource_send(int socket, const sb_resource_message *);
/* Closes all received descriptors on failure, CLOEXEC applied explicitly. */
int sb_resource_receive(int socket, sb_resource_message *);
/* Blocking entropy: construction before loops, or file worker only. */
int sb_music_resource_entropy(unsigned char token[SB_MUSIC_TOKEN_BYTES]);
#endif
