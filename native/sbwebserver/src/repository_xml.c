/* Bounded SAX adapter for the actual extensions/applets/applet format. */
#include "repository_xml.h"
#include "../third_party/yxml/yxml.h"
#include "../../common/public_address.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *e,size_t n,const char *s){snprintf(e,n,"%s",s);return -1;}
int repo_xml_url(const char *u,char *e,size_t n){
 if(!u||strlen(u)>512)return fail(e,n,"URL exceeds 512 bytes");
 const char *p=!strncmp(u,"https://",8)?u+8:!strncmp(u,"http://",7)?u+7:NULL;
 if(!p)return fail(e,n,"Only HTTP and HTTPS URLs are supported");
 for(const unsigned char *s=(const unsigned char *)u;*s;s++)if(*s<=32||*s>=127||*s=='\\'||*s=='#')return fail(e,n,"Unsafe URL character");
 const char *end=p+strcspn(p,"/?");size_t len=(size_t)(end-p);char host[256];if(!len||len>=sizeof host)return fail(e,n,"Invalid URL authority");
 memcpy(host,p,len);host[len]=0;if(strchr(host,'@')||strchr(host,'['))return fail(e,n,"Credentials and IPv6 URL literals are not supported");
 char *port=strchr(host,':');if(port){*port++=0;if(strcmp(port,"80")&&strcmp(port,"443"))return fail(e,n,"Repositories use public ports 80/443 only");}
 if(!host[0]||host[0]=='.'||host[strlen(host)-1]=='.')return fail(e,n,"Invalid hostname");
 for(char *s=host;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='.'&&*s!='-')return fail(e,n,"Invalid hostname");
 struct in_addr ip;if(inet_pton(AF_INET,host,&ip)==1){if(!sb_public_ipv4((const unsigned char *)&ip))return fail(e,n,"Protected network address");}
 else {int numeric=1;for(char *s=host;*s;s++)if(!isdigit((unsigned char)*s)&&*s!='.')numeric=0;
  if(numeric||!strchr(host,'.')||strstr(host,".localhost")||strstr(host,".local")||strstr(host,".internal"))return fail(e,n,"Public DNS hostname required");}
 return 0;
}
int repo_xml_resolve(const char *base,const char *ref,char *out,size_t cap){
 char e[100],joined[1600],result[1600];if(!ref||!*ref)return -1;
 if(!strncmp(ref,"http://",7)||!strncmp(ref,"https://",8)){if(repo_xml_url(ref,e,sizeof e)||strlen(ref)>=cap)return -1;strcpy(out,ref);return 0;}
 if(!base||repo_xml_url(base,e,sizeof e))return -1;
 const char *scheme=strstr(base,"://"),*authority=scheme+3,*path=authority+strcspn(authority,"/?");
 size_t origin=(size_t)(path-base);
 if(!strncmp(ref,"//",2))snprintf(joined,sizeof joined,"%.*s:%s",(int)(scheme-base),base,ref);
 else if(*ref=='/')snprintf(joined,sizeof joined,"%.*s%s",(int)origin,base,ref);
 else {const char *query=strchr(base,'?');size_t end=query?(size_t)(query-base):strlen(base);
  if(*ref=='?')snprintf(joined,sizeof joined,"%.*s%s",(int)end,base,ref);
  else {size_t dir=end;while(dir>origin&&base[dir-1]!='/')dir--;if(dir==origin)snprintf(joined,sizeof joined,"%.*s/%s",(int)origin,base,ref);else snprintf(joined,sizeof joined,"%.*s%s",(int)dir,base,ref);}}
 scheme=strstr(joined,"://");if(!scheme)return -1;path=scheme+3+strcspn(scheme+3,"/?");origin=(size_t)(path-joined);
 snprintf(result,sizeof result,"%.*s",(int)origin,joined);size_t used=origin;
 const char *query=strchr(path,'?');size_t plen=query?(size_t)(query-path):strlen(path);size_t starts[128];unsigned depth=0;
 for(size_t i=0;i<plen;){while(i<plen&&path[i]=='/')i++;size_t start=i;while(i<plen&&path[i]!='/')i++;size_t len=i-start;if(!len||(len==1&&path[start]=='.'))continue;
  if(len==2&&!memcmp(path+start,"..",2)){if(depth)used=starts[--depth];continue;}
  if(depth==128||used+len+2>=sizeof result)return -1;starts[depth++]=used;result[used++]='/';memcpy(result+used,path+start,len);used+=len;result[used]=0;
 }
 if(used==origin|| (plen&&path[plen-1]=='/'))result[used++]='/';result[used]=0;
 if(query){if(used+strlen(query)>=sizeof result)return -1;strcpy(result+used,query);}
 if(strlen(result)>=cap||repo_xml_url(result,e,sizeof e))return -1;strcpy(out,result);return 0;
}
static int utf8(const unsigned char *p,size_t n){
 for(size_t i=0;i<n;){unsigned c=p[i++],v=c,min=0,k=0;if(c>=128){if(c>=0xc2&&c<=0xdf){v=c&31;k=1;min=128;}else if(c>=0xe0&&c<=0xef){v=c&15;k=2;min=2048;}else if(c>=0xf0&&c<=0xf4){v=c&7;k=3;min=65536;}else return 0;
  if(i+k>n)return 0;while(k--){c=p[i++];if((c&192)!=128)return 0;v=(v<<6)|(c&63);}if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return 0;}
  if((v<32&&v!=9&&v!=10&&v!=13)||v==0xfffe||v==0xffff)return 0;
 }return 1;
}
static const char *local(const char *s){const char *p=strrchr(s,':');return p?p+1:s;}
static char *field(RepoApplet *a,const char *s,size_t *cap){
#define F(k,f) if(!strcmp(s,k)){*cap=sizeof a->f;return a->f;}
 F("name",name) F("title",title) F("version",version) F("target",target)
 F("minTarget",min_target_version) F("maxTarget",max_target_version)
 F("url",url) F("sha",sha) F("desc",desc) F("changes",changes) F("creator",creator) F("email",email)
#undef F
 return NULL;
}
static void trim(char *s){size_t n=strlen(s);while(n&&isspace((unsigned char)s[n-1]))s[--n]=0;size_t first=0;while(isspace((unsigned char)s[first]))first++;if(first)memmove(s,s+first,n-first+1);}
static int valid_bound(const char *s){if(!*s)return 1;for(;*s;s++)if(!isalnum((unsigned char)*s)&&!strchr("._+-*",*s))return 0;return 1;}
int repo_xml_parse(const char *xml,size_t length,const char *base,RepoApplet *out,int *count,RepoXmlStats *stats,char *error,size_t n){
 *count=0;memset(stats,0,sizeof *stats);
 if(!length||length>REPO_XML_LIMIT)return fail(error,n,"XML exceeds 256 KiB or is empty");
 if(!utf8((const unsigned char *)xml,length))return fail(error,n,"XML must contain valid UTF-8/XML characters");
 /* yxml never performs external I/O; reject declarations rather than ignoring DTDs. */
 for(size_t i=0;i+9<=length;i++)if(!memcmp(xml+i,"<!DOCTYPE",9)||!memcmp(xml+i,"<!ENTITY",8))return fail(error,n,"DTD/entity declarations are forbidden");
 yxml_t x;char stack[8192],elements[24][96],attr[96],value[1024],text[1024],lang[16];
 char attributes[32][96];unsigned attrs=0,depth=0,applet_depth=0,text_depth=0;int bad=0,root=0;RepoApplet item;size_t vlen=0,tlen=0;
 yxml_init(&x,stack,sizeof stack);memset(&item,0,sizeof item);attr[0]=text[0]=lang[0]=0;
 for(size_t pos=0;pos<length;pos++){
  yxml_ret_t ev=yxml_parse(&x,(unsigned char)xml[pos]);if(ev<0)return fail(error,n,"Malformed XML or parser resource limit exceeded");
  if(ev==YXML_ELEMSTART){
   if(depth==24||strlen(x.elem)>=96)return fail(error,n,"XML nesting/name limit exceeded");
   snprintf(elements[depth],sizeof elements[depth],"%s",local(x.elem));depth++;attrs=0;
   if(depth==1){if(strcmp(elements[0],"extensions"))return fail(error,n,"Expected extensions document root");root=1;}
   if(!strcmp(local(x.elem),"include")&&strchr(x.elem,':'))return fail(error,n,"XInclude is not supported");
   if(depth==3&&!strcmp(elements[1],"applets")&&!strcmp(elements[2],"applet")){
    memset(&item,0,sizeof item);strcpy(item.target,"*");strcpy(item.min_target_version,"0");strcpy(item.max_target_version,"*");bad=0;applet_depth=depth;stats->discovered++;
    if(stats->discovered>REPO_XML_ITEMS)return fail(error,n,"Repository exceeds 64 applet entries");
   }
   if(applet_depth&&depth==applet_depth+1){text_depth=depth;tlen=0;text[0]=lang[0]=0;}
  }else if(ev==YXML_ATTRSTART){
   if(attrs==32||strlen(x.attr)>=96)return fail(error,n,"XML attribute limit exceeded");
   for(unsigned a=0;a<attrs;a++)if(!strcmp(attributes[a],x.attr))return fail(error,n,"Duplicate XML attribute");
   snprintf(attributes[attrs++],96,"%s",x.attr);snprintf(attr,sizeof attr,"%s",local(x.attr));vlen=0;value[0]=0;
  }else if(ev==YXML_ATTRVAL){size_t add=strlen(x.data);if(vlen+add>=sizeof value){if(applet_depth)bad=1;else return fail(error,n,"XML attribute too long");}else {memcpy(value+vlen,x.data,add+1);vlen+=add;}}
  else if(ev==YXML_ATTREND){
   if(applet_depth&&depth==applet_depth){size_t cap=0;char *f=field(&item,attr,&cap);if(f){if(vlen>=cap)bad=1;else strcpy(f,value);}}
   else if(text_depth==depth&&!strcmp(attr,"lang"))snprintf(lang,sizeof lang,"%.15s",value);
  }else if(ev==YXML_CONTENT&&text_depth&&depth>=text_depth){size_t add=strlen(x.data);if(tlen+add>=sizeof text)bad=1;else {memcpy(text+tlen,x.data,add+1);tlen+=add;}}
  else if(ev==YXML_ELEMEND){
   if(text_depth==depth&&applet_depth){size_t cap=0;char *f=field(&item,elements[depth-1],&cap);trim(text);if(f&&(!*f||!lang[0]||!strcmp(lang,"EN")||!strcmp(lang,"en"))){if(strlen(text)>=cap)bad=1;else strcpy(f,text);}text_depth=0;}
   if(applet_depth==depth){
    char resolved[513],why[160]="Invalid or oversized applet metadata";trim(item.title);if(!item.title[0])snprintf(item.title,sizeof item.title,"%s",item.name);
    for(char *s=item.sha;*s;s++)*s=(char)tolower((unsigned char)*s);
    if(!bad&&repo_xml_resolve(base,item.url,resolved,sizeof resolved))snprintf(why,sizeof why,"Package URL is unsafe or relative URL needs a base");
    else if(!bad){snprintf(item.url,sizeof item.url,"%s",resolved);if(!valid_bound(item.min_target_version)||!valid_bound(item.max_target_version)||(strcmp(item.target,"*")&&strcmp(item.target,"baby")&&strcmp(item.target,"fab4")&&strcmp(item.target,"baby|fab4")&&strcmp(item.target,"fab4|baby")))bad=1;
     else if(repo_validate_metadata(&item,1,why,sizeof why)==0){out[(*count)++]=item;applet_depth=0;depth--;continue;}}
    stats->skipped++;if(!stats->diagnostic[0])snprintf(stats->diagnostic,sizeof stats->diagnostic,"%s",why);applet_depth=0;
   }
   if(!depth)return fail(error,n,"Invalid XML depth");depth--;
  }
 }
 if(!root||depth||yxml_eof(&x)!=YXML_OK)return fail(error,n,"Incomplete XML document");return 0;
}
