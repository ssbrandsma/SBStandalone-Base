#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include <upnp.h>
#include <upnptools.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
extern int __real_ixmlParseBufferEx(const char *,IXML_Document **);

static int copy(char *out, size_t n, const char *s)
{
 if (!s || strlen(s)>=n) return 0;
 memcpy(out,s,strlen(s)+1); return 1;
}
static int address(const char *url, char host[16])
{
 const char *p; size_t n; struct in_addr a; unsigned long port=80;
 if (!url || strlen(url)>=SBUPNP_URL_MAX || strncmp(url,"http://",7)) return 0;
 for (p=url;*p;p++) if ((unsigned char)*p<=32 || (unsigned char)*p>=127 || *p=='\\' || *p=='#' || *p=='@') return 0;
 p=url+7; n=strcspn(p,":/");
 if (!n || n>15) return 0;
 memcpy(host,p,n);host[n]=0;
 if (inet_pton(AF_INET,host,&a)!=1) return 0;
 uint32_t ip=ntohl(a.s_addr);
 if (!((ip>>24)==10 || (ip>>20)==0xac1 || (ip>>16)==0xc0a8 || (ip>>16)==0xa9fe)) return 0;
 p+=n;
 if (*p==':') {
  char *end; p++; if (!isdigit((unsigned char)*p)) return 0;
  errno=0; port=strtoul(p,&end,10); if(errno || end==p || port<1 || port>65535) return 0; p=end;
 }
 if (*p && *p!='/') return 0;
 /* Known device management/IPC ports are never UPnP fetch destinations. */
 if (port==22 || port==23 || port==3483 || port==8765) return 0;
 if(port==80||port==443||port==9000){
  struct ifaddrs *interfaces=NULL;int own=1;
  if(!getifaddrs(&interfaces)){own=0;for(struct ifaddrs *v=interfaces;v;v=v->ifa_next)if(v->ifa_addr&&v->ifa_addr->sa_family==AF_INET&&((struct sockaddr_in *)v->ifa_addr)->sin_addr.s_addr==a.s_addr)own=1;freeifaddrs(interfaces);}
  if(own)return 0;
 }
 return 1;
}
int sbupnp_url(const char *url, const char *origin)
{
 char a[16],b[16];
 return address(url,a) && (!origin || (address(origin,b) && !strcmp(a,b)));
}
int sbupnp_resolve(const char *base, const char *rel, char *out, size_t cap)
{
 char *resolved=NULL;
 if (!base || !rel || UpnpResolveURL2(base,rel,&resolved)!=UPNP_E_SUCCESS) return 0;
 int ok=copy(out,cap,resolved) && sbupnp_url(out,base);free(resolved);return ok;
}
/* Reject declarations and excessive nesting before entering the recursive DOM
 * parser. Scan quotes/comments/CDATA so embedded markup cannot alter depth. */
