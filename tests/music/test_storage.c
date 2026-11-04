#define _GNU_SOURCE
#include "music_storage.h"
#include "music_storage_adapter.h"
#include "music_storage_library.h"
#include "third_party/sqlite/sqlite3.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/resource.h>

static uint64_t seq=0;
static double clock_ms(clockid_t id){struct timespec t;clock_gettime(id,&t);return t.tv_sec*1000.+t.tv_nsec/1e6;}
static unsigned rss_kib(void){FILE *f=fopen("/proc/self/status","r");assert(f);char line[256];unsigned n=0;while(fgets(line,sizeof line,f))if(sscanf(line,"VmRSS: %u kB",&n)==1)break;fclose(f);return n;}
static int fds(void){DIR *d=opendir("/proc/self/fd");assert(d);int n=-1;struct dirent *e;while((e=readdir(d)))if(e->d_name[0]!='.')n++;closedir(d);return n;}
static void pause_ms(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static sb_storage_mount fixture_mount(unsigned i)
{
 sb_storage_mount m={0};m.mount_id=50+i;m.major=8;m.minor=i+1;
 strcpy(m.root,"/");snprintf(m.path,sizeof m.path,"/media/fixture%u",i);snprintf(m.device,sizeof m.device,"/dev/sd%c1",'a'+i);strcpy(m.fs,"vfat");return m;
}
static sb_storage_event event(sb_storage_model *m,unsigned slot,sb_storage_event_kind kind)
{
 sb_storage_event e={0};e.slot=slot;e.kind=kind;e.sequence=++seq;e.now_ms=100+seq;
 if(slot<m->count){e.expected_generation=m->media[slot].media.generation;e.mount=m->media[slot].mount;e.evidence=m->media[slot].evidence;}
 return e;
}
static sb_storage_change apply(sb_storage_model *m,sb_storage_event *e)
{sb_storage_change c;assert(sb_storage_apply(m,e,&c)==SB_STORAGE_OK);return c;}
static void insert(sb_storage_model *m,unsigned slot,unsigned identity,int reliable)
{
 sb_storage_event e=event(m,slot,SB_STORAGE_INSERT);e.mount=fixture_mount(slot);e.type=slot?SB_STORAGE_SD:SB_STORAGE_USB;
 if(reliable){snprintf(e.evidence.uuid,sizeof e.evidence.uuid,"uuid%u",identity);snprintf(e.evidence.hardware_partition,sizeof e.evidence.hardware_partition,"serial%u-part1",identity);}
 apply(m,&e);
}
static void validate(sb_storage_model *m,unsigned slot){sb_storage_event e=event(m,slot,SB_STORAGE_VALIDATED);apply(m,&e);}
static void lifecycle(void)
{
 sb_storage_model m;sb_storage_init(&m,1,1,0x12345678);assert(m.count==0);
 insert(&m,0,1,1);assert(m.media[0].state==SB_STORAGE_MOUNT_PENDING&&!m.media[0].media.online);
 sb_music_media first=m.media[0].media;
 /* delayed completion fills actual mount ID without allocating a new generation */
 m.media[0].mount.mount_id=0;sb_storage_event delayed=event(&m,0,SB_STORAGE_VALIDATED);delayed.mount.mount_id=50;apply(&m,&delayed);
 assert(m.media[0].media.generation==first.generation);validate(&m,0);first=m.media[0].media;
 assert(sb_storage_current(&m,0,&first));assert(m.media[0].type==SB_STORAGE_USB&&m.media[0].writable);
 assert(sb_storage_associate_library(&m,0,&first,42)==SB_STORAGE_OK);
 sb_storage_event dup=event(&m,0,SB_STORAGE_INSERT);apply(&m,&dup);assert(m.media[0].media.generation==first.generation);
 sb_storage_change c;assert(sb_storage_apply(&m,&dup,&c)==SB_STORAGE_STALE);
 insert(&m,1,2,1);m.media[1].mount.read_only=1;validate(&m,1);assert(m.count==2&&m.media[1].type==SB_STORAGE_SD&&!m.media[1].writable);
 sb_storage_event e=event(&m,0,SB_STORAGE_REMOVE);c=apply(&m,&e);assert(c.revoked&&!sb_storage_current(&m,0,&first));
 e=event(&m,0,SB_STORAGE_VALIDATED);assert(sb_storage_apply(&m,&e,&c)==SB_STORAGE_STALE);
 insert(&m,0,1,1);m.media[0].mount=fixture_mount(5);validate(&m,0);
 assert(sb_music_source_equal(&m.media[0].media.source,&first.source)&&m.media[0].media.generation>first.generation);
 assert(m.media[0].library_id==42&&sb_storage_associate_library(&m,0,&first,99)==SB_STORAGE_STALE);
 e=event(&m,0,SB_STORAGE_REMOVE);e.expected_generation=first.generation;assert(sb_storage_apply(&m,&e,&c)==SB_STORAGE_STALE);
 e=event(&m,0,SB_STORAGE_EJECT_REQUEST);c=apply(&m,&e);assert(c.revoked&&m.media[0].state==SB_STORAGE_QUIESCING&&m.media[0].mounted);
 e=event(&m,0,SB_STORAGE_UNMOUNT_OBSERVED);apply(&m,&e);assert(!m.media[0].mounted&&m.media[0].state==SB_STORAGE_UNMOUNTED);
 insert(&m,0,3,1);validate(&m,0);assert(!sb_music_source_equal(&first.source,&m.media[0].media.source));
 e=event(&m,0,SB_STORAGE_FAILURE);e.error=SB_STORAGE_IO;apply(&m,&e);assert(!m.media[0].media.online&&m.media[0].error==SB_STORAGE_IO);
 insert(&m,0,3,1);validate(&m,0);first=m.media[0].media;
 e=event(&m,0,SB_STORAGE_INSERT);strcpy(e.evidence.uuid,"changed-filesystem-identity");apply(&m,&e);validate(&m,0);
 assert(!sb_music_source_equal(&first.source,&m.media[0].media.source)&&m.media[0].media.generation>first.generation);
 /* Duplicate UUID with different verified serials stays separate. */
 sb_storage_init(&m,1,1,22);insert(&m,0,1,1);validate(&m,0);
 e=event(&m,1,SB_STORAGE_INSERT);e.mount=fixture_mount(1);e.evidence=m.media[0].evidence;strcpy(e.evidence.hardware_partition,"other-part1");apply(&m,&e);validate(&m,1);
 assert(!sb_music_source_equal(&m.media[0].media.source,&m.media[1].media.source));
 /* Exact composite clones quarantine the old source and taint registry. */
 e=event(&m,2,SB_STORAGE_INSERT);e.mount=fixture_mount(2);e.evidence=m.media[0].evidence;apply(&m,&e);
 assert(!m.media[0].media.online&&m.media[0].error==SB_STORAGE_AMBIGUOUS&&!m.media[2].persistent_identity);
 validate(&m,2);assert(!sb_music_source_equal(&m.media[0].media.source,&m.media[2].media.source));
 sb_storage_init(&m,1,1,23);insert(&m,0,0,0);validate(&m,0);first=m.media[0].media;
 e=event(&m,0,SB_STORAGE_REMOVE);apply(&m,&e);insert(&m,0,0,0);validate(&m,0);
 assert(!m.media[0].persistent_identity&&!sb_music_source_equal(&first.source,&m.media[0].media.source));
 /* Registry survives controller restart only through explicit caller import. */
 sb_storage_init(&m,1,1,24);insert(&m,0,4,1);sb_storage_identity id=m.identities[0];
 sb_storage_init(&m,1,1,25);assert(sb_storage_import_identity(&m,&id)==SB_STORAGE_OK);assert(sb_storage_import_identity(&m,&id)==SB_STORAGE_AMBIGUOUS);insert(&m,0,4,1);assert(sb_music_source_equal(&id.source,&m.media[0].media.source));
 sb_storage_init(&m,0,1,26);e=event(&m,0,SB_STORAGE_INSERT);assert(sb_storage_apply(&m,&e,&c)==SB_STORAGE_DISABLED&&m.count==0);
 sb_storage_init(&m,1,0,27);assert(sb_storage_apply(&m,&e,&c)==SB_STORAGE_DISABLED&&m.count==0);
 sb_storage_init(&m,1,1,28);for(unsigned i=0;i<SB_STORAGE_MAX;i++)insert(&m,i,i,1);
 e=event(&m,SB_STORAGE_MAX,SB_STORAGE_INSERT);assert(sb_storage_apply(&m,&e,&c)==SB_STORAGE_INVALID);
 m.next_generation=INT64_MAX;e=event(&m,0,SB_STORAGE_INSERT);e.mount.mount_id++;assert(sb_storage_apply(&m,&e,&c)==SB_STORAGE_LIMIT);
 puts("lifecycle: USB/SD, two media, delayed, duplicate/stale, removal/reinsert, reuse, UUID uncertainty, registry, gates, limits OK");
}
static void parsers(const char *directory)
{
 char path[2048];const char *files[]={"mountinfo.txt","mounts.txt"};
 sb_storage_mounts *t=malloc(sizeof *t);assert(t);
 for(unsigned i=0;i<2;i++){
  snprintf(path,sizeof path,"%s/%s",directory,files[i]);FILE *f=fopen(path,"rb");assert(f);char b[8192];size_t n=fread(b,1,sizeof b,f);assert(!ferror(f));fclose(f);
  assert(sb_storage_parse(b,n,!i,t)==SB_STORAGE_OK);assert(t->count==3);assert(t->rows[1].read_only);assert(!strcmp(t->rows[1].path,"/media/My Music"));
  assert(sb_storage_parse(b,n-1,!i,t)==SB_STORAGE_INVALID);
 }
 const char *bad="1 0 8:1 / /media/bad\\999 rw - ext3 /dev/sda1 rw\n";assert(sb_storage_parse(bad,strlen(bad),1,t)!=SB_STORAGE_OK);
 bad="1 0 8:1 / /media/bad rw - ext3 /dev/sda1\n";assert(sb_storage_parse(bad,strlen(bad),1,t)==SB_STORAGE_INVALID);
 char nul[]={1,0,2};assert(sb_storage_parse(nul,sizeof nul,1,t)==SB_STORAGE_INVALID);
 free(t);puts("bounded mountinfo/mounts parsing, escapes, read-only, malformed/truncated input OK");
}
typedef struct { sb_storage_mounts table;unsigned calls,disappear;sb_storage_status error; } reader_fixture;
static sb_storage_status read_fixture(void *context,sb_storage_mounts *out)
{reader_fixture *f=context;f->calls++;if(f->error)return f->error;*out=f->table;if(f->disappear&&f->calls>=f->disappear)out->count=0;return SB_STORAGE_OK;}
static void roots(void)
{
 int before=fds();sb_storage_mounts *t=malloc(sizeof *t);assert(t);assert(sb_storage_read_mounts(NULL,t)==SB_STORAGE_OK&&t->has_mount_ids);
 sb_storage_mount mount={0};for(unsigned i=0;i<t->count;i++)if(!strcmp(t->rows[i].path,"/dev/shm"))mount=t->rows[i];free(t);
 assert(mount.mount_id&&strcmp(mount.fs,"tmpfs")==0); /* REAL kernel mount, never mkdir-as-mount */
 sb_music_media media={{{1}},1,1};sb_storage_root root;
 assert(sb_storage_validate("/dev",&mount,&media,NULL,NULL,&root)==SB_STORAGE_OK);
 char temp[]="/dev/shm/sb-storage-XXXXXX";assert(mkdtemp(temp));char file[1200],link[1200],fifo[1200];
 snprintf(file,sizeof file,"%s/fixture",temp);int fd=open(file,O_CREAT|O_EXCL|O_WRONLY,0600);assert(fd>=0);assert(write(fd,"fixture",7)==7);close(fd);
 snprintf(link,sizeof link,"%s/escape",temp);assert(!symlink("/etc/passwd",link));snprintf(fifo,sizeof fifo,"%s/fifo",temp);assert(!mkfifo(fifo,0600));
 char relative[1100];snprintf(relative,sizeof relative,"%s/fixture",strrchr(temp,'/')+1);
 assert(sb_storage_open_relative(&root,&media,relative,0,NULL,NULL,&fd)==SB_STORAGE_OK);char b[8]={0};assert(read(fd,b,7)==7&&!strcmp(b,"fixture"));assert(fcntl(fd,F_GETFD)&FD_CLOEXEC);close(fd);
 sb_storage_root child;
 assert(sb_storage_root_child(&root,&media,strrchr(temp,'/')+1,NULL,NULL,&child)==SB_STORAGE_OK);
 assert(sb_storage_root_noatime(&child)==SB_STORAGE_OK);
 assert(sb_storage_open_relative(&child,&media,"fixture",0,NULL,NULL,&fd)==SB_STORAGE_OK);
 assert(fcntl(fd,F_GETFL)&O_NOATIME);close(fd);sb_storage_root_close(&child);
 assert(sb_storage_root_noatime(NULL)==SB_STORAGE_INVALID);
 const char *bad[]={"../etc/passwd","/etc/passwd","a//b","a/./b","a/../b","a\\b","a/",""};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)assert(sb_storage_open_relative(&root,&media,bad[i],0,NULL,NULL,&fd)==SB_STORAGE_INVALID&&fd==-1);
 snprintf(relative,sizeof relative,"%s/escape",strrchr(temp,'/')+1);assert(sb_storage_open_relative(&root,&media,relative,0,NULL,NULL,&fd)!=SB_STORAGE_OK);
 snprintf(relative,sizeof relative,"%s/fifo",strrchr(temp,'/')+1);assert(sb_storage_open_relative(&root,&media,relative,0,NULL,NULL,&fd)!=SB_STORAGE_OK);
 sb_music_media stale=media;stale.generation++;assert(sb_storage_root_check(&root,&stale,NULL,NULL)==SB_STORAGE_STALE);stale=media;stale.online=0;assert(sb_storage_root_check(&root,&stale,NULL,NULL)==SB_STORAGE_STALE);
 reader_fixture *f=calloc(1,sizeof *f);assert(f);f->table.has_mount_ids=1;f->table.count=1;f->table.rows[0]=mount;
 /* Synthetic authoritative observations exercise failures, NOT positive mount
  * detection from an ordinary directory. Positive open above uses real /proc. */
 f->disappear=2;sb_storage_root vanished;assert(sb_storage_validate("/dev",&mount,&media,read_fixture,f,&vanished)==SB_STORAGE_NOT_MOUNTED&&vanished.fd==-1);
 f->disappear=0;f->calls=0;f->table.rows[0].mount_id++;assert(sb_storage_root_check(&root,&media,read_fixture,f)==SB_STORAGE_STALE);f->table.rows[0]=mount;
 f->table.has_mount_ids=0;assert(sb_storage_root_check(&root,&media,read_fixture,f)==SB_STORAGE_UNSUPPORTED);f->table.has_mount_ids=1;
 f->error=SB_STORAGE_IO;assert(sb_storage_root_check(&root,&media,read_fixture,f)==SB_STORAGE_IO);f->error=SB_STORAGE_OK;
 f->table.count=0;sb_storage_mount ordinary=mount;strcpy(ordinary.path,temp);assert(sb_storage_validate("/dev/shm",&ordinary,&media,read_fixture,f,&vanished)==SB_STORAGE_NOT_MOUNTED);
 sb_storage_mount unsupported=mount;strcpy(unsupported.fs,"exfat");assert(sb_storage_validate("/dev",&unsupported,&media,NULL,NULL,&vanished)==SB_STORAGE_UNSUPPORTED);
 sb_storage_mount symlink_mount=mount;strcpy(symlink_mount.path,link);assert(sb_storage_validate("/dev",&symlink_mount,&media,NULL,NULL,&vanished)!=SB_STORAGE_OK);
 f->table.count=2;f->table.rows[0]=mount;f->table.rows[1]=mount;snprintf(f->table.rows[1].path,sizeof f->table.rows[1].path,"%s",temp);f->table.rows[1].mount_id++;
 snprintf(relative,sizeof relative,"%s/fixture",strrchr(temp,'/')+1);assert(sb_storage_open_relative(&root,&media,relative,0,read_fixture,f,&fd)==SB_STORAGE_INVALID);
 free(f);sb_storage_root_close(&root);unlink(file);unlink(link);unlink(fifo);rmdir(temp);assert(fds()==before);
 puts("real tmpfs root, anchored open, stale directory, traversal/symlink/FIFO, disappearing/reused mount, nested bind, old-source refusal, FD stability OK");
}
static sb_ml_result wait_db(sb_music_library *h)
{
 sb_ml_result r;for(;;){sb_ml_status s=sb_music_library_receive(h,&r);if(s==SB_ML_OK)return r;assert(s==SB_ML_BUSY);struct pollfd p={sb_music_library_fd(h),POLLIN,0};assert(poll(&p,1,10000)>0);}
}
static uint64_t request=0;
static sb_ml_result call_db(sb_music_library *h,sb_ml_request *r)
{r->request_id=++request;while(sb_music_library_submit(h,r)==SB_ML_BUSY)pause_ms();sb_ml_result out=wait_db(h);assert(out.request_id==r->request_id);return out;}
static void publish(sb_music_library *h,const sb_music_media *media)
{uint64_t id=++request;sb_ml_status s;while((s=sb_storage_library_publish(h,media,id,0))==SB_ML_BUSY)pause_ms();assert(s==SB_ML_OK);assert(wait_db(h).status==SB_ML_OK);}
static unsigned revoked;
static void resource_revoked(const sb_music_source_id *id,uint64_t generation){assert(id&&generation);revoked++;}
static void database(const char *path)
{
 sb_ml_options options={0};snprintf(options.path,sizeof options.path,"%s",path);options.create=1;options.busy_ms=200;
 sb_music_library *h=sb_music_library_start(&options);assert(h&&wait_db(h).status==SB_ML_OK);
 sb_storage_model m;sb_storage_init(&m,1,1,400);insert(&m,0,99,1);validate(&m,0);sb_music_media first=m.media[0].media;publish(h,&first);
 sb_ml_request r={0};r.operation=SB_ML_UPSERT;r.data.track.identity.source=first.source;r.data.track.media_generation=first.generation;strcpy(r.data.track.path,"fixture/track.flac");strcpy(r.data.track.title,"Synthetic track");
 sb_ml_result result=call_db(h,&r);assert(result.status==SB_ML_OK);uint64_t track=result.record_id;
 memset(&r,0,sizeof r);r.operation=SB_ML_QUERY;r.data.query.media=first;r.data.query.kind=SB_QUERY_TRACKS;r.data.query.limit=50;result=call_db(h,&r);assert(result.status==SB_ML_OK&&result.data.query.page.total==1&&sb_storage_library_result_current(&m,0,&first,&result));
 sb_storage_event e=event(&m,0,SB_STORAGE_REMOVE);apply(&m,&e);sb_music_resource_provider provider={0};provider.revoke_media=resource_revoked;
 sb_storage_revoke_resources(&provider,&first,&m.media[0].media);assert(revoked==1);assert(!sb_storage_library_result_current(&m,0,&first,&result));publish(h,&m.media[0].media);
 assert(call_db(h,&r).status==SB_ML_OFFLINE);
 insert(&m,0,99,1);m.media[0].mount=fixture_mount(8);validate(&m,0);assert(sb_music_source_equal(&first.source,&m.media[0].media.source));publish(h,&m.media[0].media);
 r.data.query.media=m.media[0].media;result=call_db(h,&r);assert(result.status==SB_ML_OK&&result.data.query.page.total==1&&result.data.query.rows[0].id==track);
 /* Keep SQL pending behind a real exclusive lock. Storage revocation never
  * waits for that lock, and a late completion is inadmissible immediately. */
 sqlite3 *blocker=NULL;assert(sqlite3_open(path,&blocker)==SQLITE_OK);assert(sqlite3_exec(blocker,"BEGIN EXCLUSIVE",NULL,NULL,NULL)==SQLITE_OK);
 r.request_id=++request;while(sb_music_library_submit(h,&r)==SB_ML_BUSY)pause_ms();uint64_t pending=r.request_id;
 e=event(&m,0,SB_STORAGE_REMOVE);apply(&m,&e);double start=clock_ms(CLOCK_MONOTONIC);
 assert(sb_storage_library_publish(h,&m.media[0].media,++request,pending)==SB_ML_BUSY);assert(clock_ms(CLOCK_MONOTONIC)-start<100);
 assert(!sb_storage_library_result_current(&m,0,&r.data.query.media,&result));assert(sqlite3_exec(blocker,"ROLLBACK",NULL,NULL,NULL)==SQLITE_OK);assert(sqlite3_close(blocker)==SQLITE_OK);
 result=wait_db(h);assert(result.request_id==pending);assert(!sb_storage_library_result_current(&m,0,&r.data.query.media,&result));publish(h,&m.media[0].media);
 insert(&m,0,99,1);validate(&m,0);publish(h,&m.media[0].media);
 /* Offline transition aborts an open transaction without deleting committed rows. */
 memset(&r,0,sizeof r);r.operation=SB_ML_BEGIN;r.transaction=7;assert(call_db(h,&r).status==SB_ML_OK);
 e=event(&m,0,SB_STORAGE_REMOVE);apply(&m,&e);publish(h,&m.media[0].media);r.operation=SB_ML_COMMIT;assert(call_db(h,&r).status==SB_ML_STALE);
 memset(&r,0,sizeof r);r.operation=SB_ML_CLOSE;assert(call_db(h,&r).status==SB_ML_OK);assert(sb_music_library_destroy(h)==SB_ML_OK);
 puts("WP1 identity/track preservation, offline visibility, late completion guard, resource revocation, rollback bridge OK");
}
static void wait_worker(sb_storage_worker *w,sb_storage_discovery *out)
{for(;;){sb_storage_status s=sb_storage_worker_receive(w,out);if(s==SB_STORAGE_OK)return;assert(s==SB_STORAGE_BUSY);struct pollfd p={sb_storage_worker_fd(w),POLLIN,0};assert(poll(&p,1,5000)>0);}}
static sb_storage_status submit_worker(sb_storage_worker *w,uint64_t id)
{sb_storage_status s;while((s=sb_storage_worker_submit(w,id))==SB_STORAGE_BUSY)pause_ms();return s;}
static void pump_adapter(sb_storage_adapter *a,uint64_t now)
{sb_storage_status s;while((s=sb_storage_adapter_pump(a,now))==SB_STORAGE_BUSY)pause_ms();assert(s==SB_STORAGE_OK);}
static void workers(void)
{
 int before=fds();sb_storage_worker_options o={0};strcpy(o.approved_parent,"/media");assert(!sb_storage_worker_start(&o));o.enabled=1;assert(!sb_storage_worker_start(&o));assert(!sb_storage_adapter_start(&o,1));o.fab4=1;
 reader_fixture *f=calloc(1,sizeof *f);assert(f);f->table.has_mount_ids=1;o.reader=read_fixture;o.reader_context=f;
 sb_storage_worker *w=sb_storage_worker_start(&o);assert(w);sb_storage_discovery *d=malloc(sizeof *d);assert(d);
 assert(submit_worker(w,1)==SB_STORAGE_OK);wait_worker(w,d);assert(d->status==SB_STORAGE_OK&&!d->mounts.count);assert(submit_worker(w,1)==SB_STORAGE_STALE);
 assert(sb_storage_worker_destroy(w)==SB_STORAGE_BUSY);
 while(sb_storage_worker_stop(w)==SB_STORAGE_BUSY)pause_ms();while(sb_storage_worker_destroy(w)==SB_STORAGE_BUSY)pause_ms();
 sb_storage_adapter *a=sb_storage_adapter_start(&o,99);assert(a);pump_adapter(a,100);
 for(unsigned i=0;i<50;i++){pause_ms();sb_storage_adapter_pump(a,100+i);}
 assert(!sb_storage_adapter_state(a)->count&&!sb_storage_adapter_deadline(a));
 for(unsigned i=1;i<=200;i++)assert(sb_storage_adapter_hint(a,i,"add","sda1")==SB_STORAGE_OK);
 assert(sb_storage_adapter_hint(a,1,"remove","sda1")==SB_STORAGE_STALE);pump_adapter(a,1000);
 for(unsigned i=0;i<50;i++){pause_ms();sb_storage_adapter_pump(a,1000+i);}
 assert(sb_storage_adapter_deadline(a)>1000);assert(f->calls==3); /* one probe + startup + one batch */
 printf("coalescing: 200 hints, %u reads (isolated probe + adapter startup + hint batch)\n",f->calls);
 while(sb_storage_adapter_stop(a)==SB_STORAGE_BUSY)pause_ms();while(sb_storage_adapter_destroy(a)==SB_STORAGE_BUSY)pause_ms();
 free(f);free(d);assert(fds()==before);puts("disabled/Baby zero resources, asynchronous worker, request IDs, duplicate coalescing, delayed deadline, no-media idle OK");
}
typedef struct {pthread_mutex_t lock;pthread_cond_t condition;int gate,entered;sb_storage_mounts table;} gated_fixture;
static sb_storage_status gated_read(void *context,sb_storage_mounts *out)
{
 gated_fixture *g=context;pthread_mutex_lock(&g->lock);
 if(g->gate){g->entered=1;pthread_cond_signal(&g->condition);while(g->gate)pthread_cond_wait(&g->condition,&g->lock);}
 *out=g->table;pthread_mutex_unlock(&g->lock);return SB_STORAGE_OK;
}
static void pump_until(sb_storage_adapter *a,int online)
{
 for(unsigned i=0;i<1000;i++){pump_adapter(a,200);const sb_storage_model *m=sb_storage_adapter_state(a);if(m->count&&!!m->media[0].media.online==online)return;pause_ms();}assert(0);
}
static void adapter_lifecycle(void)
{
 int before=fds();gated_fixture *g=calloc(1,sizeof *g);assert(g);pthread_mutex_init(&g->lock,NULL);pthread_cond_init(&g->condition,NULL);
 sb_storage_mounts *real=malloc(sizeof *real);assert(real);assert(sb_storage_read_mounts(NULL,real)==SB_STORAGE_OK);
 for(unsigned i=0;i<real->count;i++)if(!strcmp(real->rows[i].path,"/dev/shm"))g->table.rows[0]=real->rows[i];free(real);
 assert(g->table.rows[0].mount_id);g->table.count=1;g->table.has_mount_ids=1;
 /* Injected OS device name on an ACTUAL kernel-mounted tmpfs: state-machine
  * integration only, not evidence of USB support on the host or target. */
 strcpy(g->table.rows[0].device,"/dev/sda1");
 sb_storage_worker_options o={0};o.enabled=o.fab4=1;strcpy(o.approved_parent,"/dev");o.reader=gated_read;o.reader_context=g;
 sb_storage_adapter *a=sb_storage_adapter_start(&o,666);assert(a);pump_until(a,1);
 sb_music_media first=sb_storage_adapter_state(a)->media[0].media;
 pthread_mutex_lock(&g->lock);g->gate=1;g->entered=0;pthread_mutex_unlock(&g->lock);
 assert(sb_storage_adapter_hint(a,1,"change","sda1")==SB_STORAGE_OK);pump_adapter(a,200);
 pthread_mutex_lock(&g->lock);while(!g->entered)pthread_cond_wait(&g->condition,&g->lock);pthread_mutex_unlock(&g->lock);
 assert(sb_storage_adapter_hint(a,2,"remove","sda1")==SB_STORAGE_OK);assert(!sb_storage_current(sb_storage_adapter_state(a),0,&first));
 pthread_mutex_lock(&g->lock);g->gate=0;pthread_cond_signal(&g->condition);pthread_mutex_unlock(&g->lock);
 for(unsigned i=0;i<30;i++){sb_storage_adapter_pump(a,200);pause_ms();}assert(!sb_storage_adapter_state(a)->media[0].media.online);
 assert(sb_storage_adapter_hint(a,1,"add","sda1")==SB_STORAGE_STALE);for(unsigned i=0;i<20;i++){sb_storage_adapter_pump(a,200);pause_ms();}assert(!sb_storage_adapter_state(a)->media[0].media.online);
 assert(sb_storage_adapter_hint(a,3,"add","sda1")==SB_STORAGE_OK);pump_until(a,1);assert(sb_storage_adapter_state(a)->media[0].media.generation>first.generation);
 /* Mountpoint reuse by different mount/device, and a read-only transition. */
 first=sb_storage_adapter_state(a)->media[0].media;
 pthread_mutex_lock(&g->lock);g->table.rows[0].mount_id++;strcpy(g->table.rows[0].device,"/dev/sdb1");g->table.rows[0].read_only=1;pthread_mutex_unlock(&g->lock);
 assert(sb_storage_adapter_hint(a,4,"change","sdb1")==SB_STORAGE_OK);
 for(unsigned i=0;i<50;i++){sb_storage_adapter_pump(a,200);pause_ms();}
 assert(!sb_storage_current(sb_storage_adapter_state(a),0,&first)&&!sb_storage_adapter_state(a)->media[0].writable&&sb_storage_adapter_state(a)->media[0].media.online);
 first=sb_storage_adapter_state(a)->media[0].media;
 assert(sb_storage_adapter_eject(a,0,&first)==SB_STORAGE_OK);assert(sb_storage_adapter_eject(a,0,&first)==SB_STORAGE_OK);
 assert(sb_storage_adapter_state(a)->media[0].state==SB_STORAGE_QUIESCING&&!sb_storage_adapter_state(a)->media[0].media.online);
 while(sb_storage_adapter_stop(a)==SB_STORAGE_BUSY)pause_ms();while(sb_storage_adapter_destroy(a)==SB_STORAGE_BUSY)pause_ms();
 pthread_cond_destroy(&g->condition);pthread_mutex_destroy(&g->lock);free(g);assert(fds()==before);
 puts("Fab4 adapter: removal during blocked discovery, stale result discard, tombstone, stale add, new generation, mountpoint reuse/read-only OK");
}
static void performance(void)
{
 sb_storage_model m;sb_storage_init(&m,1,1,500);insert(&m,0,1,1);validate(&m,0);
 double start=clock_ms(CLOCK_MONOTONIC),cpu=clock_ms(CLOCK_PROCESS_CPUTIME_ID);unsigned n=100000;sb_storage_change c;
 for(unsigned i=0;i<n;i++){sb_storage_event e=event(&m,0,SB_STORAGE_INSERT);assert(sb_storage_apply(&m,&e,&c)==SB_STORAGE_OK);}
 double elapsed=clock_ms(CLOCK_MONOTONIC)-start,used=clock_ms(CLOCK_PROCESS_CPUTIME_ID)-cpu;
 sb_storage_mounts *mounts=malloc(sizeof *mounts);assert(mounts);start=clock_ms(CLOCK_MONOTONIC);assert(sb_storage_read_mounts(NULL,mounts)==SB_STORAGE_OK);double discovery=clock_ms(CLOCK_MONOTONIC)-start;
 sb_storage_mount shm={0};for(unsigned i=0;i<mounts->count;i++)if(!strcmp(mounts->rows[i].path,"/dev/shm"))shm=mounts->rows[i];free(mounts);
 sb_storage_root root;sb_music_media media={{{1}},1,1};start=clock_ms(CLOCK_MONOTONIC);assert(sb_storage_validate("/dev",&shm,&media,NULL,NULL,&root)==SB_STORAGE_OK);double validation=clock_ms(CLOCK_MONOTONIC)-start;sb_storage_root_close(&root);
 int fd_before=fds();unsigned rss_before=rss_kib();
 sb_storage_worker_options o={0};o.enabled=o.fab4=1;strcpy(o.approved_parent,"/media");sb_storage_worker *w=sb_storage_worker_start(&o);assert(w);pause_ms();
 unsigned rss_idle=rss_kib();int fd_enabled=fds();
 cpu=clock_ms(CLOCK_PROCESS_CPUTIME_ID);struct timespec idle={0,200000000};nanosleep(&idle,NULL);double idlecpu=clock_ms(CLOCK_PROCESS_CPUTIME_ID)-cpu;
 double max_submit=0;sb_storage_discovery *d=malloc(sizeof *d);assert(d);
 for(unsigned i=1;i<=20;i++){start=clock_ms(CLOCK_MONOTONIC);sb_storage_status s;do{s=sb_storage_worker_submit(w,i);}while(s==SB_STORAGE_BUSY);assert(s==SB_STORAGE_OK);double t=clock_ms(CLOCK_MONOTONIC)-start;if(t>max_submit)max_submit=t;wait_worker(w,d);}
 unsigned rss_discovered=rss_kib();free(d);while(sb_storage_worker_stop(w)==SB_STORAGE_BUSY)pause_ms();while(sb_storage_worker_destroy(w)==SB_STORAGE_BUSY)pause_ms();
 struct rusage usage;getrusage(RUSAGE_SELF,&usage);
 printf("{\"events\":%u,\"event_total_ms\":%.6f,\"event_cpu_ms\":%.6f,\"discovery_ms\":%.6f,\"validation_ms\":%.6f,\"idle_200ms_cpu_ms\":%.6f,\"max_submit_ms\":%.6f,\"rss_peak_kib\":%ld,\"rss_before_worker_kib\":%u,\"rss_idle_worker_kib\":%u,\"rss_after_discovery_kib\":%u,\"model_bytes\":%zu,\"discovery_bytes\":%zu,\"fds_before\":%d,\"fds_enabled\":%d,\"fds_final\":%d}\n",n,elapsed,used,discovery,validation,idlecpu,max_submit,usage.ru_maxrss,rss_before,rss_idle,rss_discovered,sizeof m,sizeof(sb_storage_discovery),fd_before,fd_enabled,fds());
}
int main(int argc,char **argv)
{assert(argc>=2);if(!strcmp(argv[1],"bench")){performance();return 0;}assert(argc==4);int before=fds();lifecycle();parsers(argv[2]);roots();workers();adapter_lifecycle();database(argv[3]);assert(fds()==before);puts("WP6a storage foundation OK");return 0;}
