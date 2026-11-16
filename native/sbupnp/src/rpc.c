#define _POSIX_C_SOURCE 200809L
#include "rpc.h"
#include <upnp.h>
#include <upnptools.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct {char *out;size_t capacity,used;int valid;} writer;
static void emit(writer *w,const char *fmt,...){if(!w->valid)return;va_list ap;va_start(ap,fmt);int n=vsnprintf(w->out+w->used,w->capacity-w->used,fmt,ap);va_end(ap);if(n<0||(size_t)n>=w->capacity-w->used)w->valid=0;else w->used+=(size_t)n;}
static void string(writer *w,const char *s){emit(w,"\"");for(const unsigned char *p=(const unsigned char *)s;*p;p++){if(*p=='"'||*p=='\\')emit(w,"\\%c",*p);else if(*p<32)emit(w,"\\u%04x",*p);else emit(w,"%c",*p);}emit(w,"\"");}
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;}
static const char *text(IXML_Node *n,const char *key){return sbupnp_text(sbupnp_child(n,key));}
static int number(const char *s,unsigned *out){uint64_t n=0;if(!s||!*s)return 0;for(;*s;s++){if(*s<'0'||*s>'9'||n>(UINT32_MAX-(unsigned)(*s-'0'))/10)return 0;n=n*10+(unsigned)(*s-'0');}*out=(unsigned)n;return 1;}
static IXML_Document *soap(const sbupnp_server *s,const char *action,IXML_Document *request)
{
 void *h=NULL;IXML_Document *response=NULL;UpnpString *headers=UpnpString_new();
 DOMString serialized=ixmlPrintDocument(request);char *body=NULL,*buf=NULL;
 if(!headers||!serialized||!sbupnp_url(s->control,s->location))goto done;
 const char *fragment=serialized;
 if(!strncmp(fragment,"<?xml",5)){const char *end=strstr(fragment,"?>");if(!end)goto done;fragment=end+2;}
 size_t cap=strlen(fragment)+256;body=malloc(cap);buf=malloc(SBUPNP_XML_MAX+1);if(!body||!buf)goto done;
 int bytes=snprintf(body,cap,"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>%s</s:Body></s:Envelope>",fragment);
 if(bytes<0||(size_t)bytes>=cap)goto done;
 char hdr[512];snprintf(hdr,sizeof hdr,"SOAPAction: \"%s#%s\"\r\nConnection: close\r\n",s->service,action);UpnpString_set_String(headers,hdr);
 uint64_t deadline=now()+10000;
 if(UpnpOpenHttpConnection(s->control,&h,3)!=UPNP_E_SUCCESS||
    UpnpMakeHttpRequest(UPNP_HTTPMETHOD_POST,s->control,h,headers,"text/xml; charset=\"utf-8\"",bytes,2)!=UPNP_E_SUCCESS)goto done;
 size_t sent=(size_t)bytes;
 if(UpnpWriteHttpRequest(h,body,&sent,2)!=UPNP_E_SUCCESS||sent!=(size_t)bytes||UpnpEndHttpRequest(h,2)!=UPNP_E_SUCCESS)goto done;
 int length,status;char *mime;
 if(UpnpGetHttpResponse(h,NULL,&mime,&length,&status,2)!=UPNP_E_SUCCESS||status!=200||length>(int)SBUPNP_XML_MAX)goto done;
 size_t used=0;
 while(now()<deadline){size_t n=4096;if(n>SBUPNP_XML_MAX+1-used)n=SBUPNP_XML_MAX+1-used;
  if(!n||UpnpReadHttpResponse(h,buf+used,&n,1)!=UPNP_E_SUCCESS)goto done;
  used+=n;if(used>SBUPNP_XML_MAX)goto done;
  if(!n){buf[used]=0;response=sbupnp_xml(buf,used);break;}
 }
done:if(h)UpnpCloseHttpConnection(h);if(headers)UpnpString_delete(headers);if(serialized)ixmlFreeDOMString(serialized);free(body);free(buf);return response;
}
static IXML_Node *result(IXML_Document *d,const char *action,const char *service)
{
 IXML_Node *e=sbupnp_child((IXML_Node *)d,"Envelope");if(!e)return NULL;
 const char *ns=ixmlNode_getNamespaceURI(e);if(!ns||strcmp(ns,"http://schemas.xmlsoap.org/soap/envelope/"))return NULL;
 IXML_Node *b=sbupnp_child(e,"Body");if(!b)return NULL;
 char name[128];snprintf(name,sizeof name,"%sResponse",action);IXML_Node *r=sbupnp_child(b,name);
 if(!r)return NULL;
 ns=ixmlNode_getNamespaceURI(r);return ns&&!strcmp(ns,service)?r:NULL;
}
static int scalar(const sbupnp_server *s,const char *action,const char *key,char *out,size_t cap)
{
 IXML_Document *q=UpnpMakeAction(action,s->service,0,NULL),*d=NULL;int ok=0;if(!q)return 0;
 d=soap(s,action,q);if(d){IXML_Node *r=result(d,action,s->service);if(r){const char *v=text(r,key);if(strlen(v)<cap){strcpy(out,v);ok=1;}}}
 if(d)ixmlDocument_free(d);
 ixmlDocument_free(q);return ok;
}
static void item(writer *w,const sbupnp_item *i)
{
 emit(w,"{\"id\":");string(w,i->id);emit(w,",\"parent\":");string(w,i->parent);emit(w,",\"title\":");string(w,i->title);emit(w,",\"artist\":");string(w,i->artist);emit(w,",\"album\":");string(w,i->album);emit(w,",\"artwork\":");string(w,i->artwork);emit(w,",\"uri\":");string(w,i->uri);emit(w,",\"protocol\":");string(w,i->protocol);
 emit(w,",\"genre\":");string(w,i->genre);
 emit(w,",\"container\":%s,\"searchable\":%s,\"playable\":%s,\"track_number\":%u,\"duration_ms\":%llu,\"size\":%llu}",i->container?"true":"false",i->searchable?"true":"false",i->playable?"true":"false",i->track,(unsigned long long)i->duration_ms,(unsigned long long)i->size);
}
unsigned sbupnp_rpc(const sbupnp_server *s,const sb_upnp_request *r,char *out,size_t cap)
{
 writer w={out,cap,0,1};unsigned status=502;IXML_Document *q=NULL,*d=NULL;
 if(!s||r->generation!=s->generation){snprintf(out,cap,"{\"error\":\"server_changed\"}");return 409;}
 if(r->operation==SB_UPNP_CAPABILITIES){char search[2048],sort[2048];
  int a=scalar(s,"GetSearchCapabilities","SearchCaps",search,sizeof search),b=scalar(s,"GetSortCapabilities","SortCaps",sort,sizeof sort);
  emit(&w,"{\"search\":");string(&w,a?search:"");emit(&w,",\"sort\":");string(&w,b?sort:"");emit(&w,"}");return w.valid?200:502;
 }
 if(r->operation==SB_UPNP_UPDATE_ID){char update[32];unsigned n;if(!scalar(s,"GetSystemUpdateID","Id",update,sizeof update)||!number(update,&n))goto done;snprintf(out,cap,"{\"update_id\":%u}",n);return 200;}
 if(!*r->object||!r->limit){status=400;goto done;}
 char criteria[2048]="",sortcaps[2048],searchcaps[2048];int searching=r->operation==SB_UPNP_SEARCH;
 const char *action=searching?"Search":"Browse";
 if(searching){
  if(!scalar(s,"GetSearchCapabilities","SearchCaps",searchcaps,sizeof searchcaps)||!sbupnp_search(searchcaps,r->field,r->text,criteria,sizeof criteria)){status=400;goto done;}
  /* Search scope must itself be declared searchable by BrowseMetadata. */
  sb_upnp_request meta=*r;meta.operation=SB_UPNP_METADATA;meta.limit=1;meta.offset=0;meta.sort[0]=0;
  char *projection=malloc(SB_UPNP_REPLY_MAX);if(!projection)goto done;
  unsigned valid=sbupnp_rpc(s,&meta,projection,SB_UPNP_REPLY_MAX);int searchable=valid==200&&strstr(projection,"\"container\":true,\"searchable\":true");free(projection);if(!searchable){status=400;goto done;}
 }
 if(*r->sort){if((r->sort[0]!='+'&&r->sort[0]!='-')||strchr(r->sort,',')||!scalar(s,"GetSortCapabilities","SortCaps",sortcaps,sizeof sortcaps)||!sbupnp_capability(sortcaps,r->sort+1)){status=400;goto done;}}
 char offset[32],limit[32];snprintf(offset,sizeof offset,"%u",r->offset);snprintf(limit,sizeof limit,"%u",r->limit);
 const char *keys[]={searching?"ContainerID":"ObjectID",searching?"SearchCriteria":"BrowseFlag","Filter","StartingIndex","RequestedCount","SortCriteria"};
 const char *values[]={r->object,searching?criteria:(r->operation==SB_UPNP_METADATA?"BrowseMetadata":"BrowseDirectChildren"),"*",offset,limit,r->sort};
 for(unsigned i=0;i<6;i++)if(UpnpAddToAction(&q,action,s->service,keys[i],values[i])!=UPNP_E_SUCCESS)goto done;
 d=soap(s,action,q);if(!d)goto done;IXML_Node *response=result(d,action,s->service);if(!response)goto done;
 unsigned returned,total,update;
 if(!number(text(response,"NumberReturned"),&returned)||!number(text(response,"TotalMatches"),&total)||!number(text(response,"UpdateID"),&update)||returned>r->limit||r->offset>total||returned>total-r->offset||(!returned&&r->offset<total))goto done;
 sbupnp_item *items=calloc(r->limit,sizeof *items);if(!items)goto done;unsigned count=0;const char *xml=text(response,"Result");
 int valid=sbupnp_didl(xml,strlen(xml),s->location,items,r->limit,&count)&&count==returned;
 if(r->operation==SB_UPNP_METADATA && (count!=1 || strcmp(items[0].id,r->object)))valid=0;
 if(valid){emit(&w,"{\"offset\":%u,\"count\":%u,\"total\":%u,\"update_id\":%u,\"generation\":%llu,\"items\":[",r->offset,count,total,update,(unsigned long long)s->generation);for(unsigned i=0;i<count;i++){if(i)emit(&w,",");item(&w,&items[i]);}emit(&w,"]}");if(w.valid)status=200;}
 free(items);
done:if(q)ixmlDocument_free(q);if(d)ixmlDocument_free(d);if(status!=200)snprintf(out,cap,"{\"error\":\"%s\"}",status==400?"unsupported_request":"invalid_server_response");return status;
}
