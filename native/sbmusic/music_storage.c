#include "music_storage.h"
#include <string.h>
#include <limits.h>

static int text(const char *s, size_t n) { return memchr(s,0,n)!=NULL; }
static int same_evidence(const sb_storage_evidence *a,const sb_storage_evidence *b)
{ return !strcmp(a->uuid,b->uuid)&&!strcmp(a->hardware_partition,b->hardware_partition); }
static int reliable(const sb_storage_evidence *e) { return e->uuid[0]&&e->hardware_partition[0]; }
static int mount_equal(const sb_storage_mount *a,const sb_storage_mount *b)
{ return a->mount_id==b->mount_id&&a->major==b->major&&a->minor==b->minor&&
 !strcmp(a->path,b->path)&&!strcmp(a->root,b->root)&&!strcmp(a->device,b->device)&&!strcmp(a->fs,b->fs)&&a->read_only==b->read_only; }
void sb_storage_init(sb_storage_model *m,int enabled,int fab4,uint64_t incarnation)
{ memset(m,0,sizeof *m);m->enabled=!!enabled;m->has_storage=!!fab4;m->incarnation=incarnation; }
static int new_source(sb_storage_model *m,sb_music_source_id *id)
{
 for(unsigned attempt=0;attempt<SB_STORAGE_MAX*2+1;attempt++){
  if(!m->incarnation||m->next_identity==UINT64_MAX)return 0;
  uint64_t n=++m->next_identity;
  for(unsigned i=0;i<8;i++){id->bytes[i]=(unsigned char)(m->incarnation>>(i*8));id->bytes[i+8]=(unsigned char)(n>>(i*8));}
  int collision=0;
  for(unsigned i=0;i<m->identity_count;i++)collision|=sb_music_source_equal(id,&m->identities[i].source);
  for(unsigned i=0;i<m->count;i++)collision|=sb_music_source_equal(id,&m->media[i].media.source);
  if(!collision)return 1;
 }return 0;
}
sb_storage_status sb_storage_import_identity(sb_storage_model *m,const sb_storage_identity *id)
{
 if(!m||!id||!m->enabled||!m->has_storage)return SB_STORAGE_DISABLED;
 if(!text(id->evidence.uuid,sizeof id->evidence.uuid)||!text(id->evidence.hardware_partition,sizeof id->evidence.hardware_partition)||
 !reliable(&id->evidence)||!sb_music_source_equal(&id->source,&id->source))return SB_STORAGE_INVALID;
 for(unsigned i=0;i<m->identity_count;i++)if(same_evidence(&m->identities[i].evidence,&id->evidence)||sb_music_source_equal(&m->identities[i].source,&id->source))return SB_STORAGE_AMBIGUOUS;
 if(m->identity_count==SB_STORAGE_MAX)return SB_STORAGE_LIMIT;
 m->identities[m->identity_count++]=*id;return SB_STORAGE_OK;
}
int sb_storage_current(const sb_storage_model *m,unsigned slot,const sb_music_media *media)
{ return m&&m->enabled&&m->has_storage&&slot<m->count&&m->media[slot].state==SB_STORAGE_AVAILABLE&&sb_music_media_current(&m->media[slot].media,media); }
sb_storage_status sb_storage_associate_library(sb_storage_model *m,unsigned slot,const sb_music_media *media,uint64_t library_id)
{
 if(!sb_storage_current(m,slot,media))return SB_STORAGE_STALE;
 m->media[slot].library_id=library_id;
 for(unsigned i=0;i<m->identity_count;i++)if(sb_music_source_equal(&m->identities[i].source,&media->source))m->identities[i].library_id=library_id;
 return SB_STORAGE_OK;
}
static sb_storage_status identity(sb_storage_model *m,sb_storage_medium *s,const sb_storage_evidence *e)
{
 s->evidence=*e;s->persistent_identity=0;
 if(reliable(e)){
  for(unsigned i=0;i<m->identity_count;i++)if(same_evidence(e,&m->identities[i].evidence)){
   if(!m->identities[i].tainted){s->media.source=m->identities[i].source;s->library_id=m->identities[i].library_id;s->persistent_identity=1;return SB_STORAGE_OK;}
   return new_source(m,&s->media.source)?SB_STORAGE_OK:SB_STORAGE_LIMIT;
  }
  if(m->identity_count==SB_STORAGE_MAX)return SB_STORAGE_LIMIT;
  if(!new_source(m,&s->media.source))return SB_STORAGE_LIMIT;
  sb_storage_identity *r=&m->identities[m->identity_count++];r->source=s->media.source;r->evidence=*e;r->tainted=0;s->persistent_identity=1;return SB_STORAGE_OK;
 }
 return new_source(m,&s->media.source)?SB_STORAGE_OK:SB_STORAGE_LIMIT;
}
sb_storage_status sb_storage_apply(sb_storage_model *m,const sb_storage_event *e,sb_storage_change *change)
{
 if(change)memset(change,0,sizeof *change);
 if(!m||!e||!change)return SB_STORAGE_INVALID;
 if(!m->enabled||!m->has_storage)return SB_STORAGE_DISABLED;
 if(e->slot>=SB_STORAGE_MAX||e->slot>m->count||!e->sequence||e->kind<SB_STORAGE_INSERT||e->kind>SB_STORAGE_FAILURE||e->type<SB_STORAGE_UNKNOWN||e->type>SB_STORAGE_SD||
 !text(e->mount.path,sizeof e->mount.path)||!text(e->mount.root,sizeof e->mount.root)||!text(e->mount.device,sizeof e->mount.device)||!text(e->mount.fs,sizeof e->mount.fs)||
 !text(e->evidence.uuid,sizeof e->evidence.uuid)||!text(e->evidence.hardware_partition,sizeof e->evidence.hardware_partition))return SB_STORAGE_INVALID;
 sb_storage_medium *s=&m->media[e->slot];
 if(e->kind==SB_STORAGE_VALIDATED&&!e->mount.mount_id)return SB_STORAGE_INVALID;
 if(e->sequence<=s->event_sequence)return SB_STORAGE_STALE;
 if(e->kind!=SB_STORAGE_INSERT&&(!s->media.generation||e->expected_generation!=s->media.generation))return SB_STORAGE_STALE;
 if(e->slot==m->count&&e->kind!=SB_STORAGE_INSERT)return SB_STORAGE_STALE;
 change->previous=s->media;
 if(e->kind==SB_STORAGE_INSERT){
  if(s->state==SB_STORAGE_MOUNT_PENDING||s->state==SB_STORAGE_AVAILABLE){
   if(mount_equal(&s->mount,&e->mount)&&same_evidence(&s->evidence,&e->evidence)){s->event_sequence=e->sequence;return SB_STORAGE_OK;}
  }
  if(m->next_generation>=INT64_MAX)return SB_STORAGE_LIMIT;
  sb_storage_medium replacement;memset(&replacement,0,sizeof replacement);
  sb_storage_status rc=identity(m,&replacement,&e->evidence);if(rc!=SB_STORAGE_OK)return rc;
  /* A composite identity already in use is a clone/collision. Fail closed;
   * keep the original medium offline until an explicit new insertion. */
  for(unsigned i=0;i<m->count;i++)if(i!=e->slot&&m->media[i].media.online&&replacement.persistent_identity&&same_evidence(&m->media[i].evidence,&e->evidence)){
   for(unsigned j=0;j<m->identity_count;j++)if(same_evidence(&m->identities[j].evidence,&e->evidence))m->identities[j].tainted=1;
   /* Caller must reconcile ALL model snapshots after any event, including this
    * collision, to publish revocations for affected other slots. */
   m->media[i].media.online=0;m->media[i].state=SB_STORAGE_ERROR;m->media[i].error=SB_STORAGE_AMBIGUOUS;
   replacement.persistent_identity=0;if(!new_source(m,&replacement.media.source))return SB_STORAGE_LIMIT;
  }
  replacement.media.generation=++m->next_generation;replacement.mount=e->mount;replacement.type=e->type;
  replacement.mounted=e->mount.mount_id!=0;
  replacement.state=SB_STORAGE_MOUNT_PENDING;replacement.event_sequence=e->sequence;*s=replacement;
  if(e->slot==m->count)m->count++;
 }else{
  s->event_sequence=e->sequence;
  switch(e->kind){
   case SB_STORAGE_VALIDATED:
    if(s->state!=SB_STORAGE_MOUNT_PENDING&&s->state!=SB_STORAGE_AVAILABLE)return SB_STORAGE_STALE;
    if(!mount_equal(&s->mount,&e->mount)){
     if(s->mount.mount_id||strcmp(s->mount.path,e->mount.path)||strcmp(s->mount.device,e->mount.device))return SB_STORAGE_STALE;
     s->mount=e->mount;
    }
    s->media.online=1;s->mounted=1;s->writable=!e->mount.read_only;s->state=SB_STORAGE_AVAILABLE;s->error=SB_STORAGE_OK;s->validated_ms=e->now_ms;break;
   case SB_STORAGE_REMOVE:s->media.online=0;s->mounted=0;s->writable=0;s->state=SB_STORAGE_OFFLINE;break;
   case SB_STORAGE_EJECT_REQUEST:s->media.online=0;s->writable=0;s->state=SB_STORAGE_QUIESCING;break;
   case SB_STORAGE_UNMOUNT_OBSERVED:s->media.online=0;s->mounted=0;s->writable=0;s->state=SB_STORAGE_UNMOUNTED;break;
   case SB_STORAGE_FAILURE:s->media.online=0;s->writable=0;s->state=SB_STORAGE_UNAVAILABLE;s->error=e->error;break;
   default: return SB_STORAGE_INVALID;
  }
 }
 change->current=s->media;change->changed=1;
 change->revoked=change->previous.online&&!sb_music_media_current(&change->previous,&change->current);
 return SB_STORAGE_OK;
}
