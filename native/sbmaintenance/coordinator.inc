/* Detached coordinator. Hardware actions are delegated to one qualified,
 * root-owned fixed script; deadlines and PID supervision remain native. */
#include <poll.h>
static volatile sig_atomic_t coordinator_cancel;
static int coordinator_capture_peers(const restart_profile *);
static int coordinator_owner(identity *);
static void coordinator_signal(int signal_number){(void)signal_number;coordinator_cancel=1;}
static uint64_t coordinator_clock(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;}
static unsigned coordinator_timeout(unsigned limit)
{
#ifdef SB_MAINTENANCE_TEST
 const char *value=getenv("SB_TEST_COORDINATOR_TIMEOUT");uint64_t parsed;
 if(value&&number(value,&parsed)&&parsed>=100&&parsed<limit)return (unsigned)parsed;
#endif
 return limit;
}
static const char *coordinator_run(void)
{
#ifdef SB_MAINTENANCE_TEST
 const char *value=getenv("SB_TEST_COORDINATOR_RUN");if(value)return value;
#endif
 return "/var/run/standalonebase";
}
static const char *coordinator_actions(void)
{
#ifdef SB_MAINTENANCE_TEST
 const char *value=getenv("SB_TEST_COORDINATOR_ACTIONS");if(value)return value;
#endif
 return "/usr/share/jive/applets/StandaloneBase/restart-actions.sh";
}
static int coordinator_path(char *out,size_t cap,const char *leaf)
{return snprintf(out,cap,"%s/%s",coordinator_run(),leaf)<(int)cap;}
static int coordinator_trusted(const char *path,int directory)
{struct stat st;return !lstat(path,&st)&&st.st_uid==geteuid()&&!(st.st_mode&(directory?0077:0022))&&
 (directory?S_ISDIR(st.st_mode):S_ISREG(st.st_mode));}
