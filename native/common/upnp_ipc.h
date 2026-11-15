#ifndef SB_UPNP_IPC_H
#define SB_UPNP_IPC_H
#include <stddef.h>
#include <stdint.h>
#define SB_UPNP_REQUEST_BYTES 4096u
#define SB_UPNP_REPLY_MAX 131072u
#define SB_UPNP_SOCKET "/var/run/sb-upnp/control.sock"
enum { SB_UPNP_HEALTH=1, SB_UPNP_SERVERS, SB_UPNP_BROWSE, SB_UPNP_METADATA,
 SB_UPNP_CAPABILITIES, SB_UPNP_SEARCH, SB_UPNP_UPDATE_ID,SB_UPNP_RENDERER_CONFIG };
typedef struct {
 uint64_t request, session, generation;
 unsigned operation, offset, limit;
 char server[256], object[512], field[64], text[512], sort[256];
} sb_upnp_request;
int sb_upnp_encode(const sb_upnp_request *, unsigned char [SB_UPNP_REQUEST_BYTES]);
int sb_upnp_decode(const unsigned char *,size_t,sb_upnp_request *);
int sb_upnp_listen(const char *path);
int sb_upnp_connect(const char *path);
int sb_upnp_accept(int listener);
int sb_upnp_peer(int fd);
void sb_upnp_header(unsigned char *,uint64_t,uint64_t,unsigned,unsigned);
int sb_upnp_response(const unsigned char *,size_t,uint64_t,uint64_t,unsigned *,size_t *);
#endif
