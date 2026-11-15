/* Authenticated, asynchronous gateway on Base's existing loopback listener.
 * The daemon's same-uid private Unix channel never becomes a LAN fetch API. */
static uint64_t upnp_sequence;
static const char *upnp_socket_path(void){const char *p=getenv("SB_UPNP_SOCKET");return p&&*p?p:SB_UPNP_SOCKET;}
static int upnp_auth(const char *headers,size_t length)
{
 char path[128],expected[65]={0};const char *socket=upnp_socket_path();if(strlen(socket)>100)return 0;strcpy(path,socket);char *slash=strrchr(path,'/');if(!slash)return 0;strcpy(slash+1,"token");
 int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return 0;struct stat st;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0077)&&st.st_size==64&&read(fd,expected,64)==64;close(fd);if(!ok)return 0;
 const char *p=headers,*end=headers+length;unsigned found=0,difference=0;
 while(p<end){const char *e=memchr(p,'\n',(size_t)(end-p));if(!e)break;
  static const char key[]="X-Standalone-UPnP:";
  if((size_t)(e-p)>sizeof key-1&&!strncasecmp(p,key,sizeof key-1)){
   const char *v=p+sizeof key-1;while(v<e&&(*v==' '||*v=='\t'))v++;const char *last=e;if(last>v&&last[-1]=='\r')last--;
   if(last-v!=64)return 0;
   found++;for(unsigned i=0;i<64;i++)difference|=(unsigned char)v[i]^(unsigned char)expected[i];
  }p=e+1;
 }return found==1&&!difference;
}
static int upnp_number(const char *s,uint64_t maximum,uint64_t *out)
{
 if(!s||!*s)return 0;
 uint64_t n=0;for(;*s;s++){if(*s<'0'||*s>'9'||n>(maximum-(unsigned)(*s-'0'))/10)return 0;n=n*10+(unsigned)(*s-'0');}*out=n;return 1;
}
static int upnp_tag(const sb_lms_command *c,const char *tag,char *out,size_t cap)
{
 const char *s=sb_lms_tag(c,tag);if(!s){out[0]=0;return 1;}if(strlen(s)>=cap)return 0;strcpy(out,s);return 1;
}
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
static int upnp_playback_ready(void)
{
 if(!local_music||!np_player_power(nowplaying)||local_owner.suspended||!(sb_playback_codecs(local_music)&SB_SLIM_CODEC_MP3))return 0;
#ifdef SB_MUSIC_RUNTIME
 if(!ownership_bridge.enabled||!ownership_bridge.idle_admitted||!local_owner.registrations[SB_OWNER_MUSIC]||!music_safety_allowed())return 0;
#endif
 return 1;
}
#endif
static int upnp_renderer(client *c,const sb_lms_command *cmd,const char *operation)
{
 if(strcmp(operation,"renderplay")&&strcmp(operation,"rendercontrol")&&strcmp(operation,"renderstate"))return 0;
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
 if(local_music){
  sb_queue_status q=sb_queue_snapshot(sb_playback_queue(local_music));
  const sb_queue_item *item=sb_queue_at(sb_playback_queue(local_music),q.audible!=SB_QUEUE_NONE?q.audible:q.intended);
  int renderer=item&&item->identity.track.kind==SB_TRACK_NETWORK&&!strcmp(item->network.server,"uuid:sbupnp-renderer");
  int owns=renderer&&local_owner.value.current==SB_OWNER_MUSIC;
  int pending=sb_playback_network_pending(local_music,NULL),pending_renderer=sb_playback_network_pending(local_music,"uuid:sbupnp-renderer");
  int busy=!upnp_playback_ready()||(local_owner.value.current!=SB_OWNER_NONE&&!owns)||(pending&&!pending_renderer);
  if(!strcmp(operation,"renderstate")){
   const np_status *s=np_current(nowplaying);const char *state=pending_renderer?"TRANSITIONING":!owns?"STOPPED":s->state==NP_PLAYING?"PLAYING":s->state==NP_PAUSED?"PAUSED_PLAYBACK":s->state==NP_BUFFERING?"TRANSITIONING":"STOPPED";
   char uri[4096],out[6144];np_json_escape(uri,sizeof uri,renderer?item->network.uri:"");
   snprintf(out,sizeof out,"{\"state\":\"%s\",\"owned\":%s,\"busy\":%s,\"awake\":%s,\"volume\":%d,\"position_ms\":%llu,\"duration_ms\":%llu,\"uri\":\"%s\"}",state,owns?"true":"false",busy?"true":"false",np_player_power(nowplaying)?"true":"false",np_player_volume(nowplaying),(unsigned long long)(owns?s->position_ms:0),(unsigned long long)(renderer?item->duration_ms:0),uri);
   http_reply(c,"200 OK",out,0);return 1;
  }
  if(busy||!np_player_power(nowplaying)||local_owner.suspended){http_reply(c,"409 Conflict","{\"error\":\"renderer_not_admitted\"}",0);return 1;}
  int accepted=0;
  if(!strcmp(operation,"renderplay")){
   const char *uri=sb_lms_tag(cmd,"uri");
   if(uri&&strlen(uri)<480){
    if(owns&&!strcmp(item->network.uri,uri)&&np_current(nowplaying)->state==NP_PAUSED)accepted=sb_playback_control(local_music,'u',q.context,np_monotonic_ms());
    else if(owns&&!strcmp(item->network.uri,uri)&&np_current(nowplaying)->state==NP_PLAYING)accepted=1;
    else {sb_queue_item candidate={0};strcpy(candidate.title,"Network playback");strcpy(candidate.network.server,"uuid:sbupnp-renderer");strcpy(candidate.network.object,uri);strcpy(candidate.network.uri,uri);candidate.network.generation=1;candidate.available=candidate.eligible=1;candidate.codec='m';
     accepted=sb_playback_load_network(local_music,SB_Q_LOAD,&candidate,1,q.context,np_monotonic_ms())==SB_Q_OK;
    }
   }
  }else {
   const char *command=sb_lms_tag(cmd,"command"),*value=sb_lms_tag(cmd,"value");
   if(command&&!strcmp(command,"stop")){
    sb_playback_cancel(local_music,sb_playback_operation(local_music),q.context);
    accepted=!owns||sb_playback_control(local_music,'q',q.context,np_monotonic_ms());
   }else if(command&&owns&&!strcmp(command,"pause"))accepted=sb_playback_control(local_music,'p',q.context,np_monotonic_ms());
   else if(command&&owns&&!strcmp(command,"play"))accepted=sb_playback_control(local_music,'u',q.context,np_monotonic_ms());
   else if(command&&!strcmp(command,"volume")){uint64_t volume;if(upnp_number(value,100,&volume)){np_set_player_volume(nowplaying,(int)volume,0);publish_playerstatus();accepted=1;}}
  }
  http_reply(c,accepted?"200 OK":"409 Conflict",accepted?"{\"accepted\":true}":"{\"error\":\"renderer_action_refused\"}",0);return 1;
 }
#else
 (void)cmd;
#endif
 http_reply(c,"503 Service Unavailable","{\"error\":\"renderer_backend_unavailable\"}",0);return 1;
}
static void upnp_http(client *c,const char *method,const char *headers,size_t header_length,const char *body,size_t length,int chunked)
{
 if(strcmp(method,"POST")||chunked||length>4096||!upnp_auth(headers,header_length)){http_reply(c,"403 Forbidden","{\"error\":\"unauthorized\"}",0);return;}
 sb_lms_command cmd;sb_command_result parsed=sb_lms_command_parse(body,length,&cmd);sb_upnp_request r={0};
 if(parsed==SB_COMMAND_MALFORMED||parsed==SB_COMMAND_TOO_LARGE||strcmp(cmd.arguments[0],"networkmusic"))goto invalid;
 const char *op=sb_lms_tag(&cmd,"op");if(!op)goto invalid;
 if(upnp_renderer(c,&cmd,op))return;
 const char *names[]={"health","servers","browse","metadata","capabilities","search","update","renderer"};
 for(unsigned i=0;i<8;i++)if(!strcmp(op,names[i]))r.operation=i+1;
 if(!strcmp(op,"play")||!strcmp(op,"add")||!strcmp(op,"next")){r.operation=SB_UPNP_METADATA;r.limit=1;c->upnp_edit=!strcmp(op,"play")?0:!strcmp(op,"add")?1:2;}
 int container=!strcmp(op,"playcontainer")||!strcmp(op,"addcontainer")||!strcmp(op,"nextcontainer");
 if(container){r.operation=SB_UPNP_BROWSE;c->upnp_edit=!strcmp(op,"playcontainer")?0:!strcmp(op,"addcontainer")?1:2;}
 if(!r.operation||!upnp_tag(&cmd,"server",r.server,sizeof r.server)||!upnp_tag(&cmd,"object",r.object,sizeof r.object)||!upnp_tag(&cmd,"field",r.field,sizeof r.field)||!upnp_tag(&cmd,"text",r.text,sizeof r.text)||!upnp_tag(&cmd,"sort",r.sort,sizeof r.sort))goto invalid;
 uint64_t n;if(!r.limit)r.limit=50;const char *value=sb_lms_tag(&cmd,"generation");if(value&&!upnp_number(value,UINT64_MAX,&r.generation))goto invalid;
 value=sb_lms_tag(&cmd,"offset");if(value){if(!upnp_number(value,UINT32_MAX-50,&n))goto invalid;r.offset=(unsigned)n;}
 value=sb_lms_tag(&cmd,"limit");if(value){if(!upnp_number(value,50,&n)||!n)goto invalid;r.limit=(unsigned)n;}
 if(upnp_sequence==UINT64_MAX)goto invalid;
 r.request=++upnp_sequence;r.session=r.request;
 if(container||!strcmp(op,"play")||!strcmp(op,"add")||!strcmp(op,"next")){
  c->upnp_selection=malloc(sizeof r);if(!c->upnp_selection)goto invalid;*c->upnp_selection=r;
  c->upnp_intent=upnp_intent_generation=r.request;c->upnp_item_count=c->upnp_page_count=0;
  if(container){
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
   c->upnp_items=calloc(SB_QUEUE_LIMIT,sizeof(sb_queue_item));if(!c->upnp_items)goto invalid;
#else
   goto invalid;
#endif
  }
 }
 unsigned char wire[SB_UPNP_REQUEST_BYTES];if(!sb_upnp_encode(&r,wire))goto invalid;
 c->upnp_fd=sb_upnp_connect(upnp_socket_path());
 if(c->upnp_fd<0||send(c->upnp_fd,wire,sizeof wire,MSG_NOSIGNAL)!=(ssize_t)sizeof wire){if(c->upnp_fd>=0)close(c->upnp_fd);c->upnp_fd=-1;http_reply(c,"503 Service Unavailable","{\"error\":\"upnp_unavailable\"}",0);return;}
 c->upnp_request=r.request;c->upnp_deadline=np_monotonic_ms()+30000;c->used=0;return;
invalid:free(c->upnp_selection);c->upnp_selection=NULL;free(c->upnp_items);c->upnp_items=NULL;http_reply(c,"400 Bad Request","{\"error\":\"invalid_network_music_request\"}",0);
}
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
static int upnp_project_item(const char *json,size_t length,unsigned index,const sb_upnp_request *r,sb_queue_item *item)
{
 char playable[8],duration[32];uint64_t milliseconds=0;
 memset(item,0,sizeof *item);
 if(!np_json_item_field(json,length,index,"playable",playable,sizeof playable)||strcmp(playable,"true"))return 0;
 if(!np_json_item_field(json,length,index,"uri",item->network.uri,sizeof item->network.uri)||!np_json_item_field(json,length,index,"id",item->network.object,sizeof item->network.object))return -1;
 snprintf(item->network.server,sizeof item->network.server,"%s",r->server);item->network.generation=r->generation;
 np_json_item_field(json,length,index,"title",item->title,sizeof item->title);np_json_item_field(json,length,index,"artist",item->artist,sizeof item->artist);np_json_item_field(json,length,index,"album",item->album,sizeof item->album);
 if(!np_json_item_field(json,length,index,"artwork",item->artwork,sizeof item->artwork))item->artwork[0]=0;
 np_json_item_field(json,length,index,"genre",item->genre,sizeof item->genre);
 char track[32];uint64_t track_number;if(np_json_item_field(json,length,index,"track_number",track,sizeof track)&&upnp_number(track,UINT32_MAX,&track_number))item->track_number=(unsigned)track_number;
 if(np_json_item_field(json,length,index,"duration_ms",duration,sizeof duration))upnp_number(duration,UINT64_MAX,&milliseconds);
 item->duration_ms=milliseconds;item->available=item->eligible=1;item->codec='m';return 1;
}
#endif
static unsigned upnp_selected(client *c,char *json,size_t *length)
{
 unsigned result=503;
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
 if(upnp_playback_ready()&&c->upnp_intent==upnp_intent_generation){sb_queue_item item={0};
  sb_upnp_request *r=c->upnp_selection;
  sb_queue_item *items=&item;unsigned count=1;
  if(c->upnp_items){
   char text[32];uint64_t returned,total,update;
   if(!np_json_root_field(json,*length,"count",text,sizeof text)||!upnp_number(text,50,&returned)||!np_json_root_field(json,*length,"total",text,sizeof text)||!upnp_number(text,SB_QUEUE_LIMIT,&total)||!np_json_root_field(json,*length,"update_id",text,sizeof text)||!upnp_number(text,UINT32_MAX,&update)||
      (c->upnp_page_count&&c->upnp_update_id!=(unsigned)update)){result=409;goto selected;}
   c->upnp_update_id=(unsigned)update;c->upnp_page_count++;
   for(unsigned i=0;i<(unsigned)returned;i++){
    sb_queue_item candidate;int projected=upnp_project_item(json,*length,i,r,&candidate);if(projected<0){result=422;goto selected;}
    if(projected){if(c->upnp_item_count==SB_QUEUE_LIMIT){result=409;goto selected;}((sb_queue_item *)c->upnp_items)[c->upnp_item_count++]=candidate;}
   }
   if(r->offset+returned<total){
    if(!returned||c->upnp_page_count>=6||upnp_sequence==UINT64_MAX){result=409;goto selected;}
    r->offset+=(unsigned)returned;r->request=++upnp_sequence;r->session=r->request;unsigned char wire[SB_UPNP_REQUEST_BYTES];
    if(!sb_upnp_encode(r,wire)){result=400;goto selected;}int fd=sb_upnp_connect(upnp_socket_path());
    if(fd<0||send(fd,wire,sizeof wire,MSG_NOSIGNAL)!=(ssize_t)sizeof wire){if(fd>=0)close(fd);result=503;goto selected;}
    c->upnp_fd=fd;c->upnp_request=r->request;return 0;
   }
   items=c->upnp_items;count=c->upnp_item_count;if(!count){result=422;goto selected;}
  }else if(upnp_project_item(json,*length,0,r,&item)!=1||strcmp(item.network.object,r->object)){result=422;goto selected;}
  {
   sb_queue_status snapshot=sb_queue_snapshot(sb_playback_queue(local_music));
   sb_queue_result accepted=sb_playback_load_network(local_music,(sb_queue_edit)c->upnp_edit,items,count,snapshot.context,np_monotonic_ms());
   result=accepted==SB_Q_OK?200:409;
  }
 }
selected:
#else
 (void)c;
#endif
 *length=(size_t)snprintf(json,SB_UPNP_REPLY_MAX,"{\"accepted\":%s%s}",result==200?"true":"false",result==200?"":",\"error\":\"network_playback_not_admitted\"");return result;
}
static void upnp_pump(client *c)
{
 if(c->upnp_fd<0&&!c->upnp_reply)return;
 char byte;ssize_t peer=recv(c->fd,&byte,1,MSG_PEEK|MSG_DONTWAIT);
 if(peer==0||peer>0){close_client(c);return;}
 if(np_monotonic_ms()>=c->upnp_deadline){close_client(c);return;}
 if(c->upnp_fd>=0){
  char *packet=malloc(SB_UPNP_REPLY_MAX+41);if(!packet){close_client(c);return;}
  ssize_t n=recv(c->upnp_fd,packet,SB_UPNP_REPLY_MAX+40,MSG_DONTWAIT|MSG_TRUNC);
  if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){free(packet);return;}
  unsigned status;size_t length;
  if(n<=0||!sb_upnp_response((unsigned char *)packet,(size_t)n,c->upnp_request,c->upnp_request,&status,&length)){free(packet);close_client(c);return;}
  close(c->upnp_fd);c->upnp_fd=-1;memmove(packet,packet+40,length);packet[length]=0;c->upnp_reply=packet;c->upnp_length=length;c->upnp_sent=0;
  if(c->upnp_selection){if(status==200)status=upnp_selected(c,packet,&length);
   if(!status){free(packet);c->upnp_reply=NULL;return;}
   free(c->upnp_selection);c->upnp_selection=NULL;free(c->upnp_items);c->upnp_items=NULL;c->upnp_length=length;
  }
  char header[256];int bytes=snprintf(header,sizeof header,"HTTP/1.1 %u %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",status,status==200?"OK":"Error",length);if(!send_data(c,header,(size_t)bytes))return;
 }
 if(c->upnp_reply&&c->output.length<SB_OUTPUT_CAPACITY-SB_OUTPUT_BUDGET){size_t n=c->upnp_length-c->upnp_sent;if(n>SB_OUTPUT_BUDGET)n=SB_OUTPUT_BUDGET;if(n&&!send_data(c,c->upnp_reply+c->upnp_sent,n))return;c->upnp_sent+=n;
  if(c->upnp_sent==c->upnp_length){free(c->upnp_reply);c->upnp_reply=NULL;c->close_after_drain=1;}
 }
}
