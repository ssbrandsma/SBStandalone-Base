/* Source-aware catalog persistence and one bounded, nonblocking proxy job. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "repository_sources.h"
#include "repository_xml.h"
#include "auth.h"
#include "settings.h"
#include "../../common/applet_compat.h"
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#define SOURCES 8
#define STORE_LIMIT (512u*1024u)
#define VIEW_LIMIT 59000u
#define REFRESH_MS (6ull*60*60*1000)
#define REMOTE_PREFIX "http://127.0.0.1:80/applets/remote/"
typedef struct {
 char id[40],type[12],name[129],url[513],filename[129],base[513],effective[513],etag[129],modified[129],hash[65],error[161];
 char pending_url[513],pending_name[129];
 RepoApplet *items;char *xml;int count;unsigned discovered,skipped;char diagnostic[160];
 time_t success,attempt;uint64_t due;unsigned failures;int queued;
} Source;
static Source sources[SOURCES];static int loaded,corrupt;static char stamp[128];static uint64_t reconcile_due;
static struct {
 struct mg_connection *connection;struct mg_mgr *mgr;unsigned long client;int source,package,health,headers,done,status,redirects,head,restart;
 uint64_t deadline;size_t received,expected,limit,maximum;int has_length;FILE *file;char temporary[800],destination[800],url[513],location[513],etag[129],modified[129],sha[41],range[96];
 char *body;mg_sha1_ctx digest;
} job;
static void text(FILE *f,const char *s){fputc('"',f);for(const unsigned char *p=(const unsigned char *)s;*p;p++){if(*p=='"'||*p=='\\'){fputc('\\',f);fputc(*p,f);}else if(*p=='\n')fputs("\\n",f);else if(*p=='\r')fputs("\\r",f);else if(*p=='\t')fputs("\\t",f);else if(*p>=32)fputc(*p,f);}fputc('"',f);}
static void string_field(FILE *f,const char *key,const char *value,int comma){fprintf(f,"%s\"%s\":",comma?",":"",key);text(f,value);}
static void reply(struct mg_connection *c,int status,const char *message){char *s=NULL;size_t n=0;FILE *f=open_memstream(&s,&n);if(!f){mg_http_reply(c,500,"","{}");return;}fputs("{\"error\":",f);text(f,message);fputs("}",f);fclose(f);mg_http_reply(c,status,"Content-Type: application/json\r\nCache-Control: no-store\r\n","%s",s);free(s);}
static void ok(struct mg_connection *c,int status,const char *id){mg_http_reply(c,status,"Content-Type: application/json\r\nCache-Control: no-store\r\n","{\"ok\":true,\"id\":\"%s\"}",id?id:"");}
static int copy_string(struct mg_str object,const char *key,char *out,size_t cap){char path[96];snprintf(path,sizeof path,"$.%s",key);char *s=mg_json_get_str(object,path);if(!s){*out=0;return 0;}if(strlen(s)>=cap){free(s);return -1;}strcpy(out,s);free(s);return 0;}
static int item_read(struct mg_str object,RepoApplet *a){memset(a,0,sizeof *a);
#define R(k,f) if(copy_string(object,k,a->f,sizeof a->f))return -1;
 R("name",name) R("title",title) R("version",version) R("target",target) R("min_target_version",min_target_version) R("max_target_version",max_target_version) R("url",url) R("sha",sha) R("desc",desc) R("changes",changes) R("creator",creator) R("email",email)
#undef R
 char error[160];return repo_validate_metadata(a,1,error,sizeof error)||repo_xml_url(a->url,error,sizeof error)?-1:0;}
static void path(char *out,size_t n){sb_applet_snapshot_path(repo_config_path(),out,n);}
static void dispose(Source *s){free(s->items);free(s->xml);memset(s,0,sizeof *s);}
static int active_source(void){return job.connection||job.body||job.file;}
static int find_source(const char *id){for(int i=0;i<SOURCES;i++)if(sources[i].id[0]&&!strcmp(id,sources[i].id))return i;return -1;}
static void load(void){if(loaded)return;loaded=1;char p[800];path(p,sizeof p);int fd=open(p,O_RDONLY|O_NOFOLLOW);if(fd<0){if(errno!=ENOENT)corrupt=1;return;}struct stat st;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<2||st.st_size>STORE_LIMIT){close(fd);corrupt=1;return;}
 char *data=malloc((size_t)st.st_size+1);if(!data){close(fd);corrupt=1;return;}size_t got=0;while(got<(size_t)st.st_size){ssize_t r=read(fd,data+got,(size_t)st.st_size-got);if(r<=0)break;got+=(size_t)r;}close(fd);data[got]=0;struct mg_str obj=mg_str_n(data,got);
 int toklen=0;if(got!=(size_t)st.st_size||mg_json_get(obj,"$",&toklen)<0||toklen!=(int)got||mg_json_get_long(obj,"$.schema",0)!=1){corrupt=1;goto done;}
 copy_string(obj,"base_revision",stamp,sizeof stamp);struct mg_str list=mg_json_get_tok(obj,"$.repositories"),key,value;size_t off=0;int index=0,total=0;
 while((off=mg_json_next(list,off,&key,&value))!=0){if(index==SOURCES){corrupt=1;break;}Source *s=&sources[index++];
#define L(k,f) if(copy_string(value,k,s->f,sizeof s->f)){corrupt=1;break;}
 L("id",id) L("type",type) L("name",name) L("url",url) L("filename",filename) L("base_url",base) L("effective_url",effective) L("etag",etag) L("last_modified",modified) L("content_hash",hash)
#undef L
  if(strlen(s->id)!=36||strncmp(s->id,"xml-",4)||strspn(s->id+4,"0123456789abcdef")!=32){corrupt=1;break;}
  for(int j=0;j<index-1;j++)if(!strcmp(sources[j].id,s->id))corrupt=1;
  char error[160];if(strcmp(s->type,"url")&&strcmp(s->type,"upload"))corrupt=1;
  if(!strcmp(s->type,"url")&&repo_xml_url(s->url,error,sizeof error))corrupt=1;
  if(s->base[0]&&repo_xml_url(s->base,error,sizeof error))corrupt=1;
  s->success=(time_t)mg_json_get_long(value,"$.last_success",0);s->discovered=(unsigned)mg_json_get_long(value,"$.discovered",0);s->skipped=(unsigned)mg_json_get_long(value,"$.skipped",0);copy_string(value,"diagnostic",s->diagnostic,sizeof s->diagnostic);
  s->xml=mg_json_get_str(value,"$.xml");if(s->xml&&strlen(s->xml)>REPO_XML_LIMIT)corrupt=1;
  s->items=calloc(REPO_XML_ITEMS,sizeof *s->items);if(!s->items){corrupt=1;break;}struct mg_str array=mg_json_get_tok(value,"$.items"),k,v;size_t pos=0;
  while((pos=mg_json_next(array,pos,&k,&v))!=0){if(s->count==REPO_XML_ITEMS||item_read(v,&s->items[s->count++])){corrupt=1;break;}}
  total+=s->count;if(total>REPO_XML_TOTAL)corrupt=1;s->due=mg_millis()+10000;
 }
 done:free(data);if(corrupt){for(int i=0;i<SOURCES;i++)dispose(&sources[i]);stamp[0]=0;}
}
static void device(char target[16],char firmware[64]){strcpy(target,"baby");strcpy(firmware,"0");
#ifdef SB_TARGET_FAB4
 strcpy(target,"fab4");
#endif
 FILE *f=fopen("/etc/squeezeos.version","r");if(f){if(fgets(firmware,64,f)){char *s=strstr(firmware," r");if(s){*s='.';memmove(s+1,s+2,strlen(s+2)+1);}firmware[strcspn(firmware,"\r\n ")]=0;}fclose(f);}
#ifdef SB_REPOSITORY_TESTING
 if(getenv("SB_XML_TEST_TARGET"))snprintf(target,16,"%s",getenv("SB_XML_TEST_TARGET"));if(getenv("SB_XML_TEST_FIRMWARE"))snprintf(firmware,64,"%s",getenv("SB_XML_TEST_FIRMWARE"));
#endif
}
static int same_identity(const RepoApplet *a,const RepoApplet *b){return !strcmp(a->name,b->name)&&((sb_applet_compatible(a->target,"baby","","","")&&sb_applet_compatible(b->target,"baby","","",""))||(sb_applet_compatible(a->target,"fab4","","","")&&sb_applet_compatible(b->target,"fab4","","","")));}
static int build_view(RepoApplet *base,int count,FILE *f,size_t *bytes){long start=ftell(f);int comma=0;fputc('[',f);
 for(int i=0;i<count;i++){RepoApplet item=base[i];if(!item.source_kind[0])snprintf(item.source_kind,sizeof item.source_kind,"%s",repo_is_local_url(item.url)?"zip":"catalog");snprintf(item.source_label,sizeof item.source_label,"%s",repo_is_local_url(item.url)?"Manual ZIP":"GitHub / local catalog");if(comma++)fputc(',',f);repo_write_applet(f,&item);}
 for(int i=0;i<SOURCES;i++)for(int j=0;j<sources[i].count;j++){
  RepoApplet item=sources[i].items[j];snprintf(item.repository_id,sizeof item.repository_id,"%s",sources[i].id);snprintf(item.source_kind,sizeof item.source_kind,"%s",!strcmp(sources[i].type,"url")?"xml_url":"xml_upload");snprintf(item.source_label,sizeof item.source_label,"XML %s - %.128s",!strcmp(sources[i].type,"url")?"URL":"upload",sources[i].name);
  for(int k=0;k<count;k++)if(same_identity(&item,&base[k])){item.blocked=1;if(!strcmp(item.version,base[k].version)&&strcmp(item.sha,base[k].sha))item.conflict=1;}
  for(int k=0;k<SOURCES;k++)for(int l=0;l<sources[k].count;l++){RepoApplet *other=&sources[k].items[l];if(same_identity(&item,other)&&!strcmp(item.version,other->version)&&strcmp(item.sha,other->sha)){item.blocked=item.conflict=1;}}
  snprintf(item.url,sizeof item.url,REMOTE_PREFIX "%s.zip",item.sha);if(comma++)fputc(',',f);repo_write_applet(f,&item);
 }
 fputc(']',f);long end=ftell(f);*bytes=end>=start?(size_t)(end-start):VIEW_LIMIT+1;return *bytes>VIEW_LIMIT?-1:0;
}
/* One authoritative file: source settings, uploaded XML, cache and its projection
 * commit together. config.json itself is never rewritten by XML management. */
