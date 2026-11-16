/* Production IPC dispatcher. Only the discovery thread mutates the registry;
 * workers take immutable snapshots. Two bounded workers isolate network I/O
 * from accept/cancellation and from Base's event loop. */
#include "rpc.h"
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <errno.h>
#include <ifaddrs.h>
typedef struct {int fd,started,done;uint64_t accepted;pthread_t thread;sb_upnp_request request;} operation;
static operation operations[2];
static void *discovery_worker(void *unused)
{
 (void)unused;
 while(!stop){notice n;
  if(pop(&n)){pthread_mutex_lock(&state.mutex);reconcile(&n);pthread_mutex_unlock(&state.mutex);}
  else {struct timespec t={0,100000000};nanosleep(&t,NULL);}
  pthread_mutex_lock(&state.mutex);sbupnp_registry_expire(&state.registry,millis());pthread_mutex_unlock(&state.mutex);
  renderer_tick();
 }return NULL;
}
static void append_string(char *out,size_t cap,size_t *used,const char *s)
{
 if(*used>=cap)return;
 FILE *f=fmemopen(out+*used,cap-*used,"w");if(!f){*used=cap;return;}
 fputc('"',f);for(const unsigned char *p=(const unsigned char *)s;*p;p++){if(*p=='"'||*p=='\\'){fputc('\\',f);fputc(*p,f);}else if(*p<32)fprintf(f,"\\u%04x",*p);else fputc(*p,f);}fputc('"',f);
 long n=ftell(f);int err=fclose(f);if(err||n<0||(size_t)n>=cap-*used)*used=cap;else *used+=(size_t)n;
}
static void *request_worker(void *context)
{
 operation *op=context;char *out=malloc(SB_UPNP_REPLY_MAX+40);unsigned status=503;
 if(out){char *json=out+40;sb_upnp_request *r=&op->request;
  if(r->operation==SB_UPNP_HEALTH){pthread_mutex_lock(&renderer.lock);int enabled=renderer.enabled;pthread_mutex_unlock(&renderer.lock);snprintf(json,SB_UPNP_REPLY_MAX,"{\"service\":\"sbupnp\",\"protocol\":1,\"status\":\"ok\",\"renderer\":%s,\"renderer_port\":%u}",enabled?"true":"false",UpnpGetServerPort());status=200;}
  else if(r->operation==SB_UPNP_RENDERER_CONFIG){pthread_mutex_lock(&renderer_configuration);status=renderer_configure(r,renderer.socket);pthread_mutex_unlock(&renderer_configuration);snprintf(json,SB_UPNP_REPLY_MAX,"{\"enabled\":%s%s}",status==200&&!strcmp(r->text,"1")?"true":"false",status==200?"":",\"error\":\"renderer_configuration_failed\"");}
  else if(r->operation==SB_UPNP_SERVERS){
   pthread_mutex_lock(&state.mutex);size_t used=(size_t)snprintf(json,SB_UPNP_REPLY_MAX,"{\"revision\":%llu,\"servers\":[",(unsigned long long)state.registry.revision);unsigned count=0;
   for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++){const sbupnp_server *s=&state.registry.servers[i];if(!*s->udn)continue;
    used+=(size_t)snprintf(json+used,SB_UPNP_REPLY_MAX-used,"%s{\"udn\":",count++?",":"");append_string(json,SB_UPNP_REPLY_MAX,&used,s->udn);
    used+=(size_t)snprintf(json+used,SB_UPNP_REPLY_MAX-used,",\"name\":");append_string(json,SB_UPNP_REPLY_MAX,&used,s->name);
    used+=(size_t)snprintf(json+used,SB_UPNP_REPLY_MAX-used,",\"generation\":%llu}",(unsigned long long)s->generation);
   }
   snprintf(json+used,SB_UPNP_REPLY_MAX-used,"]}");pthread_mutex_unlock(&state.mutex);status=200;
  }else {sbupnp_server server={0};pthread_mutex_lock(&state.mutex);
   for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++)if(!strcmp(state.registry.servers[i].udn,r->server)){server=state.registry.servers[i];break;}
   pthread_mutex_unlock(&state.mutex);
   if(*server.udn){status=sbupnp_rpc(&server,r,json,SB_UPNP_REPLY_MAX);
    int current=0;pthread_mutex_lock(&state.mutex);for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++)if(!strcmp(state.registry.servers[i].udn,r->server)&&state.registry.servers[i].generation==r->generation)current=1;pthread_mutex_unlock(&state.mutex);
    if(!current){status=409;strcpy(json,"{\"error\":\"server_changed\"}");}
   }
   else {status=404;strcpy(json,"{\"error\":\"server_unavailable\"}");}
  }
  size_t n=strlen(json);sb_upnp_header((unsigned char *)out,r->request,r->session,status,(unsigned)n);
  /* A closed client discards completion. No worker owns playback intent. */
  struct pollfd p={op->fd,POLLOUT,0};if(poll(&p,1,100)>0)send(op->fd,out,n+40,MSG_NOSIGNAL);
  free(out);
 }
 pthread_mutex_lock(&state.mutex);op->done=1;pthread_mutex_unlock(&state.mutex);return NULL;
}
static int provision_token(const char *path)
{
 char tokenpath[128],temporary[160];if(strlen(path)>100)return 0;strcpy(tokenpath,path);char *slash=strrchr(tokenpath,'/');if(!slash)return 0;strcpy(slash+1,"token");snprintf(temporary,sizeof temporary,"%s.XXXXXX",tokenpath);
 unsigned char raw[32];int random=open("/dev/urandom",O_RDONLY);if(random<0)return 0;ssize_t n=read(random,raw,sizeof raw);close(random);if(n!=sizeof raw)return 0;
 uint64_t incarnation=0;for(unsigned i=0;i<6;i++)incarnation=(incarnation<<8)|raw[i];state.registry.revision=(incarnation<<4)|1u;
 char token[64];static const char hex[]="0123456789abcdef";for(unsigned i=0;i<32;i++){token[2*i]=hex[raw[i]>>4];token[2*i+1]=hex[raw[i]&15];}
 int fd=mkstemp(temporary);if(fd<0)return 0;int ok=write(fd,token,sizeof token)==sizeof token;close(fd);if(ok)ok=!rename(temporary,tokenpath);if(!ok)unlink(temporary);return ok;
}
static int daemon_run(const char *path,UpnpClient_Handle client,const char *bound_interface)
{
 int listener=sb_upnp_listen(path);if(listener<0){perror("UPnP IPC listen");return 1;}
 snprintf(renderer.socket,sizeof renderer.socket,"%s",path);
 if(!provision_token(path)){close(listener);unlink(path);return 1;}
 for(unsigned i=0;i<2;i++)operations[i].fd=-1;
 pthread_t discovery;if(pthread_create(&discovery,NULL,discovery_worker,NULL)){close(listener);unlink(path);return 1;}
 uint64_t search=millis()+300000,interface_check=millis()+5000;
 while(!stop){struct pollfd p[3]={{listener,POLLIN,0},{-1,0,0},{-1,0,0}};
  for(unsigned i=0;i<2;i++){
   operation *o=&operations[i];pthread_mutex_lock(&state.mutex);int done=o->done;pthread_mutex_unlock(&state.mutex);
   if(done){pthread_join(o->thread,NULL);close(o->fd);memset(o,0,sizeof *o);o->fd=-1;}
   p[i+1]=(struct pollfd){o->fd,o->started?0:POLLIN,0};
  }
  if(poll(p,3,100)<0&&errno!=EINTR)break;
  if(p[0].revents&POLLIN){int fd=sb_upnp_accept(listener);if(fd>=0){unsigned i;for(i=0;i<2;i++)if(operations[i].fd<0){operations[i].fd=fd;operations[i].accepted=millis();break;}if(i==2)close(fd);}}
  for(unsigned i=0;i<2;i++){operation *o=&operations[i];if(o->fd<0||o->started)continue;
   if(millis()-o->accepted>5000){close(o->fd);o->fd=-1;continue;}
   if(p[i+1].revents&(POLLIN|POLLHUP|POLLERR)){unsigned char wire[SB_UPNP_REQUEST_BYTES];ssize_t n=recv(o->fd,wire,sizeof wire,MSG_TRUNC);
    if(n<=0||!sb_upnp_decode(wire,(size_t)n,&o->request)){close(o->fd);o->fd=-1;continue;}
    o->started=1;if(pthread_create(&o->thread,NULL,request_worker,o)){close(o->fd);o->fd=-1;o->started=0;}
   }
  }
  if(millis()>=search){UpnpSearchAsync(client,3,"urn:schemas-upnp-org:device:MediaServer:1",NULL);search=millis()+300000;}
  if(millis()>=interface_check){
   struct ifaddrs *interfaces=NULL;int present=0;const char *address=UpnpGetServerIpAddress();
   if(!getifaddrs(&interfaces)){for(struct ifaddrs *p=interfaces;p;p=p->ifa_next)if(p->ifa_addr&&p->ifa_addr->sa_family==AF_INET){char ip[16];if(inet_ntop(AF_INET,&((struct sockaddr_in *)p->ifa_addr)->sin_addr,ip,sizeof ip)&&address&&!strcmp(ip,address)&&!strcmp(p->ifa_name,bound_interface)&&sbupnp_interface_allowed(p->ifa_name,p->ifa_flags,ip))present=1;}freeifaddrs(interfaces);}
   if(!present){fputs("sbupnp: interface address retired; supervisor rediscovery required\n",stderr);break;}
   interface_check=millis()+5000;
  }
 }
 stop=1;close(listener);pthread_join(discovery,NULL);
 for(unsigned i=0;i<2;i++){operation *o=&operations[i];if(o->started){shutdown(o->fd,SHUT_RDWR);pthread_join(o->thread,NULL);}if(o->fd>=0)close(o->fd);}
 renderer_shutdown();unlink(path);return 0;
}
