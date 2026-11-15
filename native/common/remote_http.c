#define _POSIX_C_SOURCE 200809L
#include "remote_http.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <ifaddrs.h>
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
int sb_remote_http_open(const char *url,uint64_t *length)
{
 if(!url||strlen(url)>=2048||strncmp(url,"http://",7))return -1;
 for(const unsigned char *p=(const unsigned char *)url;*p;p++)if(*p<=32||*p>=127||strchr("\\@#",*p))return -1;
 char host[16];size_t n=strcspn(url+7,":/");if(!n||n>=sizeof host)return -1;memcpy(host,url+7,n);host[n]=0;
 struct sockaddr_in a={.sin_family=AF_INET};if(inet_pton(AF_INET,host,&a.sin_addr)!=1)return -1;uint32_t ip=ntohl(a.sin_addr.s_addr);
 if(!((ip>>24)==10||(ip>>20)==0xac1||(ip>>16)==0xc0a8||(ip>>16)==0xa9fe))return -1;
 const char *path=url+7+n;unsigned long port=80;
 if(*path==':'){char *end;if(path[1]<'0'||path[1]>'9')return -1;errno=0;port=strtoul(path+1,&end,10);if(errno||port<1||port>65535)return -1;path=end;}
 if((*path&&*path!='/')||port==22||port==23||port==3483||port==8765)return -1;
 if(port==80||port==443||port==9000){struct ifaddrs *interfaces=NULL;int own=1;
  if(!getifaddrs(&interfaces)){own=0;for(struct ifaddrs *p=interfaces;p;p=p->ifa_next)if(p->ifa_addr&&p->ifa_addr->sa_family==AF_INET&&((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr==a.sin_addr.s_addr)own=1;freeifaddrs(interfaces);}
  if(own)return -1;
 }
 a.sin_port=htons((uint16_t)port);int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
 if(fcntl(fd,F_SETFD,FD_CLOEXEC)<0||fcntl(fd,F_SETFL,O_NONBLOCK)<0)goto fail;
 uint64_t deadline=now()+5000;
 if(connect(fd,(struct sockaddr *)&a,sizeof a)&&errno!=EINPROGRESS)goto fail;
 struct pollfd ready={fd,POLLOUT,0};int error=0;socklen_t size=sizeof error;
 if(poll(&ready,1,3000)<=0||getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)||error)goto fail;
 char request[2304];int bytes=snprintf(request,sizeof request,"GET %s HTTP/1.0\r\nHost: %s:%lu\r\nConnection: close\r\nAccept: audio/mpeg\r\n\r\n",*path?path:"/",host,port);
 size_t sent=0;while(sent<(size_t)bytes&&now()<deadline){ssize_t k=send(fd,request+sent,(size_t)bytes-sent,MSG_NOSIGNAL);if(k>0)sent+=(size_t)k;else if(k<0&&(errno==EAGAIN||errno==EINTR)){ready.events=POLLOUT;poll(&ready,1,100);}else goto fail;}if(sent!=(size_t)bytes)goto fail;
 char headers[8193];size_t used=0;
 while(used<sizeof headers-1&&now()<deadline){ssize_t k=recv(fd,headers+used,1,0);if(k==1){headers[++used]=0;if(used>=4&&!memcmp(headers+used-4,"\r\n\r\n",4))break;}else if(k<0&&(errno==EAGAIN||errno==EINTR)){ready.events=POLLIN;poll(&ready,1,100);}else goto fail;}
 if(used<4||memcmp(headers+used-4,"\r\n\r\n",4)||(strncmp(headers,"HTTP/1.0 200 ",13)&&strncmp(headers,"HTTP/1.1 200 ",13)))goto fail;
 unsigned lengths=0;uint64_t value=0;int audio=0;
 for(char *p=strstr(headers,"\r\n")+2;*p&&p[0]!='\r';){char *end=strstr(p,"\r\n");if(!end)goto fail;*end=0;
  if(!strncasecmp(p,"Transfer-Encoding:",18))goto fail;
  if(!strncasecmp(p,"Content-Type:",13)){const char *mime=p+13;while(*mime==' '||*mime=='\t')mime++;audio=(!strncasecmp(mime,"audio/mpeg",10)&&(mime[10]==0||mime[10]==';'))||(!strncasecmp(mime,"audio/mp3",9)&&(mime[9]==0||mime[9]==';'));}
  if(!strncasecmp(p,"Content-Length:",15)){if(++lengths>1)goto fail;const char *v=p+15;while(*v==' '||*v=='\t')v++;if(!*v)goto fail;for(;*v;v++){if(*v<'0'||*v>'9'||value>((uint64_t)INT64_MAX-(unsigned)(*v-'0'))/10)goto fail;value=value*10+(unsigned)(*v-'0');}}
  p=end+2;
 }
 if(lengths!=1||!value||!audio)goto fail;
 *length=value;return fd;
fail:close(fd);return -1;
}