static int utf8(const unsigned char *s, size_t n)
{
 for(size_t i=0;i<n;) {
  unsigned c=s[i++],v,need,min;
  if(c<128) {if(c<32 && c!=9 && c!=10 && c!=13) return 0;continue;}
  if(c>=194 && c<=223){v=c&31;need=1;min=128;}
  else if(c>=224 && c<=239){v=c&15;need=2;min=2048;}
  else if(c>=240 && c<=244){v=c&7;need=3;min=65536;}
  else return 0;
  if(i+need>n)return 0;
  while(need--){c=s[i++];if((c&192)!=128)return 0;v=(v<<6)|(c&63);}
  if(v<min || v>0x10ffff || (v>=0xd800 && v<=0xdfff) || v==0xfffe || v==0xffff)return 0;
 }
 return 1;
}
IXML_Document *sbupnp_xml(const char *s,size_t n)
{
 if(!s || !n || n>SBUPNP_XML_MAX || memchr(s,0,n) || !utf8((const unsigned char *)s,n))return NULL;
 char *buf=malloc(n+1);if(!buf)return NULL;memcpy(buf,s,n);buf[n]=0;
 unsigned depth=0,nodes=0; int valid=1;
 for(char *p=buf;*p && valid;p++) {
  if(*p!='<')continue;
  if(!strncmp(p,"<!--",4)){char *q=strstr(p+4,"-->");if(!q){valid=0;break;}p=q+2;continue;}
  if(!strncmp(p,"<![CDATA[",9)){char *q=strstr(p+9,"]]>");if(!q){valid=0;break;}p=q+2;continue;}
  if(p[1]=='!'){valid=0;break;}
  if(p[1]=='?'){char *q=strstr(p+2,"?>");if(!q){valid=0;break;}p=q+1;continue;}
  char *q=p+1;int quote=0;
  for(;*q;q++){if(quote){if(*q==quote)quote=0;}else if(*q=='\'' || *q=='"')quote=*q;else if(*q=='>')break;}
  if(!*q){valid=0;break;}
  if(p[1]=='/'){if(!depth){valid=0;break;}depth--;}
  else {if(++nodes>8192){valid=0;break;}if(q[-1]!='/')depth++;if(depth>32){valid=0;break;}}
  p=q;
 }
 IXML_Document *doc=NULL;
 if(valid && !depth && __real_ixmlParseBufferEx(buf,&doc)!=IXML_SUCCESS)doc=NULL;
 free(buf);return doc;
}
static const char *local(IXML_Node *n)
{
 const char *s=ixmlNode_getNodeName(n),*p=s?strrchr(s,':'):NULL;return p?p+1:s?s:"";
}
IXML_Node *sbupnp_child(IXML_Node *n,const char *name)
{
 for(IXML_Node *p=ixmlNode_getFirstChild(n);p;p=ixmlNode_getNextSibling(p))
  if(ixmlNode_getNodeType(p)==eELEMENT_NODE && !strcmp(local(p),name))return p;
 return NULL;
}
const char *sbupnp_text(IXML_Node *n)
{
 if(!n)return "";
 IXML_Node *p=ixmlNode_getFirstChild(n);
 if(!p)return "";
 unsigned t=ixmlNode_getNodeType(p);
 return (t==eTEXT_NODE || t==eCDATA_SECTION_NODE)?ixmlNode_getNodeValue(p):"";
}
static const char *field(IXML_Node *n,const char *s){return sbupnp_text(sbupnp_child(n,s));}
static const char *attr(IXML_Node *n,const char *s)
{
 const char *v=ixmlElement_getAttribute((IXML_Element *)n,s);return v?v:"";
}
static int version(const char *s,const char *prefix)
{
 size_t n=strlen(prefix);if(strncmp(s,prefix,n))return 0;
 s+=n;if(!*s || *s=='0')return 0;
 unsigned v=0;for(;*s;s++){if(*s<'0'||*s>'9'||v>100)return 0;v=v*10+(unsigned)(*s-'0');}return v>0 && v<=100;
}
static IXML_Node *device(IXML_Node *n,const char *udn)
{
 if(!strcmp(local(n),"device") && !strcmp(field(n,"UDN"),udn))return n;
 for(IXML_Node *p=ixmlNode_getFirstChild(n);p;p=ixmlNode_getNextSibling(p)){
  IXML_Node *r=device(p,udn);if(r)return r;
 }return NULL;
}
int sbupnp_description(const char *xml,size_t len,const char *location,const char *udn,sbupnp_server *out)
{
 if(!sbupnp_url(location,NULL) || !udn || !*udn)return 0;
 IXML_Document *doc=sbupnp_xml(xml,len);if(!doc)return 0;
 sbupnp_server s={0};int ok=0;
 IXML_Node *root=sbupnp_child((IXML_Node *)doc,"root"),*d=device((IXML_Node *)doc,udn);
 if(!root || !d || !version(field(d,"deviceType"),"urn:schemas-upnp-org:device:MediaServer:"))goto done;
 const char *base=field(root,"URLBase");if(!*base)base=location;
 if(!sbupnp_url(base,location))goto done;
 if(!copy(s.udn,sizeof s.udn,udn)||!copy(s.name,sizeof s.name,field(d,"friendlyName"))||
    !copy(s.type,sizeof s.type,field(d,"deviceType"))||!copy(s.location,sizeof s.location,location))goto done;
 IXML_Node *list=sbupnp_child(d,"serviceList");if(!list)goto done;
 for(IXML_Node *p=ixmlNode_getFirstChild(list);p;p=ixmlNode_getNextSibling(p)){
  if(strcmp(local(p),"service") || !version(field(p,"serviceType"),"urn:schemas-upnp-org:service:ContentDirectory:"))continue;
  if(!copy(s.service,sizeof s.service,field(p,"serviceType")) || !*field(p,"controlURL") ||
     !sbupnp_resolve(base,field(p,"controlURL"),s.control,sizeof s.control))goto done;
  const char *event=field(p,"eventSubURL");
  if(*event && !sbupnp_resolve(base,event,s.event,sizeof s.event))goto done;
  ok=1;*out=s;break;
 }
done:ixmlDocument_free(doc);return ok;
}
static uint64_t number(const char *s)
{
 uint64_t n=0;if(!s || !*s)return 0;
 for(;*s;s++){if(*s<'0'||*s>'9'||n>(UINT64_MAX-(unsigned)(*s-'0'))/10)return 0;n=n*10+(unsigned)(*s-'0');}return n;
}
static uint64_t duration(const char *s)
{
 unsigned parts[3]={0},ms=0;
 for(unsigned i=0;i<3;i++){
  unsigned digits=0;
  while(isdigit((unsigned char)*s)){
   if(++digits>5)return 0;
   parts[i]=parts[i]*10+(unsigned)(*s++-'0');
  }
  if(!digits || (i==0?parts[i]>10000:parts[i]>59))return 0;
  if(i<2 && *s++!=':')return 0;
 }
 if(*s=='.'){s++;unsigned scale=100;if(!isdigit((unsigned char)*s))return 0;while(isdigit((unsigned char)*s)){if(scale){ms+=(unsigned)(*s-'0')*scale;scale/=10;}s++;}}
 if(*s)return 0;
 return ((uint64_t)parts[0]*3600+parts[1]*60+parts[2])*1000+ms;
}
int sbupnp_didl(const char *xml,size_t len,const char *origin,sbupnp_item *items,unsigned cap,unsigned *count)
{
 if(!items||!count||!cap||cap>SBUPNP_PAGE_MAX||!sbupnp_url(origin,NULL))return 0;
 *count=0;IXML_Document *doc=sbupnp_xml(xml,len);if(!doc)return 0;
 IXML_Node *root=sbupnp_child((IXML_Node *)doc,"DIDL-Lite");int ok=0;
 if(!root)goto done;
 for(IXML_Node *p=ixmlNode_getFirstChild(root);p;p=ixmlNode_getNextSibling(p)){
  if(strcmp(local(p),"container") && strcmp(local(p),"item"))continue;
  if(*count>=cap)goto done;
  sbupnp_item *v=&items[(*count)++];memset(v,0,sizeof *v);
  v->container=!strcmp(local(p),"container");v->searchable=!strcmp(attr(p,"searchable"),"1") || !strcmp(attr(p,"searchable"),"true");
  if(!*attr(p,"id")||!copy(v->id,sizeof v->id,attr(p,"id"))||!copy(v->parent,sizeof v->parent,attr(p,"parentID")))goto done;
  copy(v->title,sizeof v->title,field(p,"title"));copy(v->artist,sizeof v->artist,field(p,"artist"));
  if(!*v->artist)copy(v->artist,sizeof v->artist,field(p,"creator"));
  copy(v->album,sizeof v->album,field(p,"album"));copy(v->genre,sizeof v->genre,field(p,"genre"));
  copy(v->class_name,sizeof v->class_name,field(p,"class"));v->track=(unsigned)number(field(p,"originalTrackNumber"));
  const char *art=field(p,"albumArtURI");if(*art)sbupnp_resolve(origin,art,v->artwork,sizeof v->artwork);
  if(v->container || strncmp(v->class_name,"object.item.audioItem",21) || (v->class_name[21] && v->class_name[21]!='.'))continue;
  for(IXML_Node *r=ixmlNode_getFirstChild(p);r;r=ixmlNode_getNextSibling(r)){
   if(strcmp(local(r),"res"))continue;
   const char *proto=attr(r,"protocolInfo"),*mime;
   if(strncmp(proto,"http-get:",9))continue;
   mime=strchr(proto+9,':');if(!mime)continue;mime++;
   if(strncmp(mime,"audio/mpeg:",11) && strncmp(mime,"audio/mp3:",10))continue;
   if(!sbupnp_resolve(origin,sbupnp_text(r),v->uri,sizeof v->uri) || !copy(v->protocol,sizeof v->protocol,proto))continue;
   v->duration_ms=duration(attr(r,"duration"));v->size=number(attr(r,"size"));
   v->bitrate=(unsigned)number(attr(r,"bitrate"));v->rate=(unsigned)number(attr(r,"sampleFrequency"));v->channels=(unsigned)number(attr(r,"nrAudioChannels"));
   v->playable=1;break;
  }
 }ok=1;
done:ixmlDocument_free(doc);if(!ok)*count=0;return ok;
}
int sbupnp_capability(const char *caps,const char *field_name)
{
 if(!caps||!field_name||!*field_name)return 0;
 for(const char *p=caps;*p;){const char *e=strchr(p,',');if(!e)e=p+strlen(p);
  while(p<e&&isspace((unsigned char)*p))p++;
  const char *end=e;while(end>p&&isspace((unsigned char)end[-1]))end--;
  if((end-p==1&&*p=='*') || ((size_t)(end-p)==strlen(field_name)&&!strncmp(p,field_name,(size_t)(end-p))))return 1;
  p=*e?e+1:e;
 }return 0;
}
int sbupnp_search(const char *caps,const char *f,const char *text,char *out,size_t cap)
{
 static const char *allowed[]={"dc:title","upnp:artist","upnp:album","upnp:genre"};int found=0;
 if(!f||!text||!*text||strlen(text)>256 || !utf8((const unsigned char *)text,strlen(text)))return 0;
 for(unsigned i=0;i<4;i++)if(!strcmp(f,allowed[i]))found=1;
 if(!found||!sbupnp_capability(caps,f))return 0;
 int n=snprintf(out,cap,"%s contains \"",f);if(n<0||(size_t)n>=cap)return 0;size_t used=(size_t)n;
 for(const char *p=text;*p;p++){
  if((unsigned char)*p<32)return 0;
  if(*p=='"'||*p=='\\'){if(used+1>=cap)return 0;out[used++]='\\';}
  if(used+1>=cap)return 0;
  out[used++]=*p;
 }if(used+2>cap)return 0;out[used++]='"';out[used]=0;return 1;
}
void sbupnp_registry_expire(sbupnp_registry *r,uint64_t now)
{
 for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++)if(*r->servers[i].udn && r->servers[i].expires<=now){memset(&r->servers[i],0,sizeof r->servers[i]);r->revision++;}
}
void sbupnp_registry_remove(sbupnp_registry *r,const char *udn)
{
 for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++)if(!strcmp(r->servers[i].udn,udn)&&*udn){memset(&r->servers[i],0,sizeof r->servers[i]);r->revision++;}
}
int sbupnp_registry_put(sbupnp_registry *r,const sbupnp_server *s,uint64_t now,unsigned ttl)
{
 if(!*s->udn||!ttl||ttl>86400||UINT64_MAX-now<(uint64_t)ttl*1000)return 0;
 sbupnp_registry_expire(r,now);unsigned slot=SBUPNP_SERVERS_MAX;
 for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++)if(!strcmp(r->servers[i].udn,s->udn)){slot=i;break;}
 if(slot==SBUPNP_SERVERS_MAX)for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++)if(!*r->servers[i].udn){slot=i;break;}
 if(slot==SBUPNP_SERVERS_MAX)return 0;
 sbupnp_server *old=&r->servers[slot];uint64_t gen=old->generation;
 if(!*old->udn || strcmp(old->control,s->control)||strcmp(old->location,s->location)||strcmp(old->name,s->name))gen=++r->revision;
 *old=*s;old->generation=gen;old->expires=now+(uint64_t)ttl*1000;return 1;
}