static int commit(char *error,size_t cap){
 if(corrupt){snprintf(error,cap,"Repository configuration needs repair; existing file retained");return -1;}
 unsigned entries=0;size_t xmlbytes=0;for(int i=0;i<SOURCES;i++){entries+=(unsigned)sources[i].count;if(sources[i].xml)xmlbytes+=strlen(sources[i].xml);}if(entries>REPO_XML_TOTAL||xmlbytes>REPO_XML_LIMIT){snprintf(error,cap,"Repository entry/XML storage quota exceeded");return -1;}
 int lock=repo_catalog_lock();if(lock<0){snprintf(error,cap,"Catalog is busy or read-only");return -1;}
 RepoApplet *base=calloc(REPO_MAX_APPLETS,sizeof *base);int count=0,result=-1;char revision[128],*data=NULL;size_t length=0,view=0;FILE *f=NULL;
 if(!base||repo_read_applets(base,&count)||sb_applet_config_stamp(repo_config_path(),revision,sizeof revision))goto done;
 f=open_memstream(&data,&length);if(!f)goto done;fputs("{\"schema\":1,\"base_revision\":",f);text(f,revision);fputs(",\"applets\":",f);if(build_view(base,count,f,&view)){snprintf(error,cap,"Unified catalog exceeds 59 KiB limit");goto done;}
 fputs(",\"repositories\":[",f);int comma=0;
 for(int i=0;i<SOURCES;i++){Source *s=&sources[i];if(!s->id[0])continue;if(comma++)fputc(',',f);fputc('{',f);
#define W(k,v,c) string_field(f,k,v,c)
  W("id",s->id,0);W("type",s->type,1);W("name",s->name,1);W("url",s->url,1);W("filename",s->filename,1);W("base_url",s->base,1);W("effective_url",s->effective,1);W("etag",s->etag,1);W("last_modified",s->modified,1);W("content_hash",s->hash,1);W("diagnostic",s->diagnostic,1);
  fprintf(f,",\"last_success\":%lld,\"discovered\":%u,\"skipped\":%u,\"enabled\":true,\"items\":[",(long long)s->success,s->discovered,s->skipped);
  for(int j=0;j<s->count;j++){if(j)fputc(',',f);repo_write_applet(f,&s->items[j]);}fputs("]",f);if(s->xml)W("xml",s->xml,1);fputc('}',f);
#undef W
 }
 fputs("]}",f);if(fclose(f)){f=NULL;goto done;}f=NULL;if(length>STORE_LIMIT){snprintf(error,cap,"Repository configuration exceeds 512 KiB quota");goto done;}
 char target[800],temporary[840],directory[800];path(target,sizeof target);snprintf(directory,sizeof directory,"%s",target);char *slash=strrchr(directory,'/');if(slash)*slash=0;else strcpy(directory,".");struct statvfs v;if(statvfs(directory,&v)||(uint64_t)v.f_bavail*v.f_frsize<length+1024u*1024u){snprintf(error,cap,"Insufficient writable storage (1 MiB reserve required)");goto done;}
 snprintf(temporary,sizeof temporary,"%s.tmp.XXXXXX",target);int fd=mkstemp(temporary);if(fd<0)goto done;fchmod(fd,0600);size_t written=0;while(written<length){ssize_t n=write(fd,data+written,length-written);if(n<=0)break;written+=(size_t)n;}int failed=written!=length||fsync(fd);if(close(fd))failed=1;if(failed||rename(temporary,target)){unlink(temporary);goto done;}
 fd=open(directory,O_RDONLY);if(fd>=0){(void)fsync(fd);close(fd);}snprintf(stamp,sizeof stamp,"%s",revision);result=0;
 done:if(f)fclose(f);free(data);free(base);repo_catalog_unlock(lock);if(result&&!*error)snprintf(error,cap,"Atomic repository update failed");return result;
}
void repo_sources_reconcile(void){load();if(corrupt)return;int any=0;for(int i=0;i<SOURCES;i++)any|=sources[i].id[0]!=0;if(!any&&!*stamp)return;char current[128],error[160]="";if(!sb_applet_config_stamp(repo_config_path(),current,sizeof current)&&strcmp(stamp,current))(void)commit(error,sizeof error);}
int repo_sources_list(char *out,size_t cap){load();if(corrupt)return repo_list_json(out,cap);char p[800];path(p,sizeof p);FILE *f=fopen(p,"rb");if(!f)return repo_list_json(out,cap);char *data=malloc(STORE_LIMIT+1);if(!data){fclose(f);return -1;}size_t n=fread(data,1,STORE_LIMIT+1,f);fclose(f);if(n>STORE_LIMIT){free(data);return -1;}struct mg_str array=mg_json_get_tok(mg_str_n(data,n),"$.applets");size_t limit=repo_upload_limit();int length=snprintf(out,cap,"{\"applets\":%.*s,\"upload_limit\":%zu,\"storage_active\":%s}",(int)array.len,array.buf,limit,limit?"true":"false");free(data);return length<0||(size_t)length>=cap?-1:0;}
static void prune(void){char directory[800];snprintf(directory,sizeof directory,"%s/xml-packages",repo_root());DIR *d=opendir(directory);if(!d)return;struct dirent *e;unsigned scanned=0;while((e=readdir(d))&&scanned++<128){if(strlen(e->d_name)!=44||strcmp(e->d_name+40,".zip")||strspn(e->d_name,"0123456789abcdef")!=40)continue;int used=0;for(int i=0;i<SOURCES;i++)for(int j=0;j<sources[i].count;j++)if(!strncmp(e->d_name,sources[i].items[j].sha,40))used=1;if(!used){char p[1100];snprintf(p,sizeof p,"%s/%s",directory,e->d_name);struct stat st;if(!lstat(p,&st)&&S_ISREG(st.st_mode))unlink(p);}}closedir(d);}
static int persist_change(Source *slot,Source old,char *error,size_t n){if(commit(error,n)){Source failed=*slot;*slot=old;if(failed.items!=old.items)free(failed.items);if(failed.xml!=old.xml)free(failed.xml);return -1;}if(old.items!=slot->items)free(old.items);if(old.xml!=slot->xml)free(old.xml);repo_notify_catalog();prune();return 0;}
int repo_sources_is_api(struct mg_str uri){const char *prefix="/api/applets/repositories";return uri.len>=strlen(prefix)&&!memcmp(uri.buf,prefix,strlen(prefix))&&(uri.len==strlen(prefix)||uri.buf[strlen(prefix)]=='/');}
static int body_string(struct mg_http_message *m,const char *key,char *out,size_t n){return copy_string(m->body,key,out,n);}
static int variable(struct mg_http_message *m,const char *key,char *out,size_t n){int r=mg_http_get_var(&m->query,key,out,n);if(r<0){*out=0;return r==-1?0:-1;}return 0;}
static int busy(int i){return sources[i].queued||(active_source()&&!job.package&&job.source==i);}
static int filename_ok(char *s){if(!*s)return 0;if(strlen(s)>128||strchr(s,'/')||strchr(s,'\\')||strstr(s,".."))return 0;return 1;}
static void source_list(struct mg_connection *c){char *data=NULL;size_t n=0;FILE *f=open_memstream(&data,&n);if(!f){reply(c,500,"Out of memory");return;}char target[16],fw[64];device(target,fw);fputs("{\"repositories\":[",f);int comma=0;
 for(int i=0;i<SOURCES;i++){Source *s=&sources[i];if(!s->id[0])continue;if(comma++)fputc(',',f);int compatible=0;for(int j=0;j<s->count;j++)if(sb_applet_compatible(s->items[j].target,target,s->items[j].min_target_version,s->items[j].max_target_version,fw))compatible++;
  const char *state=busy(i)?"Checking":s->error[0]?(s->success?"Update failed - using cached catalog":s->error):!s->count?"Empty repository":!compatible?"No compatible applets":"Available";
  fputc('{',f);string_field(f,"id",s->id,0);string_field(f,"type",s->type,1);string_field(f,"name",s->name,1);string_field(f,"url",s->url,1);string_field(f,"filename",s->filename,1);string_field(f,"base_url",s->base,1);string_field(f,"status",state,1);string_field(f,"last_error",s->error,1);string_field(f,"diagnostic",s->diagnostic,1);
  fprintf(f,",\"discovered\":%u,\"imported\":%d,\"compatible\":%d,\"skipped\":%u,\"last_successful_refresh\":%lld,\"last_attempted_refresh\":%lld,\"busy\":%s}",s->discovered,s->count,compatible,s->skipped,(long long)s->success,(long long)s->attempt,busy(i)?"true":"false");
 }
 fputs("],\"xml_limit\":262144,\"repository_limit\":8}",f);fclose(f);mg_http_reply(c,200,"Content-Type: application/json\r\nCache-Control: no-store\r\n","%s",data);free(data);
}
static void queue(int i){sources[i].queued=1;sources[i].attempt=time(NULL);sources[i].error[0]=0;}
void repo_sources_handle(struct mg_connection *c,struct mg_http_message *m){load();if(corrupt){reply(c,500,"Invalid stored repository configuration; original retained");return;}
 char uri[160],id[40]="",action[32]="",name[129]="",url[513]="",base[513]="",filename[129]="",error[160]="";if(m->uri.len>=sizeof uri){reply(c,404,"Repository not found");return;}memcpy(uri,m->uri.buf,m->uri.len);uri[m->uri.len]=0;
 const char *prefix="/api/applets/repositories";char *tail=uri+strlen(prefix);if(*tail=='/'){tail++;char *slash=strchr(tail,'/');if(slash){*slash++=0;snprintf(action,sizeof action,"%.31s",slash);}if(strlen(tail)>=sizeof id){reply(c,404,"Repository not found");return;}strcpy(id,tail);}
 int post=mg_strcmp(m->method,mg_str("POST"))==0,put=mg_strcmp(m->method,mg_str("PUT"))==0,del=mg_strcmp(m->method,mg_str("DELETE"))==0;
 if(!*id&&!post&&!put&&!del&&mg_strcmp(m->method,mg_str("GET"))==0){source_list(c);return;}
 int upload=(!strcmp(id,"upload")&&post)||(!strcmp(action,"xml")&&put);
 if(!upload&&m->body.len){int bytes=0;if(mg_json_get(m->body,"$",&bytes)<0||bytes!=(int)m->body.len||memmem(m->body.buf,m->body.len,"\\u0000",6)){reply(c,400,"Invalid repository JSON");return;}}
 if(m->body.len>(upload?REPO_XML_LIMIT:4096u)){reply(c,413,"Repository request exceeds size limit");return;}
 if(upload){if(variable(m,"name",name,sizeof name)||variable(m,"base_url",base,sizeof base)||variable(m,"filename",filename,sizeof filename)){reply(c,400,"Metadata too long");return;}}
 else if(m->body.len){if(body_string(m,"name",name,sizeof name)||body_string(m,"url",url,sizeof url)){reply(c,400,"Metadata too long");return;}}
 int index=!*id||!strcmp(id,"upload")?-1:find_source(id);
 if(*id&&strcmp(id,"upload")&&index<0){reply(c,404,"Repository not found");return;}
 if(index>=0&&busy(index)){reply(c,409,"Repository refresh is in progress");return;}
 if(index>=0&&post&&!strcmp(action,"refresh")){if(strcmp(sources[index].type,"url")){reply(c,400,"Uploaded XML is static until replaced");return;}queue(index);ok(c,202,sources[index].id);return;}
 if(index>=0&&del&&!*action){Source old=sources[index];memset(&sources[index],0,sizeof sources[index]);if(persist_change(&sources[index],old,error,sizeof error)){reply(c,507,error);return;}ok(c,200,id);return;}
 int create=index<0;
 if((create&&!post)||(!create&&!put)){reply(c,405,"Unsupported repository operation");return;}
 if(create){for(int i=0;i<SOURCES;i++)if(!sources[i].id[0]){index=i;break;}if(index<0){reply(c,409,"Eight repository limit reached");return;}}
 Source old=sources[index],next=old;
 if(create){memset(&next,0,sizeof next);unsigned char random[16];mg_random(random,sizeof random);strcpy(next.id,"xml-");for(int i=0;i<16;i++)snprintf(next.id+4+i*2,3,"%02x",random[i]);strcpy(next.type,upload?"upload":"url");}
 if(upload){if(!filename[0])strcpy(filename,"extensions.xml");if(!filename_ok(filename)){reply(c,400,"Invalid uploaded filename");return;}if(base[0]&&repo_xml_url(base,error,sizeof error)){reply(c,400,error);return;}
  if(!create&&strcmp(old.type,"upload")){reply(c,400,"Cannot replace URL repository with an upload");return;}
  RepoApplet *items=calloc(REPO_XML_ITEMS,sizeof *items);RepoXmlStats stats;int count=0;if(!items){reply(c,500,"Out of memory");return;}
  if(repo_xml_parse(m->body.buf,m->body.len,base,items,&count,&stats,error,sizeof error)){free(items);reply(c,400,error);return;}
  char *xml=malloc(m->body.len+1);if(!xml){free(items);reply(c,500,"Out of memory");return;}memcpy(xml,m->body.buf,m->body.len);xml[m->body.len]=0;
  next.items=items;next.xml=xml;next.count=count;next.discovered=stats.discovered;next.skipped=stats.skipped;snprintf(next.diagnostic,sizeof next.diagnostic,"%s",stats.diagnostic);snprintf(next.filename,sizeof next.filename,"%s",filename);snprintf(next.base,sizeof next.base,"%s",base);next.success=next.attempt=time(NULL);next.error[0]=0;
 }else {
  if(!create&&!strcmp(old.type,"upload")&&url[0]){reply(c,400,"An uploaded repository has no remote URL");return;}
  if((create||mg_json_get_tok(m->body,"$.url").len)&&repo_xml_url(url,error,sizeof error)){reply(c,400,error);return;}
  if(url[0])for(int i=0;i<SOURCES;i++)if(i!=index&&sources[i].id[0]&&!strcmp(sources[i].url,url)){reply(c,409,"Repository URL already configured");return;}
  if(!create&&url[0]&&strcmp(url,old.url)){snprintf(sources[index].pending_url,sizeof sources[index].pending_url,"%s",url);snprintf(sources[index].pending_name,sizeof sources[index].pending_name,"%s",name[0]?name:old.name);queue(index);ok(c,202,old.id);return;}
  if(create)snprintf(next.url,sizeof next.url,"%s",url);
 }
 if(name[0])snprintf(next.name,sizeof next.name,"%s",name);else if(create){if(upload)snprintf(next.name,sizeof next.name,"%s",filename);else {struct mg_str host=mg_url_host(url);snprintf(next.name,sizeof next.name,"%.*s",(int)host.len,host.buf);}}
 sources[index]=next;if(persist_change(&sources[index],old,error,sizeof error)){reply(c,507,error);return;}
 if(!upload&&!strcmp(next.type,"url")&&create)queue(index);ok(c,create?201:200,next.id);
}