static int coordinator_write(const char *leaf,const char *text)
{
 char path[640],temporary[680];if(!coordinator_path(path,sizeof path,leaf))return 0;
 if(snprintf(temporary,sizeof temporary,"%s.XXXXXX",path)>=(int)sizeof temporary)return 0;
 int fd=mkstemp(temporary);if(fd<0)return 0;
 size_t length=strlen(text);int ok=write(fd,text,length)==(ssize_t)length;
 if(close(fd))ok=0;
 if(!ok||rename(temporary,path)){unlink(temporary);return 0;}return 1;
}
static void coordinator_status(const char *state,const identity *owner,const char *reason)
{
 char json[512];snprintf(json,sizeof json,
  "{\"actor\":\"maintenance_coordinator\",\"state\":\"%s\",\"pid\":%ld,\"start\":\"%llu\",\"observed_ms\":\"%llu\",\"reason\":\"%s\"}\n",
  state,(long)owner->pid,(unsigned long long)owner->start,(unsigned long long)coordinator_clock(),reason?reason:"");
 (void)coordinator_write("restart.status",json);
}
static int coordinator_pid(const identity *owner)
{
 char value[64];uint64_t pid;
 if(!read_file(pidfile,value,sizeof value))return 0;
 value[strcspn(value,"\r\n")]=0;
 return number(value,&pid)&&pid==(uint64_t)owner->pid&&same(owner->pid,owner->start);
}
static int coordinator_feeder(const restart_profile *p)
{
 if(!same(p->feeder.pid,p->feeder.start))return 0;
 char path[128];struct stat actual,node;
 snprintf(path,sizeof path,"/proc/%ld/fd/%u",(long)p->feeder.pid,p->fd);
 if(stat(path,&actual)||lstat(watchdog_node(),&node))return 0;
#ifdef SB_MAINTENANCE_TEST
 return actual.st_dev==node.st_dev&&actual.st_ino==node.st_ino;
#else
 return S_ISCHR(actual.st_mode)&&actual.st_rdev==node.st_rdev;
#endif
}
static void coordinator_reap(pid_t child)
{
 int group=getpgid(child)==child;kill(group?-child:child,SIGTERM);
 uint64_t until=coordinator_clock()+200;
 while(coordinator_clock()<until)(void)poll(NULL,0,10);
 kill(group?-child:child,SIGKILL);(void)waitpid(child,NULL,WNOHANG);
}
static int coordinator_action(const char *action,const identity *watched,const identity *owner,
 const restart_profile *profile,unsigned timeout,int owns)
{
 char pid[32],start[32];snprintf(pid,sizeof pid,"%ld",(long)watched->pid);
 snprintf(start,sizeof start,"%llu",(unsigned long long)watched->start);
 pid_t child=fork();if(child<0)return 0;
 if(!child){
  if(setsid()<0)_exit(127);
  execl("/bin/sh","sh",coordinator_actions(),action,pid,start,(char *)NULL);_exit(127);
 }
 uint64_t until=coordinator_clock()+coordinator_timeout(timeout);
 while(coordinator_clock()<until&&!coordinator_cancel){
  int status;pid_t result=waitpid(child,&status,WNOHANG);
  if(result==child)return WIFEXITED(status)&&WEXITSTATUS(status)==0;
  if(result<0&&errno!=EINTR)break;
  if(!coordinator_feeder(profile)||(owns&&!coordinator_pid(owner)))break;
  (void)poll(NULL,0,50);
 }
 coordinator_reap(child);return 0;
}
static int coordinator_challenge(char nonce[65])
{
 unsigned char bytes[32];int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
 ssize_t n=read(fd,bytes,sizeof bytes);close(fd);if(n!=sizeof bytes)return 0;
 static const char hex[]="0123456789abcdef";
 for(unsigned i=0;i<sizeof bytes;i++){nonce[i*2]=hex[bytes[i]>>4];nonce[i*2+1]=hex[bytes[i]&15];}nonce[64]=0;
 char line[66];snprintf(line,sizeof line,"%s\n",nonce);return coordinator_write("restart.challenge",line);
}
static int coordinator_new_identity(identity *out)
{
 char path[640],bytes[128];long pid;unsigned long long start;int used=0;identity candidate;
 if(!coordinator_path(path,sizeof path,"restart.new")||!coordinator_trusted(path,0)||!read_file(path,bytes,sizeof bytes)||
  sscanf(bytes,"%ld %llu %n",&pid,&start,&used)!=2||bytes[used]||pid<2||pid>INT_MAX||
  !identify((pid_t)pid,&candidate)||candidate.start!=start||strcmp(candidate.exe,jive))return 0;
 *out=candidate;return 1;
}
/* Health checks must independently inspect the replacement UI witness, IPC,
 * operational Base and process identities. No hook exit status alone suffices. */
