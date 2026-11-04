#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "music_library.h"
#include "third_party/sqlite/sqlite3.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static uint64_t serial=1;
static sb_music_media media={{{1,2,3,4}},1,1};
static double now(void){struct timespec t;
clock_gettime(CLOCK_MONOTONIC,&t);
return t.tv_sec+t.tv_nsec/1e9;
}
static sb_ml_result wait_result(sb_music_library *h)
{
 sb_ml_result result;
 for(;;){sb_ml_status status=sb_music_library_receive(h,&result);
 if(status==SB_ML_OK)return result;
 assert(status==SB_ML_BUSY);
 struct pollfd p={sb_music_library_fd(h),POLLIN,0};
 assert(poll(&p,1,10000)>0);
 }
}
static sb_ml_result call(sb_music_library *h,sb_ml_request *r)
{
 r->request_id=serial++;
 for(;;){sb_ml_status s=sb_music_library_submit(h,r);
 if(s==SB_ML_OK)break;
 assert(s==SB_ML_BUSY);
 struct timespec t={0,100000};
 nanosleep(&t,NULL);
 }
 sb_ml_result result=wait_result(h);
 assert(result.request_id==r->request_id);
 return result;
}
static sb_ml_result checked(sb_music_library *h,sb_ml_request *r)
{
 sb_ml_result result=call(h,r);
 if(result.status!=SB_ML_OK){fprintf(stderr,"operation=%d status=%s sqlite=%d: %s\n",r->operation,sb_music_library_status(result.status),result.sqlite_code,result.error);
 abort();
 }
 return result;
}
static sb_music_library *open_library(const char *path,int create,int readonly,int wal)
{
 sb_ml_options o={0};
 snprintf(o.path,sizeof o.path,"%s",path);
 o.create=create;
 o.read_only=readonly;
 o.journal=wal?SB_ML_WAL_JOURNAL:SB_ML_DELETE_JOURNAL;
 o.wal_safe=wal;
 o.busy_ms=50;
 o.query_ms=5000;
 sb_music_library *h=sb_music_library_start(&o);
 assert(sb_music_library_destroy(h)==SB_ML_BUSY);
 assert(h);
 sb_ml_result result=wait_result(h);
 if(result.status!=SB_ML_OK){fprintf(stderr,"open %s: %s %s\n",path,sb_music_library_status(result.status),result.error);
 abort();
 }
 return h;
}
static void close_library(sb_music_library *h){sb_ml_request r={0};
r.operation=SB_ML_CLOSE;
checked(h,&r);
assert(sb_music_library_destroy(h)==SB_ML_OK);
}
static void source(sb_music_library *h){sb_ml_request r={0};
r.operation=SB_ML_SOURCE;
r.data.media=media;
checked(h,&r);
}
static void transaction(sb_music_library *h,sb_ml_operation operation,uint64_t id){sb_ml_request r={0};
r.operation=operation;
r.transaction=id;
checked(h,&r);
}
static void fixture(sb_ml_track *t,unsigned i)
{
 memset(t,0,sizeof *t);
 t->identity.source=media.source;
 t->media_generation=media.generation;
 unsigned album=i/10;
 t->disc=i%10/5+1;
 t->track=i%5+1;
 snprintf(t->path,sizeof t->path,"folder%05u/disc%u/file%05u.flac",album,t->disc,i);
 snprintf(t->title,sizeof t->title,"Track %05u",i);
 if(i%31==0)strcpy(t->title,"Duplicate");
 if(i==7)strcpy(t->title,"Caf\xc3\xa9 \xf0\x9f\x8e\xb5");
 snprintf(t->album,sizeof t->album,"Album %02u",album%7);
 snprintf(t->album_key,sizeof t->album_key,"release-%05u",album);
 t->compilation=album%3==0;
 if(t->compilation)strcpy(t->album_artist,"Various");
 else snprintf(t->album_artist,sizeof t->album_artist,"Album Artist %02u",album%20);
 t->artist_count=1;
 snprintf(t->artists[0],sizeof t->artists[0],"Artist %02u",i%40);
 if(i%7==0){strcpy(t->artists[1],"Guest");
 t->artist_count++;
 }
 t->genre_count=1;
 snprintf(t->genres[0],sizeof t->genres[0],"Genre %u",i%8);
 if(i%9==0){strcpy(t->genres[1],"Shared");
 t->genre_count++;
 }
 t->file_size=UINT64_C(5368709243);
 t->audio_offset=UINT64_C(4294967296);
 t->audio_length=4096;
 t->duration_ms=180000+i;
 t->mtime=1700000000+i;
 t->sample_rate=44100;
 t->channels=2;
 t->bitrate=900;
 t->year=2000+i%25;
 t->scan_epoch=1;
 strcpy(t->codec,"flac");
 snprintf(t->fingerprint,sizeof t->fingerprint,"synthetic-%u",i);
 strcpy(t->artwork_ref,"covers/front.jpg");
}
static sb_ml_request query_request(sb_music_query_kind kind)
{
 sb_ml_request r={0};
 r.operation=SB_ML_QUERY;
 r.data.query.kind=kind;
 r.data.query.media=media;
 r.data.query.limit=50;
 return r;
}
static sb_ml_result detail(sb_music_library *h,uint64_t id)
{
 sb_ml_request r={0};
 r.operation=SB_ML_DETAIL;
 r.data.detail.identity=(sb_music_track_id){media.source,id};
 r.data.detail.generation=media.generation;
 return checked(h,&r);
}
static int compare_names(const char *a,const char *b)
{
 for(;*a&&*b;a++,b++){unsigned x=(unsigned char)*a,y=(unsigned char)*b;
 if(x>='A'&&x<='Z')x+=32;
 if(y>='A'&&y<='Z')y+=32;
 if(x!=y)return x<y?-1:1;
 }return (unsigned char)*a-(unsigned char)*b;
}
static unsigned descriptors(void){DIR *d=opendir("/proc/self/fd");
assert(d);
unsigned n=0;
struct dirent *entry;
while((entry=readdir(d)))if(entry->d_name[0]!='.')n++;
closedir(d);
return n-1; /* exclude this enumeration descriptor itself */
}
static void raw(sqlite3 *db,const char *sql){char *error=NULL;
int rc=sqlite3_exec(db,sql,NULL,NULL,&error);
if(rc){fprintf(stderr,"fixture SQL: %s\n",error);
abort();
}sqlite3_free(error);
}
static void correctness(const char *path,unsigned n,int wal)
{
 unsigned fds=descriptors();
 sb_music_library *h=open_library(path,1,0,wal);
 source(h);
 sb_ml_request r=query_request(SB_QUERY_TRACKS);
 assert(checked(h,&r).data.query.page.total==0);
 r=query_request(SB_QUERY_FOLDERS);assert(checked(h,&r).data.query.page.total==0);
 uint64_t first=0;
 transaction(h,SB_ML_BEGIN,77);
 r=query_request(SB_QUERY_TRACKS);assert(call(h,&r).status==SB_ML_CONFLICT);
 for(unsigned i=0;i<n;i++){memset(&r,0,sizeof r);
 r.operation=SB_ML_UPSERT;
 r.transaction=77;
 fixture(&r.data.track,i);
 sb_ml_result result=checked(h,&r);
 if(!i)first=result.record_id;
 }
 transaction(h,SB_ML_COMMIT,77);
 r=query_request(SB_QUERY_TRACKS);
 sb_ml_result result=checked(h,&r);
 assert(result.data.query.page.total==n&&result.data.query.page.count==50);
 uint64_t revision=result.revision;
 sb_music_library_provider provider=sb_music_library_queries(h);
 uint64_t provider_id=serial++;int submission;
 while((submission=provider.submit_query(provider.context,provider_id,&r.data.query))==SB_ML_BUSY){struct timespec delay={0,100000};nanosleep(&delay,NULL);}assert(submission==SB_ML_OK);
 sb_ml_result provider_result=wait_result(h);assert(provider_result.request_id==provider_id&&provider_result.data.query.page.total==n);
 assert(sb_music_library_cancel(h,provider_id)==SB_ML_STALE);
 r.request_id=provider_id;assert(sb_music_library_submit(h,&r)==SB_ML_STALE);

 char previous[128]="";
 uint64_t previous_id=0;
 unsigned seen=0;
 for(unsigned offset=0;offset<n;offset+=50){r.data.query.offset=offset;
 result=checked(h,&r);
 assert(result.revision==revision&&result.data.query.page.total==n);
  for(unsigned j=0;j<result.data.query.page.count;j++){sb_music_row *row=&result.data.query.rows[j];
  int order=compare_names(previous,row->name);
  assert(order<0||(order==0&&previous_id<row->id));
  strcpy(previous,row->name);
  previous_id=row->id;
  seen++;
  }}
 assert(seen==n);
 r.data.query.offset=n;
 assert(checked(h,&r).data.query.page.count==0);
 r.data.query.offset=n-1;
 assert(checked(h,&r).data.query.page.count==1);
 r=query_request(SB_QUERY_ALBUMS);
 assert(checked(h,&r).data.query.page.total==(n+9)/10);
 r=query_request(SB_QUERY_ARTISTS);
 assert(checked(h,&r).data.query.page.total==62);
 strcpy(r.data.query.search,"Artist 00");
 result=checked(h,&r);
 assert(result.data.query.page.total==1);
 uint64_t artist=result.data.query.rows[0].id;
 r=query_request(SB_QUERY_ALBUMS);
 r.data.query.artist_id=artist;
 assert(checked(h,&r).data.query.page.total==(n+39)/40);
 r=query_request(SB_QUERY_TRACKS);strcpy(r.data.query.search,"Artist 00");r.data.query.search_field=SB_SEARCH_ARTIST;assert(checked(h,&r).data.query.page.total==(n+39)/40);
 r=query_request(SB_QUERY_TRACKS);strcpy(r.data.query.search,"Album 00");r.data.query.search_field=SB_SEARCH_ALBUM;assert(checked(h,&r).data.query.page.total==((n/10+6)/7)*10);
 r=query_request(SB_QUERY_ALBUMS);result=checked(h,&r);uint64_t selected_album=result.data.query.rows[0].id;
 r=query_request(SB_QUERY_TRACKS);r.data.query.album_id=selected_album;r.data.query.sort=SB_SORT_ALBUM_TRACK;result=checked(h,&r);assert(result.data.query.page.total==10);
 for(unsigned i=0;i<10;i++){sb_ml_track item=detail(h,result.data.query.rows[i].id).data.track;assert(item.disc==i/5+1&&item.track==i%5+1);}
 r=query_request(SB_QUERY_GENRES);
 assert(checked(h,&r).data.query.page.total==9);
 strcpy(r.data.query.search,"Genre 0");
 result=checked(h,&r);
 assert(result.data.query.page.total==1);
 uint64_t genre=result.data.query.rows[0].id;
 r=query_request(SB_QUERY_TRACKS);
 r.data.query.genre_id=genre;
 assert(checked(h,&r).data.query.page.total==(n+7)/8);
 r=query_request(SB_QUERY_ALBUMS);
 r.data.query.genre_id=genre;
 assert(checked(h,&r).data.query.page.total==(n+9)/10);
 r=query_request(SB_QUERY_ARTISTS);
 r.data.query.genre_id=genre;
 assert(checked(h,&r).data.query.page.total==27);
 r=query_request(SB_QUERY_FOLDERS);
 result=checked(h,&r);
 assert(result.data.query.page.total==(n+9)/10);
 uint64_t folder=result.data.query.rows[0].id;
 r.data.query.folder_id=folder;
 result=checked(h,&r);
 assert(result.data.query.page.total==2&&result.data.query.rows[0].kind==SB_QUERY_FOLDERS);
 r.data.query.folder_id=result.data.query.rows[0].id;
 result=checked(h,&r);
 assert(result.data.query.page.total==5&&result.data.query.rows[0].kind==SB_QUERY_TRACKS);
 r=query_request(SB_QUERY_TRACKS);
 strcpy(r.data.query.search,"Caf\xc3\xa9");
 r.data.query.search_field=SB_SEARCH_TITLE;
 result=checked(h,&r);
 assert(result.data.query.page.total==1);
 sb_ml_result info=detail(h,result.data.query.rows[0].id);
 assert(!strcmp(info.data.track.title,"Caf\xc3\xa9 \xf0\x9f\x8e\xb5"));
 assert(info.data.track.file_size==UINT64_C(5368709243)&&info.data.track.audio_offset==UINT64_C(4294967296));
 strcpy(r.data.query.search,"' OR 1=1 --");
 assert(checked(h,&r).data.query.page.total==0);
 strcpy(r.data.query.search,"%");
 assert(checked(h,&r).data.query.page.total==0);
 info=detail(h,first);
 assert(info.data.track.artist_count==2&&info.data.track.genre_count==2&&info.data.track.compilation);
 memset(&r,0,sizeof r);
 r.operation=SB_ML_UPSERT;
 r.data.track=info.data.track;
 strcpy(r.data.track.path,"renamed/disc1/file00000.flac");
 strcpy(r.data.track.title,"Changed title");
 result=checked(h,&r);
 assert(result.record_id==first);
 assert(!strcmp(detail(h,first).data.track.title,"Changed title"));
 r=query_request(SB_QUERY_TRACKS);
 r.data.query.library_revision=revision;
 assert(call(h,&r).status==SB_ML_STALE);
 transaction(h,SB_ML_BEGIN,99);
 r.operation=SB_ML_UPSERT;
 r.transaction=99;
 fixture(&r.data.track,n+1);
 checked(h,&r);
 transaction(h,SB_ML_ROLLBACK,99);
 r=query_request(SB_QUERY_TRACKS);
 assert(checked(h,&r).data.query.page.total==n);
 /* Removal revokes and rolls back a pending metadata transaction even without its token. */
 transaction(h,SB_ML_BEGIN,101);
 r.operation=SB_ML_UPSERT;r.transaction=101;fixture(&r.data.track,n+2);checked(h,&r);
 media.online=0;source(h);media.online=1;media.generation++;source(h);
 r=query_request(SB_QUERY_TRACKS);assert(checked(h,&r).data.query.page.total==n);
 /* Removal/reinsertion is explicit input, never an OS/storage action. */
 media.online=0;
 source(h);
 r=query_request(SB_QUERY_TRACKS);
 assert(call(h,&r).status==SB_ML_INVALID);
 r.data.query.media.online=1;
 assert(call(h,&r).status==SB_ML_OFFLINE);
 media.online=1;
 media.generation++;
 source(h);
 assert(call(h,&r).status==SB_ML_STALE);
 assert(detail(h,first).data.track.identity.track_id==first);
 memset(&r,0,sizeof r);
 r.operation=SB_ML_DELETE_TRACK;
 r.data.detail.identity=(sb_music_track_id){media.source,first};
 r.data.detail.generation=media.generation;
 checked(h,&r);
 r=query_request(SB_QUERY_TRACKS);
 assert(checked(h,&r).data.query.page.total==n-1);
 /* Missing metadata and long Unicode-safe relative paths, no file access. */
 memset(&r,0,sizeof r);
 r.operation=SB_ML_UPSERT;
 r.data.track.identity.source=media.source;
 r.data.track.media_generation=media.generation;
 memset(r.data.track.path,'x',1000);
 strcpy(r.data.track.path+1000,"/missing.flac");
 result=checked(h,&r);
 assert(result.record_id>first);
 assert(!strcmp(detail(h,result.record_id).data.track.title,"missing.flac"));
 uint64_t long_track=result.record_id;r.data.track.identity.track_id=long_track;
 for(unsigned i=0;i<80;i++){r.data.track.title[2*i]=(char)0xc3;r.data.track.title[2*i+1]=(char)0xa9;}r.data.track.title[160]=0;checked(h,&r);
 r=query_request(SB_QUERY_TRACKS);strcpy(r.data.query.search,"\xc3\xa9");result=checked(h,&r);assert(result.data.query.page.total==1&&strlen(result.data.query.rows[0].name)==126);
 assert(strlen(detail(h,long_track).data.track.title)==160);

 r=query_request(SB_QUERY_TRACKS);
 assert(checked(h,&r).data.query.page.total==n);
 close_library(h);
 assert(descriptors()==fds);
 h=open_library(path,0,0,wal);
 source(h);
 assert(detail(h,long_track).data.track.identity.track_id==long_track);
 close_library(h);
 assert(descriptors()==fds);
 /* Lock contention remains in worker: submit is nonblocking; busy bounded. */
 h=open_library(path,0,0,wal);
 source(h);
 sqlite3 *other=NULL;
 assert(sqlite3_open(path,&other)==SQLITE_OK);
 raw(other,"BEGIN IMMEDIATE");
 memset(&r,0,sizeof r);
 r.operation=SB_ML_UPSERT;
 fixture(&r.data.track,0);
 double start=now();
 r.request_id=serial++;
 assert(sb_music_library_submit(h,&r)==SB_ML_OK);
 assert(now()-start<.1);
 assert(wait_result(h).status==SB_ML_BUSY);
 assert(now()-start<1);
 raw(other,"ROLLBACK");
 sqlite3_close(other);
 close_library(h);
 h=open_library(path,0,1,wal);
 source(h);
 r=query_request(SB_QUERY_TRACKS);
 assert(checked(h,&r).data.query.page.total==n);
 r.operation=SB_ML_UPSERT;
 fixture(&r.data.track,0);
 assert(call(h,&r).status==SB_ML_READ_ONLY);
 memset(&r,0,sizeof r);r.operation=SB_ML_INTEGRITY;checked(h,&r);
 close_library(h);
 assert(descriptors()==fds);
 /* Independent worker handles observe atomic persisted revision changes. */
 h=open_library(path,0,0,wal);source(h);
 sb_music_library *writer=open_library(path,0,0,wal);source(writer);
 r=query_request(SB_QUERY_TRACKS);uint64_t observed=checked(h,&r).revision;
 memset(&r,0,sizeof r);r.operation=SB_ML_UPSERT;fixture(&r.data.track,1);strcpy(r.data.track.title,"Other writer");uint64_t written=checked(writer,&r).revision;
 assert(written>observed);r=query_request(SB_QUERY_TRACKS);r.data.query.library_revision=observed;assert(call(h,&r).status==SB_ML_STALE);
 r.data.query.library_revision=0;assert(checked(h,&r).revision>=written);
 close_library(writer);close_library(h);assert(descriptors()==fds);
 /* Distinct sources may contain identical relative paths without sharing IDs. */
 h=open_library(path,0,0,wal);source(h);
 sb_music_media primary=media;media.source.bytes[0]=9;media.generation=1;source(h);
 memset(&r,0,sizeof r);r.operation=SB_ML_UPSERT;fixture(&r.data.track,1);uint64_t other_track=checked(h,&r).record_id;
 r=query_request(SB_QUERY_TRACKS);assert(checked(h,&r).data.query.page.total==1);
 media=primary;r=query_request(SB_QUERY_TRACKS);assert(checked(h,&r).data.query.page.total==n);
 memset(&r,0,sizeof r);r.operation=SB_ML_DETAIL;r.data.detail.identity=(sb_music_track_id){media.source,other_track};r.data.detail.generation=media.generation;assert(call(h,&r).status==SB_ML_NOT_FOUND);
 close_library(h);assert(descriptors()==fds);
 printf("correctness: %u tracks; journal=%s; relationships/pagination/transactions/identity/read-only/locking passed\n",n,wal?"wal":"delete");
}
static int compare_double(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static void benchmark(const char *path,unsigned n)
{
 sb_music_library *h=open_library(path,1,0,0);
 source(h);
 double cpu=(double)clock()/CLOCKS_PER_SEC,started=now();
 sb_ml_request r={0};
 for(unsigned i=0;i<n;i++){
  if(i%1000==0)transaction(h,SB_ML_BEGIN,1);
  memset(&r,0,sizeof r);
  r.operation=SB_ML_UPSERT;
  r.transaction=1;
  fixture(&r.data.track,i);
  checked(h,&r);
  if(i%1000==999||i+1==n)transaction(h,SB_ML_COMMIT,1);
 }
 double imported=now()-started;
 close_library(h);
 h=open_library(path,0,0,0);
 source(h);
 sb_music_query_kind kinds[]={SB_QUERY_ARTISTS,SB_QUERY_ALBUMS,SB_QUERY_TRACKS,SB_QUERY_TRACKS};
 double cold[4],warm[4],p95[4];
 for(unsigned k=0;k<4;k++){
  r=query_request(kinds[k]);
  if(k==3)strcpy(r.data.query.search,"Track 00");
  started=now();
  sb_ml_result result=checked(h,&r);
  cold[k]=(now()-started)*1000;
  assert(k==3||result.data.query.page.total==(k==0?62:k==1?(n+9)/10:n));
  started=now();
  double samples[20];
  for(unsigned j=0;j<20;j++){double tick=now();checked(h,&r);samples[j]=(now()-tick)*1000;}
  warm[k]=(now()-started)*1000/20;
  qsort(samples,20,sizeof samples[0],compare_double);p95[k]=samples[18];
 }
 started=now();
 transaction(h,SB_ML_BEGIN,2);
 for(unsigned i=0;i<10;i++){memset(&r,0,sizeof r);
 r.operation=SB_ML_UPSERT;
 r.transaction=2;
 fixture(&r.data.track,i);
 strcpy(r.data.track.title,"Incremental update");
 checked(h,&r);
 }transaction(h,SB_ML_COMMIT,2);
 double incremental=(now()-started)*1000;
 r.transaction=0;r.operation=SB_ML_STATS;uint64_t memory_before=checked(h,&r).data.stats.sqlite_memory;
 unsigned before=descriptors();
 r=query_request(SB_QUERY_TRACKS);
 for(unsigned i=0;i<100;i++)checked(h,&r);
 assert(descriptors()==before);
 r.operation=SB_ML_STATS;
 sb_ml_stats stats=checked(h,&r).data.stats;
 assert(stats.sqlite_memory<=memory_before+65536);
 long high=0;
 FILE *f=fopen("/proc/self/status","r");
 if(f){char line[128];
 while(fgets(line,sizeof line,f))if(sscanf(line,"VmHWM: %ld",&high)==1)break;
 fclose(f);
 }
 printf("{\"tracks\":%u,\"db_bytes\":%llu,\"index_bytes\":%llu,\"import_seconds\":%.6f,\"incremental_10_ms\":%.6f,\"cpu_seconds\":%.6f,\"peak_rss_kib\":%ld,\"sqlite_memory_bytes\":%llu,\"sqlite_memory_high\":%llu,\"sqlite_memory_before\":%llu,\"fds_before_after\":[%u,%u],\"connection_first_ms\":[%.6f,%.6f,%.6f,%.6f],\"warm_mean_ms\":[%.6f,%.6f,%.6f,%.6f],\"warm_p95_ms\":[%.6f,%.6f,%.6f,%.6f],\"plans\":[",n,(unsigned long long)stats.db_bytes,(unsigned long long)stats.index_bytes,imported,incremental,(double)clock()/CLOCKS_PER_SEC-cpu,high,(unsigned long long)stats.sqlite_memory,(unsigned long long)stats.sqlite_memory_high,(unsigned long long)memory_before,before,descriptors(),cold[0],cold[1],cold[2],cold[3],warm[0],warm[1],warm[2],warm[3],p95[0],p95[1],p95[2],p95[3]);
 for(unsigned k=0;k<4;k++){r=query_request(kinds[k]);
 if(k==3)strcpy(r.data.query.search,"Track 00");
 r.operation=SB_ML_PLAN;
 sb_ml_result plan=checked(h,&r);
 printf("%s[",k?",":"");
 for(unsigned j=0;j<plan.data.plan.count;j++)printf("%s\"%s\"",j?",":"",plan.data.plan.lines[j]);
 printf("]");
 }
 printf("]}\n");
 close_library(h);
}
static ssize_t fail_write(int fd,const void *buffer,size_t count){(void)fd;(void)buffer;(void)count;errno=EIO;return -1;}
static ssize_t fail_pwrite(int fd,const void *buffer,size_t count,off_t offset){(void)offset;return fail_write(fd,buffer,count);}
int main(int argc,char **argv)
{
 assert(argc>=3);
 if(!strcmp(argv[1],"integrity")){
  sb_music_library *h=open_library(argv[2],1,0,0);source(h);
  sb_ml_request r={0};r.operation=SB_ML_INTEGRITY;checked(h,&r);
  transaction(h,SB_ML_BEGIN,123);
  r.transaction=123;assert(call(h,&r).status==SB_ML_CONFLICT);
  transaction(h,SB_ML_ROLLBACK,123);r.transaction=0;checked(h,&r);
  close_library(h);unlink(argv[2]);return 0;
 }
 unsigned n=argc>3?(unsigned)strtoul(argv[3],NULL,10):1000;
 if(!strcmp(argv[1],"bench")){benchmark(argv[2],n);
 return 0;
 }
 if(!strcmp(argv[1],"correct")){correctness(argv[2],n,argc>4?atoi(argv[4]):0);
 return 0;
 }
 if(!strcmp(argv[1],"limit")){
  sqlite3_initialize();sqlite3_int64 old=sqlite3_hard_heap_limit64(1);
  sb_ml_options options={0};snprintf(options.path,sizeof options.path,"%s",argv[2]);
  sb_music_library *h=sb_music_library_start(&options);assert(h);sb_ml_result result=wait_result(h);assert(result.status==SB_ML_LIMIT);assert(sb_music_library_destroy(h)==SB_ML_OK);
  sqlite3_hard_heap_limit64(old);printf("allocator limit reported without crashing or rebuilding\n");return 0;
 }
 if(!strcmp(argv[1],"iofault")){
  sb_music_library *h=open_library(argv[2],0,0,0);
  source(h);
  sqlite3_vfs *vfs=sqlite3_vfs_find(NULL);
  assert(vfs->iVersion>=3 && vfs->xSetSystemCall);
  assert(vfs->xSetSystemCall(vfs,"write",(sqlite3_syscall_ptr)fail_write)==SQLITE_OK);
  assert(vfs->xSetSystemCall(vfs,"pwrite",(sqlite3_syscall_ptr)fail_pwrite)==SQLITE_OK);
  assert(vfs->xSetSystemCall(vfs,"pwrite64",(sqlite3_syscall_ptr)fail_pwrite)==SQLITE_OK);
  sb_ml_request r={0};
  r.operation=SB_ML_UPSERT;
  fixture(&r.data.track,999999);
  sb_ml_result result=call(h,&r);
  assert(result.status==SB_ML_IO);
  r=query_request(SB_QUERY_TRACKS);
  assert(call(h,&r).status==SB_ML_OFFLINE);
  assert(vfs->xSetSystemCall(vfs,"write",NULL)==SQLITE_OK);
  assert(vfs->xSetSystemCall(vfs,"pwrite",NULL)==SQLITE_OK);
  assert(vfs->xSetSystemCall(vfs,"pwrite64",NULL)==SQLITE_OK);
  close_library(h);
  printf("injected storage write EIO: source revoked, no destructive rebuild\n");
  return 0;
 }
 if(!strcmp(argv[1],"crash")){
  sb_music_library *h=open_library(argv[2],0,0,0);
  source(h);
  transaction(h,SB_ML_BEGIN,500);
  sb_ml_request r={0};
  r.operation=SB_ML_UPSERT;
  r.transaction=500;
  for(unsigned i=0;i<5000;i++){fixture(&r.data.track,999999+i);
  checked(h,&r);
  }_exit(0);
 }
 if(!strcmp(argv[1],"probe")){
  sb_ml_options options={0};
  snprintf(options.path,sizeof options.path,"%s",argv[2]);
  options.read_only=argc>3?atoi(argv[3]):0;
  options.busy_ms=50;
  sb_music_library *h=sb_music_library_start(&options);
  assert(h);
  sb_ml_result result=wait_result(h);
  printf("%s\n",sb_music_library_status(result.status));
  if(result.status==SB_ML_OK)close_library(h);
  else assert(sb_music_library_destroy(h)==SB_ML_OK);
  return 0;
 }
 if(!strcmp(argv[1],"count")){
  sb_music_library *h=open_library(argv[2],0,0,0);
  source(h);
  sb_ml_request r=query_request(SB_QUERY_TRACKS);
  printf("%u\n",checked(h,&r).data.query.page.total);
  close_library(h);
  return 0;
 }
 return 2;
}