static int header_value(struct mg_http_message *m,const char *key,char *out,size_t cap){struct mg_str *s=mg_http_get_header(m,key);*out=0;if(!s)return 0;if(s->len>=cap)return -1;for(size_t i=0;i<s->len;i++)if((unsigned char)s->buf[i]<32||(unsigned char)s->buf[i]>=127)return -1;memcpy(out,s->buf,s->len);out[s->len]=0;return 0;}
static struct mg_connection *waiting_client(struct mg_mgr *mgr){for(struct mg_connection *c=mgr->conns;c;c=c->next)if(c->id==job.client&&!c->is_closing)return c;return NULL;}
static void job_release(void){if(job.connection)job.connection->is_closing=1;job.done=1;job.restart=0;free(job.body);job.body=NULL;if(job.file){fclose(job.file);job.file=NULL;}if(job.temporary[0]){unlink(job.temporary);job.temporary[0]=0;}}
static void failed(const char *why){if(job.done)return;if(job.package){struct mg_connection *client=job.mgr?waiting_client(job.mgr):NULL;if(client)reply(client,502,why);}else if(job.source>=0&&job.source<SOURCES){Source *s=&sources[job.source];snprintf(s->error,sizeof s->error,"%s",why);s->pending_url[0]=s->pending_name[0]=0;static const unsigned delay[]={60000,300000,900000,3600000};unsigned n=s->failures<3?s->failures:3;s->due=mg_millis()+delay[n];if(s->failures<4)s->failures++;}job_release();}
static void served(struct mg_connection *client,const char *p,int head){struct mg_http_message m;memset(&m,0,sizeof m);m.method=mg_str(head?"HEAD":"GET");if(job.range[0]){m.headers[0].name=mg_str("Range");m.headers[0].value=mg_str(job.range);}struct mg_http_serve_opts options={0};options.extra_headers="Content-Type: application/zip\r\nX-Content-Type-Options: nosniff\r\n";mg_http_serve_file(client,&m,p,&options);}
static void complete(void){
 if(job.health){job.health=0;job.restart=1;job.done=1;job.connection->is_closing=1;return;}
 Settings settings;settings_load(&settings);if(settings.readonly){failed("Management became read-only");return;}
 if(job.package){
  unsigned char digest[20];char actual[41];mg_sha1_final(digest,&job.digest);for(int i=0;i<20;i++)snprintf(actual+i*2,3,"%02x",digest[i]);
  if(strcmp(actual,job.sha)){failed("Package SHA-1 mismatch");return;}
  if(fflush(job.file)||fsync(fileno(job.file))){failed("Package storage write failed");return;}if(fclose(job.file)){job.file=NULL;failed("Package storage close failed");return;}job.file=NULL;
  char detected[65],error[160]="";if(repo_validate_zip(job.temporary,detected,sizeof detected,error,sizeof error)){failed(error);return;}
  /* The same integrity/ZIP validator used for manual uploads, never execution. */
  int name_ok=0;for(int i=0;i<SOURCES;i++)for(int j=0;j<sources[i].count;j++)if(!strcmp(sources[i].items[j].sha,job.sha)&&!strcmp(sources[i].items[j].name,detected))name_ok=1;
  if(!name_ok){failed("Package applet identity mismatch");return;}
  if(chmod(job.temporary,0600)||rename(job.temporary,job.destination)){failed("Cannot commit verified package");return;}job.temporary[0]=0;
  struct mg_connection *client=waiting_client(job.connection->mgr);if(client)served(client,job.destination,job.head);job_release();return;
 }
 Source *s=&sources[job.source];s->attempt=time(NULL);
 if(job.status==304){if(!s->success||s->pending_url[0]||strcmp(job.url,s->effective)){failed("304 response without a matching cached repository");return;}Source old=*s;s->success=time(NULL);s->error[0]=0;s->failures=0;s->due=mg_millis()+REFRESH_MS;if(s->pending_url[0]){snprintf(s->url,sizeof s->url,"%s",s->pending_url);snprintf(s->name,sizeof s->name,"%s",s->pending_name);s->pending_url[0]=s->pending_name[0]=0;char error[160]="";if(persist_change(s,old,error,sizeof error)){failed(error);return;}}job_release();return;}
 RepoApplet *items=calloc(REPO_XML_ITEMS,sizeof *items);RepoXmlStats stats;char error[160]="";int count=0;
 if(!items){failed("Out of memory");return;}if(repo_xml_parse(job.body,job.received,job.url,items,&count,&stats,error,sizeof error)){free(items);failed(error);return;}
 unsigned char digest[32];char hash[65];mg_sha256(digest,(uint8_t *)job.body,job.received);for(int i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",digest[i]);
 int unchanged=!strcmp(hash,s->hash)&&!s->pending_url[0]&&!strcmp(job.url,s->effective)&&!strcmp(job.etag,s->etag)&&!strcmp(job.modified,s->modified);
 if(unchanged){free(items);s->success=time(NULL);s->error[0]=0;s->failures=0;s->due=mg_millis()+REFRESH_MS;job_release();return;}
 Source old=*s;s->items=items;s->count=count;s->discovered=stats.discovered;s->skipped=stats.skipped;snprintf(s->diagnostic,sizeof s->diagnostic,"%s",stats.diagnostic);snprintf(s->effective,sizeof s->effective,"%s",job.url);snprintf(s->hash,sizeof s->hash,"%s",hash);snprintf(s->etag,sizeof s->etag,"%s",job.etag);snprintf(s->modified,sizeof s->modified,"%s",job.modified);
 if(s->pending_url[0]){snprintf(s->url,sizeof s->url,"%s",s->pending_url);snprintf(s->name,sizeof s->name,"%s",s->pending_name);s->pending_url[0]=s->pending_name[0]=0;}
 s->success=time(NULL);s->error[0]=0;s->failures=0;s->due=mg_millis()+REFRESH_MS;
 if(persist_change(s,old,error,sizeof error)){failed(error);return;}job_release();
}
static void consume(struct mg_connection *c){
 if(job.done)return;
 if(!job.headers){
  if(c->recv.len>8192&&!memmem(c->recv.buf,c->recv.len,"\r\n\r\n",4)){failed("Proxy headers exceed limit");return;}
  struct mg_http_message m;int n=mg_http_parse((char *)c->recv.buf,c->recv.len,&m);if(n<0){failed("Invalid proxy response");return;}if(!n)return;if(n>8192){failed("Proxy headers exceed limit");return;}
  job.status=mg_http_status(&m);
  if(job.health){char public_guard[8],repo_guard[8];if(job.status!=200||header_value(&m,"X-SB-Public-Fetch",public_guard,sizeof public_guard)||strcmp(public_guard,"1")||header_value(&m,"X-SB-Repository-Fetch",repo_guard,sizeof repo_guard)||strcmp(repo_guard,"1")){failed("HTTPS proxy requires repository-fetch support");return;}}
  else if(job.status==301||job.status==302||job.status==303||job.status==307||job.status==308){
   char ref[513],next[513];if(job.redirects++==3||header_value(&m,"Location",ref,sizeof ref)||repo_xml_resolve(job.url,ref,next,sizeof next)){failed("Unsafe or excessive repository redirect");return;}
   snprintf(job.url,sizeof job.url,"%s",next);job.done=1;job.restart=1;c->is_closing=1;return;
  }else if(job.status!=200&&!(job.status==304&&!job.package)){failed("Repository HTTP download failed");return;}
  char encoding[64];if(header_value(&m,"Content-Encoding",encoding,sizeof encoding)||(*encoding&&strcmp(encoding,"identity"))||mg_http_get_header(&m,"Transfer-Encoding")){failed("Unsupported response encoding");return;}
  struct mg_str *length=mg_http_get_header(&m,"Content-Length");job.has_length=length!=NULL;job.expected=0;
  if(length){for(size_t i=0;i<length->len;i++){unsigned char digit=(unsigned char)length->buf[i];if(digit<'0'||digit>'9'||job.expected>job.limit/10){failed("Response exceeds size limit");return;}job.expected=job.expected*10+digit-'0';if(job.expected>job.limit){failed("Response exceeds size limit");return;}}}
  (void)header_value(&m,"ETag",job.etag,sizeof job.etag);(void)header_value(&m,"Last-Modified",job.modified,sizeof job.modified);
  job.headers=1;mg_iobuf_del(&c->recv,0,(size_t)n);
  if(job.status==304){complete();return;}
 }
 if(c->recv.len){if(c->recv.len>job.limit-job.received||(job.has_length&&c->recv.len>job.expected-job.received)){failed("Response exceeds size limit");return;}
  if(job.package&&!job.health){if(fwrite(c->recv.buf,1,c->recv.len,job.file)!=c->recv.len){failed("Package storage write failed");return;}mg_sha1_update(&job.digest,c->recv.buf,c->recv.len);}else memcpy(job.body+job.received,c->recv.buf,c->recv.len);
  job.received+=c->recv.len;mg_iobuf_del(&c->recv,0,c->recv.len);
 }
 if(job.has_length&&job.received==job.expected)complete();
}
static void network(struct mg_connection *c,int event,void *data){(void)data;
 if(event==MG_EV_CONNECT){
  if(job.health)mg_printf(c,"GET /health HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
  else {
   const char *p=strstr(job.url,"://");const char *protocol=!strncmp(job.url,"https://",8)?"https":"http";char conditional[400]="";
   if(!job.package&&job.source>=0){Source *s=&sources[job.source];if(!strcmp(s->effective,job.url)){if(*s->etag)snprintf(conditional,sizeof conditional,"If-None-Match: %s\r\n",s->etag);else if(*s->modified)snprintf(conditional,sizeof conditional,"If-Modified-Since: %s\r\n",s->modified);}}
   mg_printf(c,"GET /%s/%s HTTP/1.0\r\nHost: 127.0.0.1\r\nAccept: %s\r\nAccept-Encoding: identity\r\nUser-Agent: StandaloneBaseRepositories/1.0\r\nX-SB-Public-Fetch: 1\r\nX-SB-Repository-Fetch: 1\r\n%s%sConnection: close\r\n\r\n",protocol,p+3,job.package?"application/zip":"application/xml,text/xml,*/*",job.package?"X-SB-Repository-Package: 1\r\n":"",conditional);
  }
 }else if(event==MG_EV_READ)consume(c);
 else if(event==MG_EV_ERROR&&!job.done)failed("Repository unreachable");
 else if(event==MG_EV_CLOSE){if(!job.done){if(job.headers&&!job.has_length)complete();else failed("Incomplete repository response");}if(job.connection==c)job.connection=NULL;}
}
static void connect_job(struct mg_mgr *mgr){job.headers=job.done=0;job.received=job.expected=0;job.has_length=0;job.etag[0]=job.modified[0]=0;
 job.mgr=mgr;job.limit=job.health?4096:job.package?job.maximum:REPO_XML_LIMIT;
 if(!job.health&&job.package){size_t available=repo_upload_limit();if(!available){failed("Insufficient package storage");return;}if(job.limit>available)job.limit=available;}
 const char *proxy="http://127.0.0.1:8765";
#ifdef SB_REPOSITORY_TESTING
 if(getenv("SB_XML_TEST_PROXY"))proxy=getenv("SB_XML_TEST_PROXY");
#endif
 job.connection=mg_connect(mgr,proxy,network,NULL);if(!job.connection)failed("Cannot connect to HTTPS proxy");
}
void repo_sources_tick(struct mg_mgr *mgr){load();if(corrupt)return;Settings settings;settings_load(&settings);
 if(job.connection&&!job.done){if(mg_millis()>=job.deadline)failed("Repository download timed out");else if(settings.readonly)failed("Management became read-only");}
 if(job.package&&active_source()&&job.client&&!waiting_client(mgr)){job_release();}
 if(job.restart&&!job.connection){job.restart=0;connect_job(mgr);return;}
 if(active_source())return;
 if(mg_millis()>=reconcile_due){reconcile_due=mg_millis()+1000;char old_stamp[128];snprintf(old_stamp,sizeof old_stamp,"%s",stamp);repo_sources_reconcile();if(strcmp(old_stamp,stamp))repo_notify_catalog();}
 if(settings.readonly)return;
 for(int i=0;i<SOURCES;i++)if(sources[i].id[0]&&!strcmp(sources[i].type,"url")&&(sources[i].queued||mg_millis()>=sources[i].due)){
  memset(&job,0,sizeof job);job.source=i;job.health=1;job.deadline=mg_millis()+20000;job.body=malloc(REPO_XML_LIMIT+1);sources[i].queued=0;sources[i].attempt=time(NULL);
  snprintf(job.url,sizeof job.url,"%s",sources[i].pending_url[0]?sources[i].pending_url:sources[i].url);if(!job.body){failed("Out of memory");return;}connect_job(mgr);return;
 }
}
int repo_sources_headers(struct mg_connection *c,int event,void *data){
 if(event==MG_EV_HTTP_HDRS){struct mg_http_message *m=data;if(repo_sources_is_api(m->uri)){if(!auth_session_ok(m,NULL)){reply(c,401,"Browser pairing required");c->is_draining=1;return 1;}struct mg_str *length=mg_http_get_header(m,"Content-Length");if(length){size_t size=0;for(size_t i=0;i<length->len;i++){if(length->buf[i]<'0'||length->buf[i]>'9'||size>REPO_XML_LIMIT/10){reply(c,413,"XML request exceeds size limit");c->is_draining=1;return 1;}size=size*10+(unsigned)(length->buf[i]-'0');}if(size>REPO_XML_LIMIT){reply(c,413,"XML request exceeds size limit");c->is_draining=1;return 1;}}}}
 if(event==MG_EV_READ&&c->recv.len>REPO_XML_LIMIT+8192){struct mg_http_message m;if(mg_http_parse((char *)c->recv.buf,c->recv.len,&m)>0&&repo_sources_is_api(m.uri)){reply(c,413,"XML request exceeds size limit");c->is_draining=1;return 1;}}
 return 0;
}
int repo_sources_download(struct mg_connection *c,struct mg_http_message *m){
 const char *prefix="/applets/remote/";if(m->uri.len<strlen(prefix)||memcmp(m->uri.buf,prefix,strlen(prefix)))return 0;
 if((c->rem.is_ip6||c->rem.ip[0]!=127)&&!auth_session_ok(m,NULL)){reply(c,401,"Browser pairing required");return 1;}
 if(mg_strcmp(m->method,mg_str("GET"))&&mg_strcmp(m->method,mg_str("HEAD"))){reply(c,405,"GET or HEAD required");return 1;}
 size_t start=strlen(prefix);if(m->uri.len!=start+44||memcmp(m->uri.buf+start+40,".zip",4)){reply(c,404,"Unknown package");return 1;}char sha[41];memcpy(sha,m->uri.buf+start,40);sha[40]=0;if(strspn(sha,"0123456789abcdef")!=40){reply(c,404,"Unknown package");return 1;}
 load();RepoApplet *item=NULL;char model[16],firmware[64];device(model,firmware);
 RepoApplet *base=calloc(REPO_MAX_APPLETS,sizeof *base);int base_count=0;
 if(!base||repo_read_applets(base,&base_count)){free(base);reply(c,503,"Catalog unavailable");return 1;}
 for(int i=0;i<SOURCES;i++)for(int j=0;j<sources[i].count;j++){
  RepoApplet *candidate=&sources[i].items[j];if(strcmp(sha,candidate->sha)||!sb_applet_compatible(candidate->target,model,candidate->min_target_version,candidate->max_target_version,firmware))continue;
  int blocked=0;for(int k=0;k<base_count;k++)if(same_identity(candidate,&base[k]))blocked=1;
  for(int k=0;k<SOURCES;k++)for(int l=0;l<sources[k].count;l++){RepoApplet *other=&sources[k].items[l];if(same_identity(candidate,other)&&!strcmp(candidate->version,other->version)&&strcmp(candidate->sha,other->sha))blocked=1;}
  if(!blocked)item=candidate;
 }
 free(base);if(!item){reply(c,404,"Unknown package");return 1;}
 char directory[800],destination[800],error[160];snprintf(directory,sizeof directory,"%s/xml-packages",repo_root());snprintf(destination,sizeof destination,"%.740s/%s.zip",directory,sha);struct stat st;
 if(!lstat(destination,&st)&&S_ISREG(st.st_mode)){struct mg_http_serve_opts opts={0};opts.extra_headers="Content-Type: application/zip\r\nX-Content-Type-Options: nosniff\r\n";mg_http_serve_file(c,m,destination,&opts);return 1;}
 if(active_source()){reply(c,503,"Repository fetch busy; retry this installation");return 1;}
 if(!repo_storage_ready(error,sizeof error)||(mkdir(directory,0700)&&errno!=EEXIST)){reply(c,507,"Insufficient package storage");return 1;}
 /* Dedicated cache quota, independent of manually uploaded ZIP packages. */
 DIR *d=opendir(directory);uint64_t used=0;unsigned count=0;struct dirent *entry;if(d){while((entry=readdir(d))){if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;if(++count>96){used=32u*1024u*1024u;break;}char p[1100];snprintf(p,sizeof p,"%s/%s",directory,entry->d_name);if(!lstat(p,&st)&&S_ISREG(st.st_mode))used+=(uint64_t)st.st_size;}closedir(d);}
 if(used>=32u*1024u*1024u){reply(c,507,"XML package cache quota reached; remove unused repositories");return 1;}
 memset(&job,0,sizeof job);job.package=1;job.source=-1;job.client=c->id;job.health=1;job.deadline=mg_millis()+60000;job.head=!mg_strcmp(m->method,mg_str("HEAD"));snprintf(job.url,sizeof job.url,"%s",item->url);snprintf(job.sha,sizeof job.sha,"%s",sha);snprintf(job.destination,sizeof job.destination,"%s",destination);(void)header_value(m,"Range",job.range,sizeof job.range);
 snprintf(job.temporary,sizeof job.temporary,"%s/tmp/xml-fetch.XXXXXX",repo_root());int fd=mkstemp(job.temporary);if(fd<0){job.temporary[0]=0;reply(c,507,"Cannot stage package download");return 1;}job.file=fdopen(fd,"wb");if(!job.file){close(fd);unlink(job.temporary);job.temporary[0]=0;reply(c,507,"Cannot stage package download");return 1;}
 job.maximum=(size_t)(32u*1024u*1024u-used);if(job.maximum>REPO_MAX_UPLOAD)job.maximum=REPO_MAX_UPLOAD;mg_sha1_init(&job.digest);job.body=malloc(REPO_XML_LIMIT+1);if(!job.body){job_release();reply(c,500,"Out of memory");return 1;}
 connect_job(c->mgr);return 1;
}
