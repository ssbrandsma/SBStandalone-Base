#include "music_storage_adapter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct sb_storage_adapter {
 sb_storage_worker *worker;sb_storage_model model;sb_storage_discovery discovery;
 uint64_t ingress,event,request,deadline,mount_until;int dirty,inflight,stopping;
 unsigned blocked[SB_STORAGE_MAX];
};
sb_storage_adapter *sb_storage_adapter_start(const sb_storage_worker_options *o,uint64_t incarnation)
{
 if(!o||!o->enabled||!o->fab4||!incarnation)return NULL;
 sb_storage_adapter *a=calloc(1,sizeof *a);if(!a)return NULL;
 a->worker=sb_storage_worker_start(o);if(!a->worker){free(a);return NULL;}
 sb_storage_init(&a->model,1,1,incarnation);a->dirty=1;return a;
}
static void transition(sb_storage_adapter *a,unsigned slot,sb_storage_event_kind kind,sb_storage_status error,uint64_t now)
{
 sb_storage_event e;sb_storage_change change;memset(&e,0,sizeof e);
 e.slot=slot;e.sequence=++a->event;e.kind=kind;e.error=error;e.now_ms=now;
 e.expected_generation=a->model.media[slot].media.generation;
 (void)sb_storage_apply(&a->model,&e,&change);
}
static int device_matches(const char *base,const char *dev)
{
 if(!strcmp(base,dev))return 1;
 size_t n=strlen(dev);if(!n||strncmp(base,dev,n))return 0;
 const char *tail=base+n;if(*tail=='p')tail++;if(!*tail)return 0;
 for(;*tail;tail++)if(*tail<'0'||*tail>'9')return 0;
 return 1;
}
sb_storage_status sb_storage_adapter_eject(sb_storage_adapter *a,unsigned slot,const sb_music_media *media)
{
 if(!a)return SB_STORAGE_DISABLED;
 if(a->stopping||!media||slot>=a->model.count)return SB_STORAGE_STALE;
 sb_storage_medium *s=&a->model.media[slot];
 if(!sb_music_source_equal(&s->media.source,&media->source)||s->media.generation!=media->generation)return SB_STORAGE_STALE;
 if(s->state==SB_STORAGE_QUIESCING)return SB_STORAGE_OK;
 if(!sb_storage_current(&a->model,slot,media))return SB_STORAGE_STALE;
 transition(a,slot,SB_STORAGE_EJECT_REQUEST,SB_STORAGE_OK,0);a->blocked[slot]=1;return SB_STORAGE_OK;
}
sb_storage_status sb_storage_adapter_hint(sb_storage_adapter *a,uint64_t sequence,const char *action,const char *dev)
{
 if(!a)return SB_STORAGE_DISABLED;
 if(a->stopping)return SB_STORAGE_STALE;
 if(!sequence||!action||!dev||strlen(dev)>255||strchr(dev,'/')||strchr(dev,'\\')||
 (strcmp(action,"add")&&strcmp(action,"remove")&&strcmp(action,"change")&&strcmp(action,"reconcile")))return SB_STORAGE_INVALID;
 a->dirty=1;
 if(sequence<=a->ingress)return SB_STORAGE_STALE;
 a->ingress=sequence;
 if(!strcmp(action,"remove"))for(unsigned i=0;i<a->model.count;i++){
  const char *base=strrchr(a->model.media[i].mount.device,'/');
  if(base&&device_matches(base+1,dev)){transition(a,i,SB_STORAGE_REMOVE,SB_STORAGE_OK,0);a->blocked[i]=1;}
 }
 if(!strcmp(action,"add")){
  a->mount_until=UINT64_MAX; /* anchored at next pump */
  for(unsigned i=0;i<a->model.count;i++){const char *base=strrchr(a->model.media[i].mount.device,'/');if(base&&device_matches(base+1,dev))a->blocked[i]=0;}
 }
 return SB_STORAGE_OK;
}
static int equivalent(const sb_storage_mount *a,const sb_storage_mount *b)
{return a->mount_id==b->mount_id&&a->major==b->major&&a->minor==b->minor&&a->read_only==b->read_only&&
 !strcmp(a->device,b->device)&&!strcmp(a->path,b->path)&&!strcmp(a->fs,b->fs)&&!strcmp(a->root,b->root);}
