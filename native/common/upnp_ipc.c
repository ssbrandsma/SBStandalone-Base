#define _GNU_SOURCE
#include "upnp_ipc.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
static void put(unsigned char *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v>>(56-8*i));}
static uint64_t get(const unsigned char *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v=(v<<8)|p[i];return v;}
static int flags(int fd){if(fd<0)return -1;if(fcntl(fd,F_SETFD,FD_CLOEXEC)<0||fcntl(fd,F_SETFL,O_NONBLOCK)<0){close(fd);return -1;}return fd;}
int sb_upnp_encode(const sb_upnp_request *r,unsigned char out[SB_UPNP_REQUEST_BYTES])
{
 if(!r||!r->request||!r->session||r->operation<SB_UPNP_HEALTH||r->operation>SB_UPNP_RENDERER_CONFIG||r->limit>50)return 0;
 const char *s[]={r->server,r->object,r->field,r->text,r->sort};size_t caps[]={sizeof r->server,sizeof r->object,sizeof r->field,sizeof r->text,sizeof r->sort};
 memset(out,0,SB_UPNP_REQUEST_BYTES);memcpy(out,"SBUPNP1",7);
 put(out+8,r->request);put(out+16,r->session);put(out+24,r->generation);put(out+32,r->operation);put(out+40,r->offset);put(out+48,r->limit);
 size_t at=64;for(unsigned i=0;i<5;i++){size_t n=strnlen(s[i],caps[i]);if(n==caps[i])return 0;put(out+at,n);at+=8;memcpy(out+at,s[i],n);at+=n;}return 1;
}
int sb_upnp_decode(const unsigned char *p,size_t n,sb_upnp_request *r)
{
 if(n!=SB_UPNP_REQUEST_BYTES||memcmp(p,"SBUPNP1\0",8))return 0;
 memset(r,0,sizeof *r);r->request=get(p+8);r->session=get(p+16);r->generation=get(p+24);
 uint64_t op=get(p+32),offset=get(p+40),limit=get(p+48);
 if(!r->request||!r->session||op<SB_UPNP_HEALTH||op>SB_UPNP_RENDERER_CONFIG||offset>UINT32_MAX-50||limit>50)return 0;
 r->operation=(unsigned)op;r->offset=(unsigned)offset;r->limit=(unsigned)limit;
 char *s[]={r->server,r->object,r->field,r->text,r->sort};size_t caps[]={sizeof r->server,sizeof r->object,sizeof r->field,sizeof r->text,sizeof r->sort};
 size_t at=64;for(unsigned i=0;i<5;i++){if(at+8>n)return 0;uint64_t len=get(p+at);at+=8;if(len>=caps[i]||len>n-at||memchr(p+at,0,(size_t)len))return 0;memcpy(s[i],p+at,(size_t)len);at+=(size_t)len;}return 1;
}
int sb_upnp_peer(int fd){struct ucred c;socklen_t n=sizeof c;return !getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&c,&n)&&n==sizeof c&&c.uid==geteuid();}
static int address(const char *path,struct sockaddr_un *a){if(!path||strlen(path)>=sizeof a->sun_path)return 0;memset(a,0,sizeof *a);a->sun_family=AF_UNIX;strcpy(a->sun_path,path);return 1;}
static int directory(const char *path)
{
 char parent[108];if(strlen(path)>=sizeof parent)return 0;strcpy(parent,path);char *slash=strrchr(parent,'/');if(!slash||slash==parent)return 0;*slash=0;
 if(mkdir(parent,0700)&&errno!=EEXIST)return 0;
 struct stat s;
 return !lstat(parent,&s)&&S_ISDIR(s.st_mode)&&s.st_uid==geteuid()&&!(s.st_mode&0077);
}
int sb_upnp_connect(const char *path)
{
 struct sockaddr_un a;if(!address(path,&a))return -1;
 int fd=flags(socket(AF_UNIX,SOCK_SEQPACKET,0));if(fd<0)return -1;
 if(connect(fd,(struct sockaddr *)&a,sizeof a)||!sb_upnp_peer(fd)){close(fd);return -1;}return fd;
}
int sb_upnp_listen(const char *path)
{
 struct sockaddr_un a;if(!address(path,&a)||!directory(path))return -1;
 int probe=sb_upnp_connect(path);if(probe>=0){close(probe);errno=EADDRINUSE;return -1;}
 if(errno!=ENOENT&&errno!=ECONNREFUSED)return -1;
 struct stat st;if(!lstat(path,&st)&&(!S_ISSOCK(st.st_mode)||st.st_uid!=geteuid()))return -1;
 if(unlink(path)&&errno!=ENOENT)return -1;
 int fd=flags(socket(AF_UNIX,SOCK_SEQPACKET,0));if(fd<0)return -1;
 if(bind(fd,(struct sockaddr *)&a,sizeof a)||chmod(path,0600)||listen(fd,4)){close(fd);return -1;}return fd;
}
int sb_upnp_accept(int listener){int fd=flags(accept(listener,NULL,NULL));if(fd>=0&&!sb_upnp_peer(fd)){close(fd);return -1;}return fd;}
void sb_upnp_header(unsigned char *p,uint64_t request,uint64_t session,unsigned status,unsigned size){memset(p,0,40);memcpy(p,"SBUPNR1",7);put(p+8,request);put(p+16,session);put(p+24,status);put(p+32,size);}
int sb_upnp_response(const unsigned char *p,size_t n,uint64_t request,uint64_t session,unsigned *status,size_t *size)
{
 if(n<40||memcmp(p,"SBUPNR1\0",8)||get(p+8)!=request||get(p+16)!=session||get(p+24)>599||get(p+32)>SB_UPNP_REPLY_MAX||get(p+32)!=n-40)return 0;
 *status=(unsigned)get(p+24);*size=n-40;return 1;
}