static int coordinator_ui_sample(const identity *replacement,const char *nonce,uint64_t *ticks,uint64_t *sequence)
{
 char path[640],bytes[256],challenge[65];long pid;unsigned long long start,tick,seq;int used=0;
 if(!coordinator_path(path,sizeof path,"jive.health")||!coordinator_trusted(path,0)||!read_file(path,bytes,sizeof bytes)||
  sscanf(bytes,"SBUI1 %64s %ld %llu %llu %llu %n",challenge,&pid,&start,&tick,&seq,&used)!=5||bytes[used]||
  strcmp(challenge,nonce)||pid!=replacement->pid||start!=replacement->start||!seq||
  !same(replacement->pid,replacement->start))return 0;
 *ticks=tick;*sequence=seq;return 1;
}
static int coordinator_ui_ipc(const identity *replacement,int expected,unsigned *minimum)
{
 if(minimum)*minimum=UINT_MAX;
 int id=semget((key_t)1234,0,0);if(id<0||(expected>=0&&id!=expected))return 0;
 struct semid_ds meta;union semarg arg;arg.buf=&meta;
 if(semctl(id,0,IPC_STAT,arg)<0||meta.sem_nsems!=20)return 0;
 unsigned short values[20];arg.array=values;if(semctl(id,0,GETALL,arg)<0)return 0;
 unsigned ui=0;
 for(unsigned n=0;n<20;n+=2){
  if(values[n]>1)return 0;
  if(values[n]==0){
   if(minimum&&values[n+1]<*minimum)*minimum=values[n+1];
   if(!values[n+1])return 0;
   int actor=semctl(id,(int)n+1,GETPID);if(actor<0)return 0;
   if(actor==replacement->pid)ui++;
  }
 }
 return ui==1&&same(replacement->pid,replacement->start);
}
static int coordinator_verify_ui(uint64_t pid,uint64_t start,const char *nonce)
{
 if(pid<2||pid>INT_MAX||strlen(nonce)!=64)return 2;
 for(unsigned n=0;n<64;n++)if(!strchr("0123456789abcdef",nonce[n]))return 2;
 identity target;uint64_t ticks,sequence;
 if(!identify((pid_t)pid,&target)||strcmp(target.exe,jive)||target.start!=start||
  !coordinator_ui_sample(&target,nonce,&ticks,&sequence)||!coordinator_ui_ipc(&target,-1,NULL))return 2;
 printf("{\"pid\":%ld,\"start\":\"%llu\",\"ticks\":\"%llu\",\"sequence\":\"%llu\"}\n",
  (long)target.pid,(unsigned long long)target.start,(unsigned long long)ticks,(unsigned long long)sequence);return 0;
}
static int coordinator_launch_jive(identity *result)
{
 identity owner,child;if(!coordinator_trusted(coordinator_run(),1)||!coordinator_owner(&owner)||spawn_identified(0,&child))return 2;
 *result=child;
 char line[128];snprintf(line,sizeof line,"%ld %llu\n",(long)child.pid,(unsigned long long)child.start);
 if(!coordinator_write("restart.new",line)){if(same(child.pid,child.start))kill(child.pid,SIGTERM);return 2;}
 return 0;
}
static void coordinator_child_exit(const identity *child)
{
 int status;pid_t result=waitpid(child->pid,&status,WNOHANG);if(result!=child->pid)return;
 char text[256];snprintf(text,sizeof text,"pid=%ld start=%llu exited=%d status=%d signal=%d\n",
  (long)child->pid,(unsigned long long)child->start,WIFEXITED(status),
  WIFEXITED(status)?WEXITSTATUS(status):-1,WIFSIGNALED(status)?WTERMSIG(status):0);
 (void)coordinator_write("jive.exit",text);
}
static int coordinator_verify_stack(uint64_t pid,uint64_t start)
{
 identity ui,services[3];unsigned counts[3]={0};
 if(pid<2||pid>INT_MAX||!identify((pid_t)pid,&ui)||ui.start!=start||strcmp(ui.exe,jive))return 2;
 DIR *dir=opendir("/proc");if(!dir)return 2;
 struct dirent *entry;unsigned visited=0;int ok=1;
 while((entry=readdir(dir))){
  uint64_t number_pid;if(!number(entry->d_name,&number_pid)||number_pid<2||number_pid>INT_MAX)continue;
  if(++visited>4096){ok=0;break;}
  identity process;if(!identify((pid_t)number_pid,&process))continue;
  int role=base_role(&process);if(!role)continue;
  if(++counts[role-1]!=1||process.start<ui.start){ok=0;break;}
  services[role-1]=process;
 }
 closedir(dir);
 if(!ok||counts[0]!=1||counts[1]!=1||counts[2]!=1||!same(ui.pid,ui.start))return 2;
 for(unsigned n=0;n<3;n++)if(!same(services[n].pid,services[n].start))return 2;
 printf("{\"base_pid\":%ld,\"base_start\":\"%llu\",\"proxy_pid\":%ld,\"proxy_start\":\"%llu\",\"web_pid\":%ld,\"web_start\":\"%llu\"}\n",
  (long)services[0].pid,(unsigned long long)services[0].start,
  (long)services[1].pid,(unsigned long long)services[1].start,
  (long)services[2].pid,(unsigned long long)services[2].start);return 0;
}
static int coordinator_health(const identity *replacement,const identity *owner,const restart_profile *p,
 const char *nonce,int recovery)
{
 uint64_t until=coordinator_clock()+coordinator_timeout(15000),previous_tick=0,previous_sequence=0;
 uint64_t low_since=0,advanced=0;unsigned samples=0;
 while(coordinator_clock()<until&&!coordinator_cancel){
  if(!coordinator_pid(owner)||!coordinator_feeder(p))return 0;
  if(!same(replacement->pid,replacement->start)){coordinator_child_exit(replacement);return 0;}
  uint64_t tick,sequence,now=coordinator_clock();unsigned minimum;
  int ipc=coordinator_ui_ipc(replacement,p->semid,&minimum);
  if(minimum<=2){
   if(!low_since)low_since=now;
   if(minimum<=1||now-low_since>=500)return 0;
  }else low_since=0;
  if(samples&&now-advanced>2500)return 0;
  if(coordinator_ui_sample(replacement,nonce,&tick,&sequence)&&
     ipc&&
     (!samples||(tick>previous_tick&&sequence>previous_sequence))&&
     coordinator_action(recovery?"health-recovery":"health",replacement,owner,p,1000,1)){
   previous_tick=tick;previous_sequence=sequence;advanced=coordinator_clock();if(++samples>=2)return 1;
  }
  (void)poll(NULL,0,200);
 }
 return 0;
}
static void coordinator_cleanup(void)
{
 const char *leaves[]={"restart.challenge","jive.health","restart.new","restart.old","restart.peers"};char path[640];
 for(unsigned n=0;n<sizeof leaves/sizeof leaves[0];n++)
  if(coordinator_path(path,sizeof path,leaves[n]))unlink(path);
}
static int coordinator_old_healthy(const identity *old,const identity *owner,const restart_profile *p,const char *nonce)
{
 uint64_t tick,sequence;if(!coordinator_ui_sample(old,nonce,&tick,&sequence))return 0;
 uint64_t until=coordinator_clock()+coordinator_timeout(1500);
 while(coordinator_clock()<until){
  uint64_t next_tick,next_sequence;unsigned minimum;
  if(!coordinator_pid(owner)||!coordinator_feeder(p)||
   !coordinator_ui_ipc(old,p->semid,&minimum)||minimum<3)return 0;
  if(coordinator_ui_sample(old,nonce,&next_tick,&next_sequence)&&next_tick>tick&&next_sequence>sequence&&
   coordinator_action("health-recovery",old,owner,p,1000,1))return 1;
  (void)poll(NULL,0,50);
 }
 return 0;
}
static int coordinate_restart(void)
{
 identity owner,replacement={0};if(!identify(getpid(),&owner)||!profile()||!coordinator_trusted(coordinator_run(),1)||
  !coordinator_trusted(coordinator_actions(),0))return 2;
 char path[640];if(!coordinator_path(path,sizeof path,"restart.lock"))return 2;
 int lock=open(path,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);struct stat st;
 if(lock<0)return 2;
 if(fstat(lock,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077)||
  flock(lock,LOCK_EX|LOCK_NB)){close(lock);return 2;}
 signal(SIGHUP,SIG_IGN);signal(SIGINT,coordinator_signal);signal(SIGTERM,coordinator_signal);
 restart_profile p;coordinator_status("PREFLIGHT",&owner,NULL);
 if(!restart_preflight(&p)||!coordinator_capture_peers(&p)||
  !coordinator_action("preflight",&p.watched,&owner,&p,5000,0))goto refused;
 char nonce[65];if(!coordinator_challenge(nonce))goto refused;
 char original[128];snprintf(original,sizeof original,"%ld %llu\n",(long)p.watched.pid,(unsigned long long)p.watched.start);
 if(!coordinator_write("restart.old",original))goto refused;
 int unexpected_exit=dead(p.watched.pid,p.watched.start);
 if(!coordinator_feeder(&p)||swap_pid(p.watched.pid,p.watched.start,owner.pid,owner.start,unexpected_exit))goto refused;
 if(unexpected_exit)goto recover;
 coordinator_status("STOPPING",&owner,NULL);
 if(!coordinator_action("stop",&p.watched,&owner,&p,10000,1))goto recover;
 if(!dead(p.watched.pid,p.watched.start))goto recover;
 coordinator_status("STARTING",&owner,NULL);
 if(!coordinator_action("start",&p.watched,&owner,&p,5000,1))goto recover;
 if(coordinator_launch_jive(&replacement))goto recover;
#ifdef SB_MAINTENANCE_TEST
 if(getenv("SB_TEST_COORDINATOR_BAD_RECORD")){
  if(coordinator_path(path,sizeof path,"restart.new"))unlink(path);
 }
#endif
 identity recorded;
 if(!coordinator_new_identity(&recorded)||recorded.pid!=replacement.pid||recorded.start!=replacement.start||
  replacement.pid==p.watched.pid||replacement.start<owner.start)goto recover;
 coordinator_status("VERIFYING",&owner,NULL);
 if(!coordinator_health(&replacement,&owner,&p,nonce,0))goto recover;
 if(swap_pid(owner.pid,owner.start,replacement.pid,replacement.start,0))goto recover;
 coordinator_status("COMPLETED",&owner,NULL);
 coordinator_cleanup();
 close(lock);return 0;
recover:
 coordinator_status("RECOVERING",&owner,"restart qualification failed");
 coordinator_cancel=0;
 if(coordinator_old_healthy(&p.watched,&owner,&p,nonce)&&
  !swap_pid(owner.pid,owner.start,p.watched.pid,p.watched.start,0)){
  coordinator_status("RECOVERED",&owner,"original Jive remains live; qualification stopped");
  coordinator_cleanup();close(lock);return 2;
 }
 /* One bounded known-good recovery attempt. This never qualifies a reboot. */
 if(replacement.pid){
  char record[128];snprintf(record,sizeof record,"%ld %llu\n",(long)replacement.pid,(unsigned long long)replacement.start);
  if(!coordinator_write("restart.new",record))goto held;
 }
#ifdef SB_MAINTENANCE_TEST
 (void)setenv("SB_TEST_UI_MODE","normal",1);
#endif
 if(coordinator_action("recover",&p.watched,&owner,&p,12000,1)&&
  !coordinator_launch_jive(&replacement)&&coordinator_health(&replacement,&owner,&p,nonce,1)&&
  !swap_pid(owner.pid,owner.start,replacement.pid,replacement.start,0)){
  coordinator_status("RECOVERED",&owner,"qualification failed; known-good services restored");coordinator_cleanup();close(lock);return 2;
 }
held:
 coordinator_status("FAILED_HELD",&owner,"bounded recovery failed; explicit owner recovery required");
 /* A supervised maintenance actor, explicitly FAILED, not a healthy Jive.
  * Finite poll intervals allow owner PID recovery; no sleeps/fake UI refresh.
  * Never expire the monitored PID into an automatic watchdog reset. */
 for(;;){
  /* A missing/foreign PID marker is not successful recovery and must not
   * make the maintenance actor disappear. An owner may explicitly hand back
   * to an independently verified live UI using the existing CAS primitive. */
  coordinator_cancel=0;
  char bytes[64];uint64_t watched;
  if(read_file(pidfile,bytes,sizeof bytes)){
   bytes[strcspn(bytes,"\r\n")]=0;identity candidate;
   uint64_t tick,sequence,next_tick,next_sequence;
   if(number(bytes,&watched)&&watched>=2&&watched<=INT_MAX&&
    identify((pid_t)watched,&candidate)&&!strcmp(candidate.exe,jive)&&
    coordinator_ui_sample(&candidate,nonce,&tick,&sequence)&&coordinator_ui_ipc(&candidate,p.semid,NULL)&&
    coordinator_action("health-recovery",&candidate,&owner,&p,1000,0)){
    (void)poll(NULL,0,250);
    if(coordinator_ui_sample(&candidate,nonce,&next_tick,&next_sequence)&&next_tick>tick&&next_sequence>sequence&&
     coordinator_ui_ipc(&candidate,p.semid,NULL)&&coordinator_feeder(&p)){
     coordinator_status("OWNER_RECOVERED",&owner,"explicit owner handoff verified");coordinator_cleanup();break;
    }
   }
  }
  (void)poll(NULL,0,500);
 }
 close(lock);return 2;
refused:
 coordinator_status("REFUSED",&owner,"preflight failed; no Jive stop or handoff");close(lock);return 2;
}