static void reconcile(sb_storage_adapter *a,uint64_t now)
{
 unsigned seen[SB_STORAGE_MAX]={0};sb_storage_discovery *d=&a->discovery;
 if(d->status!=SB_STORAGE_OK){for(unsigned i=0;i<a->model.count;i++)transition(a,i,SB_STORAGE_FAILURE,d->status,now);return;}
 for(unsigned row=0;row<d->mounts.count;row++){
  const sb_storage_mount *m=&d->mounts.rows[row];unsigned slot;
  for(slot=0;slot<a->model.count;slot++)if(!seen[slot]&&!strcmp(a->model.media[slot].mount.path,m->path))break;
  if(slot==SB_STORAGE_MAX)continue;
  seen[slot]=1;sb_storage_medium *s=&a->model.media[slot];
  if(a->blocked[slot]&&equivalent(&s->mount,m))continue;
  a->blocked[slot]=0;
  if(slot==a->model.count||!s->media.online||!equivalent(&s->mount,m)||
   strcmp(s->evidence.uuid,d->evidence[row].uuid)||strcmp(s->evidence.hardware_partition,d->evidence[row].hardware_partition)){
   sb_storage_event e;sb_storage_change change;memset(&e,0,sizeof e);
   e.kind=SB_STORAGE_INSERT;e.slot=slot;e.sequence=++a->event;e.mount=*m;e.evidence=d->evidence[row];
   const char *base=strrchr(m->device,'/');base=base?base+1:m->device;
   e.type=!strncmp(base,"mmc",3)?SB_STORAGE_SD:!strncmp(base,"sd",2)?SB_STORAGE_USB:SB_STORAGE_UNKNOWN;
   if(sb_storage_apply(&a->model,&e,&change)!=SB_STORAGE_OK)continue;
  }
  if(d->validation[row]==SB_STORAGE_OK){
   sb_storage_event e;sb_storage_change change;memset(&e,0,sizeof e);e.kind=SB_STORAGE_VALIDATED;e.slot=slot;e.sequence=++a->event;
   e.expected_generation=s->media.generation;e.mount=*m;e.now_ms=now;(void)sb_storage_apply(&a->model,&e,&change);
  }else transition(a,slot,SB_STORAGE_FAILURE,d->validation[row],now);
 }
 for(unsigned i=0;i<a->model.count;i++)if(!seen[i]&&(a->model.media[i].media.online||a->model.media[i].mounted))transition(a,i,SB_STORAGE_UNMOUNT_OBSERVED,SB_STORAGE_OK,now);
}
sb_storage_status sb_storage_adapter_pump(sb_storage_adapter *a,uint64_t now)
{
 if(!a)return SB_STORAGE_DISABLED;
 if(a->stopping)return SB_STORAGE_STALE;
 if(a->mount_until==UINT64_MAX)a->mount_until=now+60000;
 if(a->inflight&&sb_storage_worker_receive(a->worker,&a->discovery)==SB_STORAGE_OK){
  a->inflight=0;
  /* A notification during discovery makes that result inadmissible. In
   * particular, late validation cannot undo synchronous removal revocation. */
  if(!a->dirty)reconcile(a,now);
  int online=0;for(unsigned i=0;i<a->model.count;i++)online|=a->model.media[i].media.online;
  a->deadline=now<a->mount_until?now+1000:online?now+30000:0;
 }
 if(a->deadline&&now>=a->deadline){a->dirty=1;a->deadline=0;}
 if(a->dirty&&!a->inflight){
  sb_storage_status rc=sb_storage_worker_submit(a->worker,++a->request);
  if(rc!=SB_STORAGE_OK)return rc;
  a->inflight=1;a->dirty=0;
 }return SB_STORAGE_OK;
}
uint64_t sb_storage_adapter_deadline(const sb_storage_adapter *a){return a?a->deadline:0;}
int sb_storage_adapter_fd(const sb_storage_adapter *a){return a?sb_storage_worker_fd(a->worker):-1;}
const sb_storage_model *sb_storage_adapter_state(const sb_storage_adapter *a){return a?&a->model:NULL;}
sb_storage_status sb_storage_adapter_stop(sb_storage_adapter *a)
{
 if(!a)return SB_STORAGE_DISABLED;
 sb_storage_status rc=sb_storage_worker_stop(a->worker);if(rc!=SB_STORAGE_OK)return rc;
 a->stopping=1;for(unsigned i=0;i<a->model.count;i++)transition(a,i,SB_STORAGE_REMOVE,SB_STORAGE_OK,0);return SB_STORAGE_OK;
}
sb_storage_status sb_storage_adapter_destroy(sb_storage_adapter *a)
{if(!a)return SB_STORAGE_DISABLED;
 if(!a->stopping)return SB_STORAGE_BUSY;
 sb_storage_status rc=sb_storage_worker_destroy(a->worker);if(rc==SB_STORAGE_OK)free(a);return rc;}
