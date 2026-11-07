#define _GNU_SOURCE
#include "music_snapshot_view.h"
#include "music_async.h"
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
/* Never assert() a call: setup must run even when NDEBUG compiles assert out. */
#define CHECK(x) do{if(!(x)){fprintf(stderr,"snapshot_view_driver: check failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int offline,foreign;
static const char *selected;
static uint64_t milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void pause_ms(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static sb_storage_status mounts(void *context,sb_storage_mounts *table) {
 (void)context;sb_storage_status status=sb_storage_read_mounts(NULL,table);
 if(offline)for(unsigned i=0;i<table->count;i++)if(!strcmp(table->rows[i].path,"/dev/shm")){
  memmove(table->rows+i,table->rows+i+1,(table->count-i-1)*sizeof table->rows[0]);table->count--;break;
 }
 return status;
}
static sb_storage_status identity(void *context,const sb_storage_mount *mount,sb_storage_evidence *e) {
 (void)context;(void)mount;memset(e,0,sizeof *e);
 snprintf(e->uuid,sizeof e->uuid,"host-fixture");
 snprintf(e->hardware_partition,sizeof e->hardware_partition,"%s%s",selected,foreign?"-foreign":"");return SB_STORAGE_OK;
}
int main(int argc,char **argv) {
 CHECK(argc==2);selected=argv[1];setvbuf(stdout,NULL,_IONBF,0);
 sb_storage_mounts *table=calloc(1,sizeof *table);CHECK(table&&mounts(NULL,table)==SB_STORAGE_OK);
 sb_storage_mount mount={0};for(unsigned i=0;i<table->count;i++)if(!strcmp(table->rows[i].path,"/dev/shm"))mount=table->rows[i];free(table);
 sb_storage_evidence evidence;identity(NULL,&mount,&evidence);
 sb_music_media media={0};CHECK(sb_music_snapshot_source(&evidence,&media.source));media.generation=mount.mount_id;media.online=1;
 sb_storage_root whole,root;CHECK(sb_storage_validate("/dev",&mount,&media,mounts,NULL,&whole)==SB_STORAGE_OK);
 CHECK(sb_storage_root_child(&whole,&media,selected+strlen(mount.path)+1,mounts,NULL,&root)==SB_STORAGE_OK);sb_storage_root_close(&whole);
 CHECK(sb_storage_root_noatime(&root)==SB_STORAGE_OK);
 sb_music_snapshot snapshot;CHECK(sb_music_snapshot_open(&snapshot,&root,".standalone-music",0,4194304,mounts,NULL));
 sb_music_snapshot_view view={0};view.initial_root=root;view.maximum_bytes=4194304;view.generation=media.generation;
 view.mounts=mounts;view.identity=identity;strcpy(view.directory,".standalone-music");strcpy(view.work,"/dev/shm/sb-view.XXXXXX");CHECK(mkdtemp(view.work));
 sb_ml_options options={0};snprintf(options.path,sizeof options.path,"%s/library.db",view.work);
 int fd=open(options.path,O_CREAT|O_EXCL|O_RDWR,0600);CHECK(fd>=0&&sb_music_snapshot_load(&snapshot,fd)==1);close(fd);
 view.sequence=snapshot.sequence;sb_music_snapshot_close(&snapshot);
 options.read_only=1;options.query_ms=5000;options.refresh=sb_music_snapshot_refresh;options.refresh_context=&view;
 sb_music_library *library=sb_music_library_start(&options);CHECK(library);
 sb_ml_result result;while(sb_music_library_receive(library,&result)==SB_ML_BUSY){struct pollfd p={sb_music_library_fd(library),POLLIN,0};CHECK(poll(&p,1,10000)>0);}CHECK(result.status==SB_ML_OK);
 sb_ml_request request={.operation=SB_ML_SOURCE};request.data.media=media;
 CHECK(sb_music_library_exchange(library,&request,&result)==SB_ML_OK&&result.status==SB_ML_OK);puts("READY");
 sb_music_async *dispatcher=sb_music_async_start(library,1);CHECK(dispatcher);
 char line[64];
 while(fgets(line,sizeof line,stdin)) {
  line[strcspn(line,"\r\n")]=0;if(!strcmp(line,"exit"))break;
  if(!strcmp(line,"offline"))offline=1;
  if(!strcmp(line,"online")){offline=foreign=0;}
  if(!strcmp(line,"foreign"))foreign=1;
  request=(sb_ml_request){.operation=SB_ML_REFRESH};
  if(!strcmp(line,"summary")){request.operation=SB_ML_BROWSE_SUMMARY;request.data.query.media=media;request.data.query.limit=1;}
  uint64_t id=0;
  while(sb_music_async_submit(dispatcher,&request,milliseconds()+5000,&id)==SB_ML_BUSY)pause_ms();
  CHECK(id);
  while(sb_music_async_poll(dispatcher,id,&result)==SB_ML_BUSY)pause_ms();
  if(request.operation==SB_ML_REFRESH&&result.status==SB_ML_OK)media=result.data.media;
  printf("{\"status\":%d,\"revision\":%llu,\"changed\":%llu,\"generation\":%llu,\"tracks\":%u}\n",result.status,
   (unsigned long long)result.revision,(unsigned long long)result.record_id,(unsigned long long)media.generation,
   request.operation==SB_ML_BROWSE_SUMMARY?result.data.summary.tracks:0);
 }
 while(sb_music_async_stop(dispatcher)==SB_ML_BUSY)pause_ms();
 while(sb_music_async_destroy(dispatcher)==SB_ML_BUSY)pause_ms();
 request=(sb_ml_request){.operation=SB_ML_CLOSE};CHECK(sb_music_library_exchange(library,&request,&result)==SB_ML_OK);
 CHECK(sb_music_library_destroy(library)==SB_ML_OK);sb_storage_root_close(&root);
 char path[256];snprintf(path,sizeof path,"%s/library.db",view.work);unlink(path);snprintf(path,sizeof path,"%s/library.next",view.work);unlink(path);CHECK(!rmdir(view.work));
 return 0;
}
