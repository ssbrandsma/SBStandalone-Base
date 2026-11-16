/* Optional UPnP AV v1 renderer. The only playback backend is Base's
 * authenticated shared Music adapter; SDK callbacks never issue decoder I/O. */
#include <UpnpActionRequest.h>
#include <UpnpSubscriptionRequest.h>
#include "rpc.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "renderer_descriptions.h"
static struct {
 pthread_mutex_t lock;
 int enabled,handle,volume,saved_volume,available,dirty;
 char uuid[64],name[128],uri[480],metadata[4097],actual[32];
 char socket[108];uint64_t position,duration,next_poll;
} renderer={.lock=PTHREAD_MUTEX_INITIALIZER,.handle=-1,.volume=50,.saved_volume=50,.actual="STOPPED"};
static pthread_mutex_t renderer_configuration=PTHREAD_MUTEX_INITIALIZER;
static int escape_json(const char *in,char *out,size_t capacity)
{
 size_t used=0;for(const unsigned char *p=(const unsigned char *)in;*p;p++){
  if(*p=='"'||*p=='\\'){if(used+2>=capacity)return 0;out[used++]='\\';out[used++]=(char)*p;}
  else if(*p<32){if(used+6>=capacity)return 0;int n=snprintf(out+used,capacity-used,"\\u%04x",*p);used+=(size_t)n;}
  else {if(used+1>=capacity)return 0;out[used++]=(char)*p;}
 }out[used]=0;return 1;
}
static int base_call(const char *op,const char *command,const char *value,const char *uri,char *out,size_t capacity)
{
 char tokenpath[128],token[65]={0},escaped[3000],body[4096],headers_text[256];
 if(!escape_json(uri?uri:"",escaped,sizeof escaped))return 0;
 strcpy(tokenpath,renderer.socket);char *slash=strrchr(tokenpath,'/');if(!slash)return 0;strcpy(slash+1,"token");
 FILE *f=fopen(tokenpath,"r");size_t bytes=f?fread(token,1,65,f):0;if(f)fclose(f);if(bytes!=64)return 0;
 int length=snprintf(body,sizeof body,"{\"data\":{\"request\":[\"\",[\"networkmusic\",\"op:%s\",\"command:%s\",\"value:%s\",\"uri:%s\"]]}}",op,command?command:"",value?value:"",escaped);
 if(length<0||(size_t)length>=sizeof body)return 0;
 snprintf(headers_text,sizeof headers_text,"X-Standalone-UPnP: %.64s\r\nConnection: close\r\n",token);
 UpnpString *headers=UpnpString_new();void *connection=NULL;int ok=0;if(!headers)return 0;UpnpString_set_String(headers,headers_text);
 const char *url="http://127.0.0.1:9000/api/network-music";
 if(UpnpOpenHttpConnection(url,&connection,1)!=UPNP_E_SUCCESS||UpnpMakeHttpRequest(UPNP_HTTPMETHOD_POST,url,connection,headers,"application/json",length,1)!=UPNP_E_SUCCESS)goto done;
 size_t sent=(size_t)length;if(UpnpWriteHttpRequest(connection,body,&sent,1)!=UPNP_E_SUCCESS||sent!=(size_t)length||UpnpEndHttpRequest(connection,1)!=UPNP_E_SUCCESS)goto done;
 char *mime;int size,status;if(UpnpGetHttpResponse(connection,NULL,&mime,&size,&status,1)!=UPNP_E_SUCCESS||status!=200||size>(int)capacity-1)goto done;
 size_t used=0;uint64_t deadline=millis()+3000;
 while(used<capacity-1&&millis()<deadline){size_t n=capacity-1-used;if(UpnpReadHttpResponse(connection,out+used,&n,1)!=UPNP_E_SUCCESS)goto done;used+=n;if(!n){out[used]=0;ok=1;break;}}
done:if(connection)UpnpCloseHttpConnection(connection);UpnpString_delete(headers);return ok;
}
static const char *json_value(const char *json,const char *name)
{char key[64];snprintf(key,sizeof key,"\"%s\":",name);const char *p=strstr(json,key);return p?p+strlen(key):"";}
static void renderer_refresh(void)
{
 char out[8192];renderer.available=base_call("renderstate",NULL,NULL,NULL,out,sizeof out);
 strcpy(renderer.actual,"STOPPED");renderer.position=renderer.duration=0;
 if(!renderer.available)return;
 const char *state=json_value(out,"state");
 const char *names[]={"PLAYING","PAUSED_PLAYBACK","TRANSITIONING","STOPPED"};
 for(unsigned i=0;i<4;i++)if(*state=='"'&&!strncmp(state+1,names[i],strlen(names[i]))&&state[1+strlen(names[i])]=='"')strcpy(renderer.actual,names[i]);
 renderer.position=strtoull(json_value(out,"position_ms"),NULL,10);renderer.duration=strtoull(json_value(out,"duration_ms"),NULL,10);renderer.volume=atoi(json_value(out,"volume"));
}
static void time_text(char out[32],uint64_t ms){uint64_t seconds=ms/1000;snprintf(out,32,"%llu:%02u:%02u",(unsigned long long)(seconds/3600),(unsigned)(seconds/60%60),(unsigned)(seconds%60));}
static int xml_text(const char *s,char *out,size_t cap)
{
 size_t used=0;for(;*s;s++){const char *escaped=*s=='&'?"&amp;":*s=='<'?"&lt;":*s=='>'?"&gt;":*s=='"'?"&quot;":NULL;size_t n=escaped?strlen(escaped):1;if(used+n>=cap)return 0;if(escaped)memcpy(out+used,escaped,n);else out[used]=*s;used+=n;}out[used]=0;return 1;
}
static char *renderer_change(int rendering_control)
{
 char *out=malloc(65536);if(!out)return NULL;
 if(rendering_control){snprintf(out,65536,"<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/RCS/\"><InstanceID val=\"0\"><Volume channel=\"Master\" val=\"%d\"/><Mute channel=\"Master\" val=\"%d\"/></InstanceID></Event>",renderer.volume,renderer.volume==0);return out;}
 char uri[3000],metadata[25000],duration[32];if(!xml_text(renderer.uri,uri,sizeof uri)||!xml_text(renderer.metadata,metadata,sizeof metadata)){free(out);return NULL;}time_text(duration,renderer.duration);
 snprintf(out,65536,"<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/AVT/\"><InstanceID val=\"0\"><TransportState val=\"%s\"/><TransportStatus val=\"%s\"/><TransportPlaySpeed val=\"1\"/><CurrentPlayMode val=\"NORMAL\"/><NumberOfTracks val=\"%d\"/><CurrentTrack val=\"%d\"/><CurrentTrackDuration val=\"%s\"/><CurrentMediaDuration val=\"%s\"/><AVTransportURI val=\"%s\"/><CurrentTrackURI val=\"%s\"/><AVTransportURIMetaData val=\"%s\"/><CurrentTrackMetaData val=\"%s\"/></InstanceID></Event>",renderer.actual,renderer.available?"OK":"ERROR_OCCURRED",*renderer.uri?1:0,*renderer.uri?1:0,duration,duration,uri,uri,metadata,metadata);return out;
}
static const char *arg_value(UpnpActionRequest *request,const char *name)
{
 IXML_Node *root=ixmlNode_getFirstChild((IXML_Node *)UpnpActionRequest_get_ActionRequest(request));return sbupnp_text(sbupnp_child(root,name));
}
static void fault(UpnpActionRequest *r,int code,const char *message){UpnpActionRequest_set_ErrCode(r,code);UpnpActionRequest_strcpy_ErrStr(r,message);}
static void output(UpnpActionRequest *r,const char *type,const char *name,const char *value)
{
 IXML_Document *response=UpnpActionRequest_get_ActionResult(r);
 if(UpnpAddToActionResponse(&response,UpnpActionRequest_get_ActionName_cstr(r),type,name,value)!=UPNP_E_SUCCESS){fault(r,501,"Action Failed");return;}
 UpnpActionRequest_set_ActionResult(r,response);
}
static int renderer_uri_allowed(const char *uri,const struct sockaddr_storage *peer)
{
 if(!sbupnp_url(uri,NULL)||!peer||peer->ss_family!=AF_INET)return 0;
 char ip[16],origin[64];if(!inet_ntop(AF_INET,&((const struct sockaddr_in *)peer)->sin_addr,ip,sizeof ip))return 0;snprintf(origin,sizeof origin,"http://%s/",ip);
 if(sbupnp_url(uri,origin))return 1;
 int allowed=0;pthread_mutex_lock(&state.mutex);for(unsigned i=0;i<SBUPNP_SERVERS_MAX;i++)if(*state.registry.servers[i].udn&&sbupnp_url(uri,state.registry.servers[i].location))allowed=1;pthread_mutex_unlock(&state.mutex);return allowed;
}
static int renderer_event(Upnp_EventType type,void *data,void *cookie)
{
 (void)cookie;
 if(type==UPNP_EVENT_SUBSCRIPTION_REQUEST){
  UpnpSubscriptionRequest *r=data;pthread_mutex_lock(&renderer.lock);
  if(renderer.enabled){renderer_refresh();const char *service=UpnpSubscriptionRequest_get_ServiceId_cstr(r);const char *names[]={"LastChange"};char *change=renderer_change(strstr(service,"RenderingControl")!=NULL);const char *values[]={change?change:""};
   if(strstr(service,"ConnectionManager")){const char *cn[]={"SourceProtocolInfo","SinkProtocolInfo","CurrentConnectionIDs"},*cv[]={"","http-get:*:audio/mpeg:*","0"};UpnpAcceptSubscription(renderer.handle,renderer.uuid,service,cn,cv,3,UpnpSubscriptionRequest_get_SID_cstr(r));}
   else UpnpAcceptSubscription(renderer.handle,renderer.uuid,service,names,values,1,UpnpSubscriptionRequest_get_SID_cstr(r));
   free(change);
  }pthread_mutex_unlock(&renderer.lock);return 0;
 }
 if(type!=UPNP_CONTROL_ACTION_REQUEST)return 0;
 UpnpActionRequest *r=data;pthread_mutex_lock(&renderer.lock);
 if(!renderer.enabled){fault(r,701,"Transition not available");goto done;}
 const char *name=UpnpActionRequest_get_ActionName_cstr(r),*id=UpnpActionRequest_get_ServiceID_cstr(r),*service;
 if(strstr(id,"AVTransport"))service="urn:schemas-upnp-org:service:AVTransport:1";
 else if(strstr(id,"RenderingControl"))service="urn:schemas-upnp-org:service:RenderingControl:1";
 else if(strstr(id,"ConnectionManager"))service="urn:schemas-upnp-org:service:ConnectionManager:1";
 else {fault(r,401,"Invalid Action");goto done;}
 if(!strstr(id,"ConnectionManager")&&strcmp(arg_value(r,"InstanceID"),"0")){fault(r,718,"Invalid InstanceID");goto done;}
 if(!strstr(id,"ConnectionManager"))renderer_refresh();
 UpnpActionRequest_set_ErrCode(r,0);
 if(strstr(id,"AVTransport")){
  if(!strcmp(name,"SetAVTransportURI")){
   const char *uri=arg_value(r,"CurrentURI"),*metadata=arg_value(r,"CurrentURIMetaData");
   if(strlen(uri)>=sizeof renderer.uri||strlen(metadata)>=sizeof renderer.metadata||!renderer_uri_allowed(uri,UpnpActionRequest_get_CtrlPtIPAddr(r))){fault(r,714,"Illegal MIME-type or resource origin");goto done;}
   if(*metadata){sbupnp_item item;unsigned count=0;if(!sbupnp_didl(metadata,strlen(metadata),uri,&item,1,&count)||count!=1||!item.playable||strcmp(item.uri,uri)){fault(r,714,"Illegal MIME-type");goto done;}}
   strcpy(renderer.uri,uri);strcpy(renderer.metadata,metadata);renderer.dirty=1;
  }else if(!strcmp(name,"Play")){
   if(strcmp(arg_value(r,"Speed"),"1")){fault(r,717,"Play speed not supported");goto done;}
   char response[8192];if(!*renderer.uri||!base_call("renderplay",NULL,NULL,renderer.uri,response,sizeof response)){fault(r,701,"Playback not admitted");goto done;}
  }else if(!strcmp(name,"Pause")||!strcmp(name,"Stop")){
   char response[8192];if(!base_call("rendercontrol",!strcmp(name,"Pause")?"pause":"stop",NULL,NULL,response,sizeof response)){fault(r,701,"Transition not available");goto done;}
  }else if(!strcmp(name,"GetTransportInfo")){output(r,service,"CurrentTransportState",renderer.actual);output(r,service,"CurrentTransportStatus",renderer.available?"OK":"ERROR_OCCURRED");output(r,service,"CurrentSpeed","1");}
  else if(!strcmp(name,"GetMediaInfo")){char duration[32];time_text(duration,renderer.duration);output(r,service,"NrTracks",*renderer.uri?"1":"0");output(r,service,"MediaDuration",duration);output(r,service,"CurrentURI",renderer.uri);output(r,service,"CurrentURIMetaData",renderer.metadata);output(r,service,"NextURI","");output(r,service,"NextURIMetaData","");output(r,service,"PlayMedium","NETWORK");output(r,service,"RecordMedium","NOT_IMPLEMENTED");output(r,service,"WriteStatus","NOT_IMPLEMENTED");}
  else if(!strcmp(name,"GetPositionInfo")){char duration[32],position[32];time_text(duration,renderer.duration);time_text(position,renderer.position);output(r,service,"Track",*renderer.uri?"1":"0");output(r,service,"TrackDuration",duration);output(r,service,"TrackMetaData",renderer.metadata);output(r,service,"TrackURI",renderer.uri);output(r,service,"RelTime",position);output(r,service,"AbsTime",position);output(r,service,"RelCount","2147483647");output(r,service,"AbsCount","2147483647");}
  else if(!strcmp(name,"GetDeviceCapabilities")){output(r,service,"PlayMedia","NETWORK");output(r,service,"RecMedia","NOT_IMPLEMENTED");output(r,service,"RecQualityModes","NOT_IMPLEMENTED");}
  else if(!strcmp(name,"GetTransportSettings")){output(r,service,"PlayMode","NORMAL");output(r,service,"RecQualityMode","NOT_IMPLEMENTED");}
  else if(!strcmp(name,"GetCurrentTransportActions"))output(r,service,"Actions",!strcmp(renderer.actual,"PLAYING")?"Pause,Stop":!strcmp(renderer.actual,"PAUSED_PLAYBACK")?"Play,Stop":*renderer.uri?"Play":"");
  else if(!strcmp(name,"SetPlayMode")){if(strcmp(arg_value(r,"NewPlayMode"),"NORMAL")){fault(r,712,"Play mode not supported");goto done;}}
  else if(!strcmp(name,"Seek")){fault(r,710,"Seek mode not supported");goto done;}
  else if(!strcmp(name,"Next")||!strcmp(name,"Previous")){fault(r,701,"Transition not available");goto done;}
  else {fault(r,401,"Invalid Action");goto done;}
 }else if(strstr(id,"RenderingControl")){
  if(!strcmp(name,"ListPresets"))output(r,service,"CurrentPresetNameList","FactoryDefaults");
  else if(!strcmp(name,"SelectPreset")){char response[8192];if(strcmp(arg_value(r,"PresetName"),"FactoryDefaults")){fault(r,701,"Preset not available");goto done;}if(!base_call("rendercontrol","volume","50",NULL,response,sizeof response)){fault(r,701,"Preset not admitted");goto done;}}
  else {
   if(strcmp(arg_value(r,"Channel"),"Master")){fault(r,402,"Invalid Args");goto done;}
   if(!strcmp(name,"GetVolume")){char volume[16];snprintf(volume,sizeof volume,"%d",renderer.volume);output(r,service,"CurrentVolume",volume);}
   else if(!strcmp(name,"GetMute"))output(r,service,"CurrentMute",renderer.volume==0?"1":"0");
   else if(!strcmp(name,"SetVolume")||!strcmp(name,"SetMute")){
    const char *value=arg_value(r,!strcmp(name,"SetMute")?"DesiredMute":"DesiredVolume");char *end;unsigned long n=strtoul(value,&end,10);
    if(!*value||*end||*value=='-'||n>(!strcmp(name,"SetMute")?1u:100u)){fault(r,402,"Invalid Args");goto done;}
    if(!strcmp(name,"SetMute")){if(n){if(renderer.volume>0)renderer.saved_volume=renderer.volume;n=0;}else n=(unsigned)renderer.saved_volume;}
    char volume[16],response[8192];snprintf(volume,sizeof volume,"%lu",n);if(!base_call("rendercontrol","volume",volume,NULL,response,sizeof response)){fault(r,701,"Volume not admitted");goto done;}
   }else {fault(r,401,"Invalid Action");goto done;}
  }
 }else {
  if(!strcmp(name,"GetProtocolInfo")){output(r,service,"Source","");output(r,service,"Sink","http-get:*:audio/mpeg:*");}
  else if(!strcmp(name,"GetCurrentConnectionIDs"))output(r,service,"ConnectionIDs","0");
  else if(!strcmp(name,"GetCurrentConnectionInfo")){
   if(strcmp(arg_value(r,"ConnectionID"),"0")){fault(r,706,"Invalid Connection Reference");goto done;}
   output(r,service,"RcsID","0");output(r,service,"AVTransportID","0");output(r,service,"ProtocolInfo","http-get:*:audio/mpeg:*");output(r,service,"PeerConnectionManager","");output(r,service,"PeerConnectionID","-1");output(r,service,"Direction","Input");output(r,service,"Status","OK");
  }else {fault(r,401,"Invalid Action");goto done;}
 }
 if(!UpnpActionRequest_get_ActionResult(r)){IXML_Document *response=UpnpMakeActionResponse(name,service,0,NULL);UpnpActionRequest_set_ActionResult(r,response);}
done:pthread_mutex_unlock(&renderer.lock);return 0;
}
static void renderer_shutdown(void)
{
 pthread_mutex_lock(&renderer.lock);int handle=renderer.handle;renderer.enabled=0;renderer.handle=-1;renderer.uri[0]=renderer.metadata[0]=0;pthread_mutex_unlock(&renderer.lock);
 if(handle>=0){char response[8192];base_call("rendercontrol","stop",NULL,NULL,response,sizeof response);UpnpUnRegisterRootDevice(handle);}
}
static unsigned renderer_configure(const sb_upnp_request *r,const char *socket)
{
 if(strcmp(r->text,"0")&&strcmp(r->text,"1"))return 400;
 renderer_shutdown();if(!strcmp(r->text,"0"))return 200;
 if(strncmp(r->server,"uuid:",5)||strlen(r->server)!=41||!*r->object||strlen(r->object)>64)return 400;
 for(const char *p=r->server+5;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||*p=='-'))return 400;
 char directory[128];if(strlen(socket)>100)return 400;strcpy(directory,socket);char *slash=strrchr(directory,'/');if(!slash)return 400;strcpy(slash+1,"renderer");
 if(mkdir(directory,0700)&&errno!=EEXIST)return 503;
 struct stat st;if(lstat(directory,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))return 503;
 const char *names[]={"AVTransport","RenderingControl","ConnectionManager"},*descriptions[]={scpd_AVTransport,scpd_RenderingControl,scpd_ConnectionManager};
 for(unsigned i=0;i<3;i++){char path[180];snprintf(path,sizeof path,"%s/%s.xml",directory,names[i]);int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);if(fd<0)return 503;size_t n=strlen(descriptions[i]);int ok=write(fd,descriptions[i],n)==(ssize_t)n;close(fd);if(!ok)return 503;}
 char friendly[512];size_t used=0;for(const char *p=r->object;*p;p++){const char *v=*p=='&'?"&amp;":*p=='<'?"&lt;":*p=='>'?"&gt;":NULL;if(v){memcpy(friendly+used,v,strlen(v));used+=strlen(v);}else friendly[used++]=*p;}friendly[used]=0;
 char description[4096];size_t n=(size_t)snprintf(description,sizeof description,"<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><device><deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType><friendlyName>%s</friendlyName><manufacturer>Standalone Base</manufacturer><modelName>Squeezebox Network Playback</modelName><UDN>%s</UDN><serviceList>",friendly,r->server);
 for(unsigned i=0;i<3;i++)n+=(size_t)snprintf(description+n,sizeof description-n,"<service><serviceType>urn:schemas-upnp-org:service:%s:1</serviceType><serviceId>urn:upnp-org:serviceId:%s</serviceId><SCPDURL>/%s.xml</SCPDURL><controlURL>/%s/control</controlURL><eventSubURL>/%s/event</eventSubURL></service>",names[i],names[i],names[i],names[i],names[i]);
 snprintf(description+n,sizeof description-n,"</serviceList></device></root>");
 pthread_mutex_lock(&renderer.lock);memmove(renderer.socket,socket,strlen(socket)+1);strcpy(renderer.uuid,r->server);strcpy(renderer.name,r->object);renderer.enabled=1;renderer.handle=-1;renderer.dirty=1;pthread_mutex_unlock(&renderer.lock);
 int handle=-1;
 if(UpnpSetWebServerRootDir(directory)!=UPNP_E_SUCCESS||UpnpRegisterRootDevice2(UPNPREG_BUF_DESC,description,strlen(description),1,renderer_event,NULL,&handle)!=UPNP_E_SUCCESS){renderer_shutdown();return 503;}
 pthread_mutex_lock(&renderer.lock);renderer.handle=handle;pthread_mutex_unlock(&renderer.lock);
 UpnpSetMaxSubscriptions(handle,8);UpnpSetMaxSubscriptionTimeOut(handle,300);UpnpSendAdvertisement(handle,1800);return 200;
}
static void renderer_tick(void)
{
 pthread_mutex_lock(&renderer.lock);
 if(!renderer.enabled||renderer.handle<0||millis()<renderer.next_poll){pthread_mutex_unlock(&renderer.lock);return;}
 renderer.next_poll=millis()+2000;char before[32];strcpy(before,renderer.actual);int volume=renderer.volume;renderer_refresh();
 if(strcmp(before,renderer.actual)||renderer.dirty){char *change=renderer_change(0);if(change){const char *names[]={"LastChange"},*values[]={change};UpnpNotify(renderer.handle,renderer.uuid,"urn:upnp-org:serviceId:AVTransport",names,values,1);free(change);}renderer.dirty=0;}
 if(volume!=renderer.volume){char *change=renderer_change(1);if(change){const char *names[]={"LastChange"},*values[]={change};UpnpNotify(renderer.handle,renderer.uuid,"urn:upnp-org:serviceId:RenderingControl",names,values,1);free(change);}}
 pthread_mutex_unlock(&renderer.lock);
}
