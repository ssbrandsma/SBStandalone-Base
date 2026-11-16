#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include "netif.h"
#include <net/if.h>
#include <upnp.h>
#include <upnptools.h>
#include <UpnpDiscovery.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {char udn[256],location[SBUPNP_URL_MAX],peer[64];unsigned ttl;int remove;} notice;
static struct {
 pthread_mutex_t mutex;
 notice notices[32];unsigned head,count,dropped;
 sbupnp_registry registry;
} state={.mutex=PTHREAD_MUTEX_INITIALIZER};
static volatile sig_atomic_t stop;
static void interrupted(int sig){(void)sig;stop=1;}
static uint64_t millis(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;}
static int event(Upnp_EventType type,void *data,void *cookie)
{
 (void)cookie;
 if(type!=UPNP_DISCOVERY_ADVERTISEMENT_ALIVE && type!=UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE && type!=UPNP_DISCOVERY_SEARCH_RESULT)return 0;
 const UpnpDiscovery *d=data;if(UpnpDiscovery_get_ErrCode(d)!=UPNP_E_SUCCESS)return 0;
 const char *id=UpnpDiscovery_get_DeviceID_cstr(d),*url=UpnpDiscovery_get_Location_cstr(d);
 const char *kind=UpnpDiscovery_get_DeviceType_cstr(d);
 int remove=type==UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE;
 if(!id||!*id||strlen(id)>=256 || (!remove && (!kind || strncmp(kind,"urn:schemas-upnp-org:device:MediaServer:",40) || !sbupnp_url(url,NULL))))return 0;
 const struct sockaddr_storage *remote=UpnpDiscovery_get_DestAddr(d);
 char ip[INET_ADDRSTRLEN],peer[64];
 if(!remote || remote->ss_family!=AF_INET || !inet_ntop(AF_INET,&((const struct sockaddr_in *)remote)->sin_addr,ip,sizeof ip))return 0;
 /* The origin represents the SSDP sender, not an HTTP management service. */
 snprintf(peer,sizeof peer,"http://%s:1900/",ip);
 if(!sbupnp_url(peer,NULL) || (!remove && !sbupnp_url(url,peer)))return 0;
 int ttl=UpnpDiscovery_get_Expires(d);if(!remove && (ttl<=0 || ttl>86400))return 0;
 pthread_mutex_lock(&state.mutex);
 unsigned slot=32;
 for(unsigned i=0;i<state.count;i++){unsigned k=(state.head+i)%32;if(!strcmp(state.notices[k].udn,id)){slot=k;break;}}
 if(slot==32 && state.count<32){slot=(state.head+state.count)%32;state.count++;}
 if(slot<32){notice *n=&state.notices[slot];memset(n,0,sizeof *n);strcpy(n->udn,id);strcpy(n->peer,peer);if(!remove)strcpy(n->location,url);n->remove=remove;n->ttl=(unsigned)ttl;}
 else state.dropped++;
 pthread_mutex_unlock(&state.mutex);return 0;
}
static int pop(notice *n)
{
 int ok=0;pthread_mutex_lock(&state.mutex);
 if(state.count){*n=state.notices[state.head];state.head=(state.head+1)%32;state.count--;ok=1;}
 pthread_mutex_unlock(&state.mutex);return ok;
}
static char *description(const char *url,size_t *len)
{
 void *h=NULL;char *mime=NULL;int length=0,status=0;
 if(UpnpOpenHttpGet(url,&h,&mime,&length,&status,5)!=UPNP_E_SUCCESS)return NULL;
 char *buf=NULL;size_t used=0;uint64_t deadline=millis()+5000;
 if(status!=200 || length>(int)SBUPNP_XML_MAX)goto done;
 buf=malloc(SBUPNP_XML_MAX+1);if(!buf)goto done;
 while(!stop && millis()<deadline){size_t n=4096;if(n>SBUPNP_XML_MAX+1-used)n=SBUPNP_XML_MAX+1-used;
  if(!n || UpnpReadHttpGet(h,buf+used,&n,1)!=UPNP_E_SUCCESS){free(buf);buf=NULL;break;}
  used+=n;if(used>SBUPNP_XML_MAX){free(buf);buf=NULL;break;}if(!n){buf[used]=0;*len=used;goto done;}
 }
 free(buf);buf=NULL;
done:UpnpCloseHttpGet(h);return buf;
}
static void reconcile(notice *n)
{
 if(n->remove){
  for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++)if(!strcmp(state.registry.servers[i].udn,n->udn) && sbupnp_url(state.registry.servers[i].location,n->peer)){sbupnp_registry_remove(&state.registry,n->udn);break;}
  return;
 }
 for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++){
  sbupnp_server *s=&state.registry.servers[i];
  if(!strcmp(s->udn,n->udn) && !strcmp(s->location,n->location)){
   s->expires=millis()+(uint64_t)n->ttl*1000;return;
  }
 }
 size_t length=0;char *xml=description(n->location,&length);if(!xml)return;
 sbupnp_server s;
 if(sbupnp_description(xml,length,n->location,n->udn,&s))sbupnp_registry_put(&state.registry,&s,millis(),n->ttl);
 free(xml);
}
static void json(const char *s)
{
 putchar('"');for(const unsigned char *p=(const unsigned char *)s;*p;p++){
  if(*p=='"'||*p=='\\'){putchar('\\');putchar(*p);}else if(*p<32)printf("\\u%04x",*p);else putchar(*p);
 }putchar('"');
}
static void servers(void)
{
 printf("{\"revision\":%llu,\"servers\":[",(unsigned long long)state.registry.revision);int comma=0;
 for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++){const sbupnp_server *s=&state.registry.servers[i];if(!*s->udn)continue;
  if(comma++)putchar(',');
  printf("{\"udn\":");json(s->udn);printf(",\"name\":");json(s->name);printf(",\"generation\":%llu}",(unsigned long long)s->generation);
 }puts("]}");
}
static int action(UpnpClient_Handle client,const sbupnp_server *s,const char *name,IXML_Document *request,IXML_Document **response)
{
 int result=UpnpSendAction(client,s->control,s->service,NULL,request,response);
 if(result!=UPNP_E_SUCCESS)fprintf(stderr,"%s failed: %d\n",name,result);
 return result==UPNP_E_SUCCESS;
}
static int browse(UpnpClient_Handle client,const sbupnp_server *s,const char *object,unsigned offset)
{
 IXML_Document *req=NULL,*res=NULL;char index[32];snprintf(index,sizeof index,"%u",offset);
 const char *keys[]={"ObjectID","BrowseFlag","Filter","StartingIndex","RequestedCount","SortCriteria"};
 const char *values[]={object,"BrowseDirectChildren","*",index,"50",""};int ok=0;
 for(unsigned i=0;i<6;i++)if(UpnpAddToAction(&req,"Browse",s->service,keys[i],values[i])!=UPNP_E_SUCCESS)goto done;
 if(!action(client,s,"Browse",req,&res))goto done;
 IXML_Node *r=ixmlNode_getFirstChild((IXML_Node *)res);if(!r)goto done;
 const char *xml=sbupnp_text(sbupnp_child(r,"Result"));
 sbupnp_item *items=calloc(SBUPNP_PAGE_MAX,sizeof *items);unsigned count=0;if(!items)goto done;
 if(sbupnp_didl(xml,strlen(xml),s->location,items,SBUPNP_PAGE_MAX,&count)){
  printf("{\"offset\":%u,\"total\":",offset);json(sbupnp_text(sbupnp_child(r,"TotalMatches")));printf(",\"update_id\":");json(sbupnp_text(sbupnp_child(r,"UpdateID")));printf(",\"items\":[");
  for(unsigned i=0;i<count;i++){sbupnp_item *v=&items[i];if(i)putchar(',');printf("{\"id\":");json(v->id);printf(",\"title\":");json(v->title);printf(",\"artist\":");json(v->artist);printf(",\"album\":");json(v->album);printf(",\"container\":%s,\"compatible_mp3\":%s}",v->container?"true":"false",v->playable?"true":"false");}
  puts("]}");ok=1;
 }free(items);
done:if(req)ixmlDocument_free(req);if(res)ixmlDocument_free(res);return ok;
}
#include "renderer.inc"
#include "daemon.inc"
int main(int argc,char **argv)
{
 const char *iface=NULL,*udn=NULL,*object="0",*socket_path=SB_UPNP_SOCKET;unsigned seconds=5,offset=0;int daemon=0;
 for(int i=1;i<argc;i++){
  if(!strcmp(argv[i],"--version")){puts("sbupnp 0.3.1; pupnp 22.2.2; IPC version 1");return 0;}
  if(!strcmp(argv[i],"--daemon")){daemon=1;continue;}
  if(i+1>=argc){fprintf(stderr,"Expected option value\n");return 2;}
  const char *key=argv[i],*value=argv[++i];
  if(!strcmp(key,"--interface"))iface=value;
  else if(!strcmp(key,"--socket"))socket_path=value;
  else if(!strcmp(key,"--browse"))udn=value;
  else if(!strcmp(key,"--object")){if(strlen(value)>=512)return 2;object=value;}
  else if(!strcmp(key,"--seconds")||!strcmp(key,"--offset")){
   char *end;unsigned long n=strtoul(value,&end,10);if(!*value||*end||*value=='-'||n>1000000)return 2;
   if(!strcmp(key,"--seconds")){if(n<1||n>60)return 2;seconds=(unsigned)n;}else offset=(unsigned)n;
  }else return 2;
 }
 signal(SIGINT,interrupted);signal(SIGTERM,interrupted);
 char lan[IF_NAMESIZE];
 if(!sbupnp_choose_interface(iface,lan,sizeof lan)){fputs("sbupnp: no eligible LAN interface; awaiting supervisor retry\n",stderr);return 1;}
 iface=lan;
 int result=UpnpInit2(iface,0);if(result!=UPNP_E_SUCCESS){fprintf(stderr,"UpnpInit2: %d\n",result);return 1;}
 UpnpSetMaxContentLength(SBUPNP_XML_MAX);
 UpnpClient_Handle client;
 if(UpnpRegisterClient(event,NULL,&client)!=UPNP_E_SUCCESS){UpnpFinish();return 1;}
 UpnpSearchAsync(client,2,"urn:schemas-upnp-org:device:MediaServer:1",NULL);
 if(daemon){int status=daemon_run(socket_path,client,iface);UpnpUnRegisterClient(client);UpnpFinish();return status;}
 uint64_t deadline=millis()+(uint64_t)seconds*1000;
 while(!stop&&millis()<deadline){notice n;if(pop(&n))reconcile(&n);else{struct timespec t={0,50000000};nanosleep(&t,NULL);}sbupnp_registry_expire(&state.registry,millis());}
 int status=0;
 if(udn){status=1;int found=0;for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++)if(!strcmp(state.registry.servers[i].udn,udn)){found=1;status=browse(client,&state.registry.servers[i],object,offset)?0:1;break;}if(!found)fprintf(stderr,"Server not discovered on %s: %s\n",UpnpGetServerIpAddress(),udn);}
 else servers();
 UpnpUnRegisterClient(client);UpnpFinish();return status;
}
