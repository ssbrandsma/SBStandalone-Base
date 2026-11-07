/* Checked stock SIGTERM stop, preserving the coordinator's monitored PID. */
typedef struct {identity rows[12];unsigned count;} coordinator_peers;
static int coordinator_owner(identity *owner)
{
 char bytes[64];uint64_t pid;if(!read_file(pidfile,bytes,sizeof bytes))return 0;
 bytes[strcspn(bytes,"\r\n")]=0;
 if(!number(bytes,&pid)||pid<2||pid>INT_MAX||!identify((pid_t)pid,owner)||!holder(owner))return 0;
 size_t length=strlen(owner->args);return !strcmp(owner->args+length+1,"coordinate");
}
static int peer_add(coordinator_peers *peers,const identity *id)
{
 for(unsigned n=0;n<peers->count;n++)if(peers->rows[n].pid==id->pid)
  return peers->rows[n].start==id->start;
 if(peers->count==12)return 0;
 peers->rows[peers->count++]=*id;return 1;
}
static int peer_collect(coordinator_peers *peers,const identity *target)
{
 DIR *dir=opendir("/proc");if(!dir)return 0;
 struct dirent *entry;unsigned count=0,roles[5]={0};int ok=1;
 while((entry=readdir(dir))){
  uint64_t pid;if(!number(entry->d_name,&pid)||pid<2||pid>INT_MAX)continue;
  if(++count>4096){ok=0;break;}
  identity id;if(!identify((pid_t)pid,&id))continue;
  int role=base_role(&id);
  if(role){
   if(++roles[role]>1||id.start<target->start||!peer_add(peers,&id)){ok=0;break;}
  }else if(!strcmp(id.exe,alsa_executable())&&id.parent==target->pid&&id.start>=target->start){
   if(!peer_add(peers,&id)){ok=0;break;}
  }
 }
 closedir(dir);return ok;
}
static int peers_save(const coordinator_peers *peers)
{
 char text[1024];size_t used=0;
 for(unsigned n=0;n<peers->count;n++){
  const identity *id=&peers->rows[n];
  int added=snprintf(text+used,sizeof text-used,"%ld %llu\n",(long)id->pid,(unsigned long long)id->start);
  if(added<0||(size_t)added>=sizeof text-used)return 0;
  used+=(size_t)added;
 }
 text[used]=0;return coordinator_write("restart.peers",text);
}
static int peers_load(coordinator_peers *peers)
{
 char path[640],bytes[1024];struct stat st;
 if(!coordinator_path(path,sizeof path,"restart.peers"))return 0;
 if(lstat(path,&st))return errno==ENOENT;
 if(!coordinator_trusted(path,0))return 0;
 int length=read_file(path,bytes,sizeof bytes);if(!length)return st.st_size==0;
 char *save=NULL;
 for(char *line=strtok_r(bytes,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
  long pid;unsigned long long start;int used=0;
  if(sscanf(line,"%ld %llu %n",&pid,&start,&used)!=2||line[used]||pid<2||pid>INT_MAX||!start)return 0;
  identity id;if(!identify((pid_t)pid,&id)){if(dead((pid_t)pid,start))continue;return 0;}
  if(id.start!=start)return 0;
  if(!base_role(&id)&&strcmp(id.exe,alsa_executable()))return 0;
  if(!peer_add(peers,&id))return 0;
 }
 return 1;
}
static int coordinator_capture_peers(const restart_profile *p)
{
 coordinator_peers peers={0};if(!peers_load(&peers))return 0;
 for(unsigned n=0;n<p->child_count;n++)if(!peer_add(&peers,&p->children[n]))return 0;
 return peer_collect(&peers,&p->watched)&&peers_save(&peers);
}
static int coordinator_record(const char *leaf,identity *target)
{
 char path[640],bytes[128];long pid;unsigned long long start;int used=0;
 if(!coordinator_path(path,sizeof path,leaf)||!coordinator_trusted(path,0)||!read_file(path,bytes,sizeof bytes)||
  sscanf(bytes,"%ld %llu %n",&pid,&start,&used)!=2||bytes[used]||pid<2||pid>INT_MAX||!start)return 0;
 if(identify((pid_t)pid,target))return target->start==start&&!strcmp(target->exe,jive);
 memset(target,0,sizeof *target);target->pid=(pid_t)pid;target->start=start;
 return dead(target->pid,target->start);
}
static int coordinator_quiesce(const char *record,uint64_t requested_pid,uint64_t requested_start,int recovery)
{
 identity owner,target;coordinator_peers peers={0};
 if(!coordinator_trusted(coordinator_run(),1)||!coordinator_owner(&owner)||
  !coordinator_record(record,&target)||(requested_pid&&((uint64_t)target.pid!=requested_pid||target.start!=requested_start))||
  !peers_load(&peers)||!peer_collect(&peers,&target)||!peers_save(&peers))return 2;
 if(recovery){
  char path[640],status[512];
  if(!coordinator_path(path,sizeof path,"restart.status")||!read_file(path,status,sizeof status)||
   !strstr(status,"\"state\":\"RECOVERING\""))return 2;
 }
 if(!dead(target.pid,target.start)){
  if(!same(target.pid,target.start)||kill(target.pid,SIGTERM))return 2;
 }
 uint64_t until=coordinator_clock()+coordinator_timeout(recovery?500:4000);
 while(!dead(target.pid,target.start)&&coordinator_clock()<until){
  if(!coordinator_pid(&owner))return 2;
  (void)poll(NULL,0,50);
 }
 if(!dead(target.pid,target.start)&&recovery){
  if(!same(target.pid,target.start)||kill(target.pid,SIGKILL))return 2;
  until=coordinator_clock()+coordinator_timeout(1000);
  while(!dead(target.pid,target.start)&&coordinator_clock()<until)(void)poll(NULL,0,10);
 }
 if(!dead(target.pid,target.start)||!peer_collect(&peers,&target)||!peers_save(&peers))return 2;
 for(unsigned n=0;n<peers.count;n++){
  identity *old=&peers.rows[n],now;
  if(dead(old->pid,old->start))continue;
  if(!identify(old->pid,&now)||now.start!=old->start||
   (!base_role(&now)&&strcmp(now.exe,alsa_executable()))||kill(now.pid,SIGTERM))return 2;
 }
 until=coordinator_clock()+coordinator_timeout(recovery?500:4000);
 int forced=0;
 for(;;){
  int pending=0;for(unsigned n=0;n<peers.count;n++)if(!dead(peers.rows[n].pid,peers.rows[n].start))pending=1;
  if(!pending)break;
  if(!coordinator_pid(&owner))return 2;
  if(coordinator_clock()>=until){
   if(!recovery||forced)return 2;
   for(unsigned n=0;n<peers.count;n++){
    identity *peer=&peers.rows[n];
    if(dead(peer->pid,peer->start))continue;
    if(!same(peer->pid,peer->start)||kill(peer->pid,SIGKILL))return 2;
   }
   forced=1;until=coordinator_clock()+coordinator_timeout(1000);
  }
  (void)poll(NULL,0,50);
 }
 coordinator_peers remaining={0};
 if(!peer_collect(&remaining,&target)||remaining.count)return 2;
 return 0;
}
