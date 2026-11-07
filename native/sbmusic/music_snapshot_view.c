#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "music_snapshot_view.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static uint64_t view_clock(void){struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);return (uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;}
sb_ml_status sb_music_snapshot_refresh(void *context,const char *active,sb_ml_refresh *out)
{
 sb_music_snapshot_view *v=context;
 if(!v||!out||!active)return SB_ML_INVALID;
 memset(out,0,sizeof *out);
 if(v->pending_path[0]){
  if(!strcmp(active,v->pending_path)){
   v->sequence=v->pending_sequence;v->generation=v->pending_generation;v->offline=0;
  }else{
   if(v->corrupt_hint!=v->pending_sequence)fputs("music store: database validation rejected snapshot; previous catalogue retained\n",stderr);
   v->corrupt_hint=v->pending_sequence;v->retry_after=view_clock()+60000;v->offline=1;
  }
  v->pending_path[0]=0;
 }
 sb_storage_mount_reader reader=v->mounts?v->mounts:sb_storage_read_mounts;
 sb_storage_mounts *mounts=calloc(1,sizeof *mounts);
 sb_storage_root root={.fd=-1};sb_music_snapshot snapshot={.directory=-1};int fd=-1;
 sb_ml_status status=SB_ML_OFFLINE;char candidate[256]={0};
 if(!mounts)return SB_ML_LIMIT;
 sb_storage_mount mount={0};
 if(reader(v->mount_context,mounts)!=SB_STORAGE_OK)goto done;
 for(unsigned i=0;i<mounts->count;i++)if(!strcmp(mounts->rows[i].path,v->initial_root.mount.path))mount=mounts->rows[i];
 if(!mount.mount_id)goto done;
 sb_storage_evidence evidence;sb_music_source_id source;
 sb_storage_status identity=v->identity?v->identity(v->identity_context,&mount,&evidence):sb_storage_identity_linux(&mount,&evidence);
 if(identity!=SB_STORAGE_OK||!sb_music_snapshot_source(&evidence,&source)||
    !sb_music_source_equal(&source,&v->initial_root.media.source))goto done;
 uint64_t generation=mount.mount_id;
 if(generation<v->generation)generation=v->generation;
 if(v->offline&&generation<=v->generation){if(generation>=INT64_MAX-1){status=SB_ML_LIMIT;goto done;}generation++;}
 sb_music_media media={source,generation,1};
 const char *parent=!strcmp(mount.fs,"tmpfs")?"/dev":"/media";
 if(sb_storage_validate(parent,&mount,&media,reader,v->mount_context,&root)!=SB_STORAGE_OK)goto done;
 if(v->initial_root.relative[0]) {
  sb_storage_root child={.fd=-1};
  if(sb_storage_root_child(&root,&media,v->initial_root.relative,reader,v->mount_context,&child)!=SB_STORAGE_OK)goto done;
  sb_storage_root_close(&root);root=child;
 }
 if(sb_storage_root_noatime(&root)!=SB_STORAGE_OK)goto done;
 errno=0;
 if(!sb_music_snapshot_open(&snapshot,&root,v->directory,0,v->maximum_bytes,reader,v->mount_context)) {
  if(errno==EWOULDBLOCK||errno==EAGAIN)status=SB_ML_BUSY;
  goto done;
 }
 uint64_t hint=sb_music_snapshot_hint(&snapshot);
 if(!hint){status=SB_ML_CORRUPT;goto done;}
    uint64_t milliseconds=view_clock();
    if(hint==v->corrupt_hint&&milliseconds<v->retry_after){status=SB_ML_CORRUPT;goto done;}
 out->media=media;
 if(hint==v->sequence&&generation==v->generation&&!v->offline){status=SB_ML_OK;goto done;}
 char first[256],second[256];
 if(snprintf(first,sizeof first,"%s/library.db",v->work)>=(int)sizeof first||
    snprintf(second,sizeof second,"%s/library.next",v->work)>=(int)sizeof second){status=SB_ML_LIMIT;goto done;}
 if(strcmp(active,first)&&strcmp(active,second)){status=SB_ML_INVALID;goto done;}
 snprintf(candidate,sizeof candidate,"%s",!strcmp(active,first)?second:first);
 if(unlink(candidate)&&errno!=ENOENT){status=SB_ML_IO;goto done;}
 fd=open(candidate,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0){status=SB_ML_IO;goto done;}
    snapshot.quiet=1;
    errno=0;
    if(sb_music_snapshot_load(&snapshot,fd)!=1||snapshot.damaged){
        if(errno==ENOSPC||errno==EDQUOT||errno==ENOMEM){status=SB_ML_LIMIT;goto done;}
        if(v->corrupt_hint!=hint)fputs("music store: corrupt generation retained; explicit recovery required\n",stderr);
        v->corrupt_hint=hint;v->retry_after=milliseconds+60000;status=SB_ML_CORRUPT;goto done;
    }
    v->corrupt_hint=v->retry_after=0;
 snprintf(out->path,sizeof out->path,"%s",candidate);out->changed=1;
 snprintf(v->pending_path,sizeof v->pending_path,"%s",candidate);
 v->pending_sequence=snapshot.sequence;v->pending_generation=generation;
 status=SB_ML_OK;
done:
 if(fd>=0)close(fd);
 sb_music_snapshot_close(&snapshot);sb_storage_root_close(&root);free(mounts);
 if(status!=SB_ML_OK&&status!=SB_ML_BUSY&&status!=SB_ML_LIMIT&&status!=SB_ML_CANCELLED)v->offline=1;
 return status;
}
