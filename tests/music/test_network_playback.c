#define _POSIX_C_SOURCE 200809L
#include "music_playback.h"
#include "music_stream.h"
#include "music_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
static sb_music_playback *player;
static sb_stream_registry *registry;
static sb_physical_owner owner;
static char token[65];
static unsigned starts;
static int send_frame(void *unused,const unsigned char *frame,size_t size,const sb_music_stream_id *id)
{
 (void)unused;assert(id->track.kind==SB_TRACK_NETWORK&&!id->track.track_id&&id->track.network_id);
 if(size>7&&!memcmp(frame+2,"strm",4)&&frame[6]=='s'){
  starts++;for(size_t i=0;i+78<=size;i++)if(!memcmp(frame+i,"/music/stream/",14)){memcpy(token,frame+i+14,64);token[64]=0;break;}
 }return 1;
}
static void tick(void){uint64_t now=np_monotonic_ms();sb_stream_registry_tick(registry,now);sb_playback_tick(player,now);struct timespec t={0,1000000};nanosleep(&t,NULL);}
static void event(const char *code,unsigned ticks)
{
 sb_slim_event e={.kind=SB_SLIM_STAT,.jiffies=ticks};memcpy(e.code,code,4);
 assert(sb_playback_event(player,sb_playback_stream(player)->connection_generation,sb_playback_stream(player),&e,np_monotonic_ms()));
}
int main(int argc,char **argv)
{
 assert(argc==3);int ipc[2];assert(sb_resource_pair(ipc));assert(sb_owner_init(&owner,1));
 np_manager *np=np_create(120,32);assert(np);registry=sb_stream_registry_start(1,ipc[1]);assert(registry);
 /* No library, mount, local root or SQLite initialization. */
 player=sb_playback_create(1,NULL,NULL,&owner,np,ipc[0],8080,send_frame,NULL);assert(player);
 assert(sb_playback_probe_resources(player,np_monotonic_ms()));uint64_t deadline=np_monotonic_ms()+5000;
 while(!sb_playback_bridge_ready(player)){assert(np_monotonic_ms()<deadline);tick();}
 assert(sb_owner_register(&owner,SB_OWNER_MUSIC,99));sb_slim_hello hello={0};hello.codecs=SB_SLIM_CODEC_MP3;
 assert(sb_playback_attach(player,1,&hello));assert(sb_playback_idle_proof(player,99,1,owner.value.generation,np_monotonic_ms(),1,1,1));
 sb_queue_item item={0};strcpy(item.title,"Network fixture");strcpy(item.network.server,"uuid:fixture");strcpy(item.network.object,"opaque:$1");snprintf(item.network.uri,sizeof item.network.uri,"%s",argv[1]);item.network.generation=1;item.available=item.eligible=1;item.codec='m';
 uint64_t context=sb_queue_snapshot(sb_playback_queue(player)).context;
 /* Stop before the worker/ownership transaction completes must cancel it. */
 assert(sb_playback_load_network(player,SB_Q_LOAD,&item,1,context,np_monotonic_ms())==SB_Q_OK);
 assert(sb_playback_control(player,'q',context,np_monotonic_ms()));
 for(unsigned i=0;i<50;i++)tick();
 assert(starts==0&&owner.value.current!=SB_OWNER_MUSIC);
 context=sb_queue_snapshot(sb_playback_queue(player)).context;
 sb_queue_item tracks[2]={item,item};strcpy(tracks[1].network.object,"opaque:$2");strcpy(tracks[1].title,"Network second");
 assert(sb_playback_load_network(player,SB_Q_LOAD,tracks,2,context,np_monotonic_ms())==SB_Q_OK);
 deadline=np_monotonic_ms()+10000;while(sb_playback_phase(player)!=SB_PLAY_ACTIVE){if(sb_playback_phase(player)==SB_PLAY_FAILED)fprintf(stderr,"%s\n",sb_playback_error(player));assert(np_monotonic_ms()<deadline&&sb_playback_phase(player)!=SB_PLAY_FAILED);tick();}
 assert(starts==1&&*token&&owner.value.current==SB_OWNER_MUSIC);
 assert(sb_queue_at(sb_playback_queue(player),0)->identity.track.kind==SB_TRACK_NETWORK);
 sb_slim_event response={.kind=SB_SLIM_RESP,.http_status=200};assert(sb_playback_event(player,1,sb_playback_stream(player),&response,np_monotonic_ms()));event("STMs",1);
 assert(np_current(np)->state==NP_PLAYING&&!strcmp(np_current(np)->track.title,"Network fixture"));
 unsigned char raw[32];assert(sb_music_token_decode(token,64,raw));sb_music_resource resource;assert(sb_stream_info(registry,raw,SB_RESOURCE_GET,np_monotonic_ms(),&resource)==200&&resource.streaming);
 FILE *expected=fopen(argv[2],"rb");assert(expected);uint64_t offset=0;
 while(offset<resource.size){unsigned wanted=resource.size-offset>SB_STREAM_BUFFER?SB_STREAM_BUFFER:(unsigned)(resource.size-offset);uint64_t job=sb_stream_read(registry,raw,offset,wanted,np_monotonic_ms());assert(job);
  unsigned char data[SB_STREAM_BUFFER],compare[SB_STREAM_BUFFER];unsigned length;int status;deadline=np_monotonic_ms()+5000;
  while(!sb_stream_result(registry,job,np_monotonic_ms(),data,&length,&status)){assert(np_monotonic_ms()<deadline);tick();}
  assert(status==200&&length>0&&length<=wanted);assert(fread(compare,1,length,expected)==length&&!memcmp(compare,data,length));offset+=length;
 }
 fclose(expected);
 sb_music_stream_id old=*sb_playback_stream(player);event("STMd",2);event("STMu",3);
 deadline=np_monotonic_ms()+10000;while(starts!=2||sb_playback_phase(player)!=SB_PLAY_ACTIVE){assert(np_monotonic_ms()<deadline&&sb_playback_phase(player)!=SB_PLAY_FAILED);tick();}
 assert(sb_queue_snapshot(sb_playback_queue(player)).intended==1);
 assert(!sb_playback_event(player,1,&old,&response,np_monotonic_ms()));
 assert(sb_playback_stream(player)->track.network_id!=old.track.network_id);
 assert(sb_playback_event(player,1,sb_playback_stream(player),&response,np_monotonic_ms()));event("STMs",4);
 assert(!strcmp(np_current(np)->track.title,"Network second"));
 for(unsigned i=0;i<30;i++)tick();
 /* A replaced registry slot may forget its tombstone (404); neither
  * response permits reading the retired capability. */
 int retired=sb_stream_info(registry,raw,SB_RESOURCE_GET,np_monotonic_ms(),NULL);assert(retired==404||retired==410);
 assert(sb_music_token_decode(token,64,raw));
 assert(sb_playback_control(player,'p',context,np_monotonic_ms()));event("STMp",5);assert(np_current(np)->state==NP_PAUSED);
 assert(sb_playback_control(player,'u',context,np_monotonic_ms()));event("STMr",6);
 assert(sb_playback_control(player,'q',context,np_monotonic_ms()));for(unsigned i=0;i<30;i++)tick();
 assert(sb_stream_info(registry,raw,SB_RESOURCE_GET,np_monotonic_ms(),NULL)==410);
 assert(owner.value.current==SB_OWNER_MUSIC); /* HTTP cleanup is not release proof */
 assert(sb_playback_idle_proof(player,99,1,owner.value.generation,np_monotonic_ms(),1,1,1));
 while(!sb_playback_destroy(player))tick();
 player=NULL;
 while(!sb_stream_registry_stop(registry)){}while(!sb_stream_registry_destroy(registry)){}
 np_destroy(np);puts("network playback: no SQLite, typed queue, v3 streaming resource, exact HTTP bytes, simulated decoder start/state/pause/resume/stop and revocation passed");return 0;
}
