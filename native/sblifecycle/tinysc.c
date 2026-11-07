#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tinysc.h"
#include "../common/process_identity.h"
#include "../common/process_spawn.h"
#include <pthread.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#define TS_TABLE_MAX 4096
typedef struct {pid_t pid,parent;uint64_t start;unsigned role,owned;char state,name[64];} row;
struct sb_tinysc {
 pthread_t thread;pthread_mutex_t lock;pthread_cond_t changed;
 sb_ts_options options;sb_ts_status status;int stop,fd[2],lease;
 char run[512],policy[512],original[512],control[512],boot[64];
 pid_t pid;uint64_t start;
 unsigned close_limit;
};
static const char *tiny_root(void)
{
#ifdef SB_TINYSC_TEST
 const char *value=getenv("SB_TS_TEST_ROOT");if(value)return value;
#endif
 return "/usr/squeezecenter";
}
static const char *stock_path(void)
{
#ifdef SB_TINYSC_TEST
 const char *value=getenv("SB_TS_TEST_SERVICE");if(value)return value;
#endif
 return "/etc/init.d/squeezecenter.stock";
}
uint64_t sb_ts_clock(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;}
const char *sb_ts_state_name(sb_ts_state state)
{static const char *names[]={"UNKNOWN","DETECTING","STOP_REQUESTED","STOPPING","STOPPED_VERIFIED","FAILED"};return (unsigned)state<=SB_TS_FAILED?names[state]:"FAILED";}
static unsigned role_name(const char *name)
{return !strcmp(name,"slimserver.pl")?1:!strcmp(name,"scanner.pl")?2:!strcmp(name,"gdresized.pl")?3:0;}
static int inside(const char *path)
{size_t n=strlen(tiny_root());return !strncmp(path,tiny_root(),n)&&path[n]=='/'&&!strstr(path,"/../")&&!strstr(path,"/./");}
static unsigned classify(const identity *id,int *uncertain)
{
 char path[128],exe[512],args[4096];snprintf(path,sizeof path,"/proc/%ld/exe",(long)id->pid);
 ssize_t n=readlink(path,exe,sizeof exe-1);
 if(n<0||n>=(ssize_t)sizeof exe-1){if(role_name(id->name)){*uncertain=1;return 5;}return 0;}exe[n]=0;
 int own=inside(exe);snprintf(path,sizeof path,"/proc/%ld/cmdline",(long)id->pid);
 int length=read_file(path,args,sizeof args);
 if(!length){if(own||role_name(id->name)){*uncertain=1;return 5;}return 0;}
 unsigned index=0;const char *tokens[4]={0};const char *base=strrchr(exe,'/');base=base?base+1:exe;
 int perl=!strcmp(base,"perl");
 if(!strncmp(base,"perl5.",6)&&base[6]){
  perl=1;for(const char *p=base+6;*p;p++)if((*p<'0'||*p>'9')&&*p!='.')perl=0;
 }
 for(size_t at=0;at<(size_t)length&&index<32;index++){
  const char *token=args+at;size_t bytes=strnlen(token,(size_t)length-at);
  if(index<4)tokens[index]=token;
  if((index==0||(perl&&index==1))&&inside(token)){
   const char *leaf=strrchr(token,'/');unsigned role=leaf?role_name(leaf+1):0;
   if(role)return role;
   own=1;
  }
  at+=bytes+1;
 }
 int nice=!strcmp(exe,"/usr/bin/nice")||!strcmp(exe,"/bin/nice")||
  !strcmp(exe,"/bin/busybox")||!strcmp(exe,"/usr/bin/busybox");
#ifdef SB_TINYSC_TEST
 const char *test_nice=getenv("SB_TS_TEST_NICE");if(test_nice&&!strcmp(exe,test_nice))nice=1;
#endif
 const char *first=tokens[0]?strrchr(tokens[0],'/'):NULL;first=first?first+1:tokens[0];
 if(nice&&first&&!strcmp(first,"nice")&&tokens[1]&&tokens[2]){
  const char *script=NULL;uint64_t priority;
  if(!strcmp(tokens[1],"-n")&&tokens[3]&&number(tokens[2],&priority)&&priority<=19)script=tokens[3];
  else if(!strncmp(tokens[1],"-n",2)&&number(tokens[1]+2,&priority)&&priority<=19)script=tokens[2];
  if(script&&inside(script)){const char *leaf=strrchr(script,'/');unsigned role=leaf?role_name(leaf+1):0;if(role)return role;}
 }
 /* BusyBox ash may run nice before exec without changing its own argv.
  * Retain delayed stock restart shells until they reenter the public guard. */
 int shell=!strcmp(exe,"/bin/busybox")||!strcmp(exe,"/usr/bin/busybox")||!strcmp(exe,"/bin/sh")||!strcmp(exe,"/bin/ash");
#ifdef SB_TINYSC_TEST
 const char *test_shell=getenv("SB_TS_TEST_SHELL");if(test_shell&&!strcmp(exe,test_shell))shell=1;
#endif
 if(shell&&first&&(!strcmp(first,"sh")||!strcmp(first,"ash"))&&tokens[1]&&tokens[2]&&!strcmp(tokens[1],stock_path())){
  if(!strcmp(tokens[2],"start"))return 1;
  if(!strcmp(tokens[2],"startresizer"))return 3;
  if(!strcmp(tokens[2],"restart")||!strcmp(tokens[2],"rescan"))return 4;
 }
 if(!own&&perl&&role_name(id->name)){*uncertain=1;return 5;}
 return own?4:0;
}
static int row_compare(const void *a,const void *b)
{const row *x=a,*y=b;return (x->pid>y->pid)-(x->pid<y->pid);}
static row *find_row(row *rows,unsigned count,pid_t pid)
{row key={.pid=pid};return bsearch(&key,rows,count,sizeof *rows,row_compare);}
static int detect(sb_ts_scan *out,const sb_ts_scan *previous)
{
 memset(out,0,sizeof *out);row *rows=calloc(TS_TABLE_MAX,sizeof *rows);if(!rows)return 0;
 DIR *dir=opendir("/proc");if(!dir){free(rows);return 0;}
 struct dirent *entry;unsigned count=0;int ok=1;
 while((entry=readdir(dir))){
  uint64_t pid;if(!number(entry->d_name,&pid)||pid<2||pid>INT_MAX)continue;
  identity id;if(!process_stat((pid_t)pid,&id)){
   char proc[80];struct stat st;snprintf(proc,sizeof proc,"/proc/%ld",(long)pid);
   if(!stat(proc,&st))out->uncertain++;
   continue;
  }
  if(id.state=='Z'||id.state=='X')continue;
  if(count==TS_TABLE_MAX){ok=0;break;}
  row *r=&rows[count++];r->pid=id.pid;r->parent=id.parent;r->start=id.start;
  r->state=id.state;
  snprintf(r->name,sizeof r->name,"%s",id.name);
  int uncertain=0;r->role=classify(&id,&uncertain);r->owned=!!r->role;
  if(uncertain)out->uncertain++;
  if(previous)for(unsigned n=0;n<previous->count;n++)
   if(previous->rows[n].pid==r->pid&&previous->rows[n].start==r->start){r->owned=1;if(!r->role)r->role=4;}
 }
 closedir(dir);qsort(rows,count,sizeof *rows,row_compare);
 for(unsigned pass=0;pass<16;pass++){
  unsigned changed=0;
  for(unsigned n=0;n<count;n++)if(!rows[n].owned){
   row *parent=find_row(rows,count,rows[n].parent);
   if(parent&&parent->owned){rows[n].owned=1;rows[n].role=4;changed++;}
  }
  if(!changed)break;
 }
 for(unsigned n=0;n<count;n++){
  row *r=&rows[n];
  if(!r->owned){row *parent=find_row(rows,count,r->parent);if(parent&&parent->owned)ok=0;continue;}
  if(out->count==SB_TS_MAX_PROCESSES){ok=0;break;}
  sb_ts_process *p=&out->rows[out->count++];p->pid=r->pid;p->parent=r->parent;p->start=r->start;p->role=r->role;
  p->state=r->state;
  snprintf(p->name,sizeof p->name,"%s",r->name);
 }
 free(rows);return ok;
}
int sb_ts_detect(sb_ts_scan *scan){return scan&&detect(scan,NULL);}
static int trusted_directory(const char *path)
{struct stat st;return !lstat(path,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0077);}
static int join(char *out,size_t size,const char *dir,const char *leaf)
{return snprintf(out,size,"%s/%s",dir,leaf)<(int)size;}
static int policy_enabled(const sb_ts_options *o)
{struct stat st;if(lstat(o->policy_path,&st))return errno==ENOENT?0:-1;return S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0022)?1:-1;}
static int lock_open(const sb_ts_options *o)
{
 if(!trusted_directory(o->run_dir))return -1;
 char path[600];if(!join(path,sizeof path,o->run_dir,"tinysc.lock"))return -1;
 int fd=open(path,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -1;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077)){close(fd);return -1;}return fd;
}
static int has_latch(const sb_ts_options *o)
{char path[600];struct stat st;if(!join(path,sizeof path,o->run_dir,"tinysc.latch"))return -1;
 if(!lstat(path,&st))return 1;
 return errno==ENOENT?0:-1;}
static int original(const sb_ts_options *o,const char *action)
{
 struct stat st;if(lstat(o->original_service,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0022))return 77;
 pid_t pid=fork();if(pid<0)return 77;
 if(!pid){execl("/bin/sh","sh",o->original_service,action,(char *)NULL);_exit(127);}
 int status;while(waitpid(pid,&status,0)<0){if(errno!=EINTR)return 77;}
 return WIFEXITED(status)?WEXITSTATUS(status):77;
}
static const char *pid_name(unsigned role)
{return role==1?"squeezecenter.pid":role==2?"scanner.pid":"gdresized.pid";}
static int stop_process(const sb_ts_options *o,const sb_ts_process *p)
{
 identity id;if(!process_stat(p->pid,&id)||id.state=='Z'||id.state=='X')return 1;
 int uncertain=0;if(id.start!=p->start||classify(&id,&uncertain)!=p->role||uncertain)return 0;
 const char *run="/var/run";
#ifdef SB_TINYSC_TEST
 const char *test=getenv("SB_TS_TEST_PID_DIR");if(test)run=test;
#endif
 char path[600];if(!join(path,sizeof path,run,pid_name(p->role)))return 0;
 struct stat st;if(!lstat(path,&st)&&(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0022)))return 0;
 char old[64];if(read_file(path,old,sizeof old)){
  old[strcspn(old,"\r\n")]=0;uint64_t value;
  if(!number(old,&value)||value<2||value>INT_MAX)return 0;
  identity other;
  if(value!=(uint64_t)p->pid&&process_stat((pid_t)value,&other)&&other.state!='Z'&&other.state!='X'){
   int bad=0;if(classify(&other,&bad)!=p->role||bad)return 0;
  }
 }
 char tmp[640];if(snprintf(tmp,sizeof tmp,"%s.sbts-XXXXXX",path)>=(int)sizeof tmp)return 0;
 int fd=mkstemp(tmp);if(fd<0)return 0;
 char bytes[64];int size=snprintf(bytes,sizeof bytes,"%ld\n",(long)p->pid);
 int ok=!fchmod(fd,0644)&&write(fd,bytes,(size_t)size)==size;close(fd);
 if(!process_stat(p->pid,&id)||id.start!=p->start)ok=0;
 if(!ok||rename(tmp,path)){unlink(tmp);return 0;}
 const char *action=p->role==1?"stopserver":p->role==2?"stopscan":"stopresizer";
 return original(o,action)==0;
}
int sb_ts_service(const sb_ts_options *o,const char *action)
{
 if(!o||!action||!o->policy_path||!o->run_dir||!o->original_service)return 77;
 int enabled=policy_enabled(o);
 if(enabled<0)return 77;
 int stopping=!strcmp(action,"stop")||!strcmp(action,"stopserver")||!strcmp(action,"stopscan")||!strcmp(action,"stopresizer");
 /* An installed but disabled wrapper delegates every stock action unchanged.
  * A same-boot quarantine requires explicit recovery before disabling. */
 if(!enabled){
  int latch=has_latch(o);
  if(latch==0)return original(o,action);
  if(latch<0||!stopping)return 77;
 }
 if(stopping){
  sb_ts_scan scan;if(!sb_ts_detect(&scan)||scan.uncertain)return 77;
  const unsigned order[]={2,1,3};
  for(unsigned r=0;r<3;r++)for(unsigned n=0;n<scan.count;n++){
   sb_ts_process *p=&scan.rows[n];if(p->role!=order[r])continue;
   if(strcmp(action,"stop")&&strcmp(action,p->role==1?"stopserver":p->role==2?"stopscan":"stopresizer"))continue;
   if(!stop_process(o,p))return 77;
  }return 0;
 }
 int lease=lock_open(o);if(lease<0)return 77;
 if(flock(lease,LOCK_SH|LOCK_NB)||has_latch(o)!=0){close(lease);fputs("SBTS start suppressed: Base arbitration/quarantine\n",stderr);return 77;}
 /* CLOEXEC keeps the lease in this parent, never in daemon descendants.
  * Original recursive service calls must target the public guarded script. */
 int result=original(o,action);close(lease);return result;
}
static int music_absent(void)
{
 DIR *dir=opendir("/proc");if(!dir)return 0;
 struct dirent *entry;unsigned count=0;int ok=1;
 while((entry=readdir(dir))){
  uint64_t pid;if(!number(entry->d_name,&pid)||pid<2||pid>INT_MAX)continue;
  if(++count>TS_TABLE_MAX){ok=0;break;}
  identity id;if(!process_stat((pid_t)pid,&id)||id.state=='Z'||id.state=='X')continue;
  char path[80],exe[512];snprintf(path,sizeof path,"/proc/%ld/exe",(long)pid);
  ssize_t n=readlink(path,exe,sizeof exe-1);if(n<0)continue;
  if(n>=(ssize_t)sizeof exe-1){ok=0;break;}exe[n]=0;
  const char *leaf=strrchr(exe,'/');leaf=leaf?leaf+1:exe;
  if(!strcmp(leaf,"sbbase_music")||!strcmp(leaf,"sbwebserver_music")||
     !strcmp(leaf,"sbmusic-session")||!strcmp(leaf,"sbmusic-metadata-helper")){ok=0;break;}
  if(!strcmp(leaf,"sbbase")){
   char args[4096];snprintf(path,sizeof path,"/proc/%ld/cmdline",(long)pid);
   int length=read_file(path,args,sizeof args);if(!length){ok=0;break;}
   for(int at=0;at<length;){
    if(!strncmp(args+at,"--music-",8)){ok=0;break;}
    at+=(int)strlen(args+at)+1;
   }
   if(!ok)break;
  }
 }
 closedir(dir);return ok;
}
int sb_ts_release(const sb_ts_options *o)
{
 if(!o||!o->run_dir||!o->policy_path||policy_enabled(o)!=0)return 77;
 int lease=lock_open(o);if(lease<0)return 77;
 if(flock(lease,LOCK_EX|LOCK_NB)||!music_absent()){close(lease);return 77;}
 char disabled[600],temporary[640];
 if(!join(disabled,sizeof disabled,o->run_dir,"heartbeat.stop")||
  snprintf(temporary,sizeof temporary,"%s.XXXXXX",disabled)>=(int)sizeof temporary){close(lease);return 77;}
 int fd=mkstemp(temporary);int written=fd>=0&&write(fd,"disabled\n",9)==9;
 if(fd>=0)close(fd);
 if(!written||rename(temporary,disabled)){if(fd>=0)unlink(temporary);close(lease);return 77;}
 char path[600];int ok=join(path,sizeof path,o->run_dir,"tinysc.latch");
 if(ok&&unlink(path)&&errno!=ENOENT)ok=0;
 /* Retain the identity journal until the reboot. Clearing suppression is
  * explicit; retaining forensic identities is harmless and conservative. */
 close(lease);return ok?0:77;
}
static void notify(sb_tinysc *a)
{char byte=1;ssize_t n=write(a->fd[1],&byte,1);(void)n;}
static void publish(sb_tinysc *a,sb_ts_state state,const sb_ts_scan *scan,const char *error)
{
 pthread_mutex_lock(&a->lock);
 int changed=a->status.state!=state||a->status.count!=scan->count||strcmp(a->status.error,error?error:"");
 a->status.state=state;a->status.count=scan->count;a->status.ready=state==SB_TS_STOPPED_VERIFIED;
 a->status.observed_ms=sb_ts_clock();
 memcpy(a->status.processes,scan->rows,sizeof scan->rows);
 snprintf(a->status.error,sizeof a->status.error,"%s",error?error:"");
 if(changed)a->status.revision++;
 pthread_mutex_unlock(&a->lock);if(changed)notify(a);
}
static int make_latch(sb_tinysc *a)
{
 char path[600],tmp[640];if(!join(path,sizeof path,a->run,"tinysc.latch"))return 0;
 if(snprintf(tmp,sizeof tmp,"%s.XXXXXX",path)>=(int)sizeof tmp)return 0;
 int fd=mkstemp(tmp);if(fd<0)return 0;
 identity me;if(!identify(getpid(),&me)){close(fd);unlink(tmp);return 0;}
 char bytes[256];int n=snprintf(bytes,sizeof bytes,"SBTS1 %s %ld %llu quarantine-until-owner-recovery\n",a->boot,(long)getpid(),(unsigned long long)me.start);
 int ok=!fchmod(fd,0600)&&write(fd,bytes,(size_t)n)==n;close(fd);
 if(!ok||rename(tmp,path)){unlink(tmp);return 0;}return 1;
}
static int journal_read(sb_tinysc *a,sb_ts_scan *scan)
{
 char path[600];struct stat st;
 if(!join(path,sizeof path,a->run,"tinysc.identities"))return 0;
 if(lstat(path,&st))return errno==ENOENT;
 if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))return 0;
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 FILE *f=fdopen(fd,"r");if(!f){close(fd);return 0;}
 char line[256],boot[64];int used=0,ok=0;
 if(!fgets(line,sizeof line,f)||sscanf(line,"SBTS1 %63s %n",boot,&used)!=1||line[used]||strcmp(boot,a->boot))goto done;
 while(fgets(line,sizeof line,f)){
  if(scan->count==SB_TS_MAX_PROCESSES)goto done;
  long pid;unsigned long long start;unsigned role;used=0;
  if(sscanf(line,"%ld %llu %u %n",&pid,&start,&role,&used)!=3||line[used]||pid<2||pid>INT_MAX||!start||role<1||role>5)goto done;
  sb_ts_process *p=&scan->rows[scan->count++];p->pid=(pid_t)pid;p->start=start;p->role=role;
 }
 ok=!ferror(f);
done:fclose(f);return ok;
}
static int journal_write(sb_tinysc *a,const sb_ts_scan *scan)
{
 char path[600],tmp[640];if(!join(path,sizeof path,a->run,"tinysc.identities"))return 0;
 if(snprintf(tmp,sizeof tmp,"%s.XXXXXX",path)>=(int)sizeof tmp)return 0;
 int fd=mkstemp(tmp);if(fd<0)return 0;
 FILE *f=fdopen(fd,"w");if(!f){close(fd);unlink(tmp);return 0;}
 int ok=fprintf(f,"SBTS1 %s\n",a->boot)>0;
 for(unsigned n=0;n<scan->count;n++){
  const sb_ts_process *p=&scan->rows[n];
  if(fprintf(f,"%ld %llu %u\n",(long)p->pid,(unsigned long long)p->start,p->role)<0)ok=0;
 }
 if(fclose(f))ok=0;
 if(!ok||rename(tmp,path)){unlink(tmp);return 0;}return 1;
}
static int stopped(sb_tinysc *a)
{pthread_mutex_lock(&a->lock);int result=a->stop;pthread_mutex_unlock(&a->lock);return result;}
static void pause_worker(sb_tinysc *a,unsigned ms)
{
 struct timespec until;clock_gettime(CLOCK_MONOTONIC,&until);
 until.tv_sec+=ms/1000;until.tv_nsec+=(long)(ms%1000)*1000000;
 if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}
 pthread_mutex_lock(&a->lock);if(!a->stop)pthread_cond_timedwait(&a->changed,&a->lock,&until);pthread_mutex_unlock(&a->lock);
}
static void reap_request(pid_t child)
{
 /* The request is a private process group, never the daemon's group. Bound
  * shutdown even if firmware stop scripts block or ignore SIGTERM. */
 int group=getpgid(child)==child;
 kill(group?-child:child,SIGTERM);
 uint64_t until=sb_ts_clock()+200;
 while(sb_ts_clock()<until){
  /* Keep the leader unreaped until after the final group signal: this pins
   * its PID and covers firmware grandchildren ignoring SIGTERM. */
  struct timespec delay={0,10000000};nanosleep(&delay,NULL);
 }
 kill(group?-child:child,SIGKILL);
 /* Never wait on an uninterruptible child on the Base shutdown path. The
  * kernel/reaper collects it if exit is delayed by uninterruptible I/O. */
 (void)waitpid(child,NULL,WNOHANG);
}
static void *worker(void *context)
{
 sb_tinysc *a=context;sb_ts_scan previous={0},scan={0};
 publish(a,SB_TS_DETECTING,&scan,NULL);
 if(!make_latch(a)){publish(a,SB_TS_FAILED,&scan,"cannot establish fail-closed lifecycle latch");return NULL;}
 a->lease=lock_open(&a->options);
 uint64_t deadline=sb_ts_clock()+a->options.stop_timeout_ms;
 while(a->lease>=0&&flock(a->lease,LOCK_EX|LOCK_NB)){
  if(stopped(a))return NULL;
  if(sb_ts_clock()>=deadline){publish(a,SB_TS_FAILED,&scan,"TinySC launch lease did not settle");return NULL;}
  pause_worker(a,50);
 }
 if(a->lease<0){publish(a,SB_TS_FAILED,&scan,"cannot acquire lifecycle lease");return NULL;}
 if(!journal_read(a,&previous)){publish(a,SB_TS_FAILED,&scan,"cannot reconcile prior TinySC child identities");return NULL;}
 unsigned empty=0;pid_t request=0;int requested=0;
 while(!stopped(a)){
  if(!detect(&scan,&previous)||scan.uncertain){publish(a,SB_TS_FAILED,&scan,"incomplete/uncertain process identity");break;}
  /* Persist discovered descendants before a service stop can orphan them.
   * Private RAM survives Base crashes, but not an accepted normal reboot. */
  if(memcmp(&previous,&scan,sizeof scan)&&!journal_write(a,&scan)){
   publish(a,SB_TS_FAILED,&scan,"cannot retain TinySC child identities");break;
  }
  previous=scan;
  if(request){int status;pid_t got=waitpid(request,&status,WNOHANG);
   if(got==request){request=0;if(!WIFEXITED(status)||WEXITSTATUS(status)){publish(a,SB_TS_FAILED,&scan,"firmware stop request failed");break;}}
   else if(got<0&&errno!=EINTR){publish(a,SB_TS_FAILED,&scan,"lost firmware stop request identity");break;}
   else if(sb_ts_clock()>=deadline){publish(a,SB_TS_FAILED,&scan,"firmware stop request exceeded deadline");break;}
  }
  if(scan.count){
   empty=0;
   if(!requested){
    pthread_mutex_lock(&a->lock);if(a->status.ready&&a->status.conflicts<UINT_MAX)a->status.conflicts++;pthread_mutex_unlock(&a->lock);
    publish(a,SB_TS_STOP_REQUESTED,&scan,NULL);deadline=sb_ts_clock()+a->options.stop_timeout_ms;
    request=fork();
    if(!request){if(setsid()<0)_exit(127);sb_process_close_fds(a->close_limit,-1);execl(a->control,a->control,"service","stop",(char *)NULL);_exit(127);}
    if(request<0){request=0;publish(a,SB_TS_FAILED,&scan,"cannot request firmware stop");break;}
    requested=1;
   }
   publish(a,SB_TS_STOPPING,&scan,NULL);
   if(sb_ts_clock()>=deadline){publish(a,SB_TS_FAILED,&scan,"TinySC process/child remains after stop deadline");break;}
  }else if(!request){
   if(++empty>=2){publish(a,SB_TS_STOPPED_VERIFIED,&scan,NULL);requested=0;}
  }
  pause_worker(a,scan.count||empty<2?100:a->options.poll_ms);
 }
 if(request)reap_request(request);
 return NULL;
}
static int copy_option(char *to,size_t cap,const char *from)
{if(!from||from[0]!='/'||strlen(from)>=cap)return 0;strcpy(to,from);return 1;}
sb_tinysc *sb_ts_start(const sb_ts_options *o)
{
 if(!o||!o->enabled)return NULL;
#ifndef SB_TINYSC_TEST
 char cpu[8192],version[512];
 if(geteuid()!=0||!read_file("/proc/cpuinfo",cpu,sizeof cpu)||!strstr(cpu,"Logitech Fab4 Board")||
  !read_file("/etc/squeezeos.version",version,sizeof version)||strncmp(version,"7.7.3 r16676\n",13))return NULL;
#endif
 sb_tinysc *a=calloc(1,sizeof *a);if(!a)return NULL;
 a->lease=-1;a->fd[0]=a->fd[1]=-1;a->options=*o;
#ifdef SB_TINYSC_TEST
 {
  const char *run=getenv("SB_TS_TEST_RUN_DIR"),*policy=getenv("SB_TS_TEST_POLICY");
  if(run)a->options.run_dir=run;
  if(policy)a->options.policy_path=policy;
 }
#endif
 if(!sb_process_fd_limit(&a->close_limit))goto fail;
 if(!copy_option(a->run,sizeof a->run,a->options.run_dir)||!copy_option(a->policy,sizeof a->policy,a->options.policy_path)||
  !copy_option(a->original,sizeof a->original,o->original_service)||!copy_option(a->control,sizeof a->control,o->control_path)||
  !trusted_directory(a->run))goto fail;
 a->options.run_dir=a->run;a->options.policy_path=a->policy;a->options.original_service=a->original;a->options.control_path=a->control;
 if(policy_enabled(&a->options)!=1||!read_file("/proc/sys/kernel/random/boot_id",a->boot,sizeof a->boot))goto fail;
 a->boot[strcspn(a->boot,"\r\n")]=0;
 identity self;if(!identify(getpid(),&self))goto fail;a->pid=self.pid;a->start=self.start;
 if(!a->options.poll_ms)a->options.poll_ms=500;
 if(a->options.poll_ms>1000||a->options.poll_ms<50)goto fail;
 if(!a->options.stop_timeout_ms)a->options.stop_timeout_ms=15000;
 if(a->options.stop_timeout_ms>30000||a->options.stop_timeout_ms<100)goto fail;
 if(pipe(a->fd))goto fail;
 for(unsigned n=0;n<2;n++)if(fcntl(a->fd[n],F_SETFD,FD_CLOEXEC)<0||fcntl(a->fd[n],F_SETFL,O_NONBLOCK)<0)goto fail;
 if(pthread_mutex_init(&a->lock,NULL))goto fail;
 pthread_condattr_t condition;
 if(pthread_condattr_init(&condition)){pthread_mutex_destroy(&a->lock);goto fail;}
 int condition_error=pthread_condattr_setclock(&condition,CLOCK_MONOTONIC);
 if(!condition_error)condition_error=pthread_cond_init(&a->changed,&condition);
 pthread_condattr_destroy(&condition);
 if(condition_error){pthread_mutex_destroy(&a->lock);goto fail;}
 a->status.enabled=1;a->status.revision=1;
 pthread_attr_t attributes;int attribute_error=pthread_attr_init(&attributes);
 if(!attribute_error){
  attribute_error=pthread_attr_setstacksize(&attributes,128*1024);
  if(!attribute_error)attribute_error=pthread_create(&a->thread,&attributes,worker,a);
  pthread_attr_destroy(&attributes);
 }
 if(attribute_error){pthread_cond_destroy(&a->changed);pthread_mutex_destroy(&a->lock);goto fail;}
 return a;
fail:if(a->fd[0]>=0)close(a->fd[0]);if(a->fd[1]>=0)close(a->fd[1]);free(a);return NULL;
}
int sb_ts_snapshot(sb_tinysc *a,sb_ts_status *s)
{if(!a||!s)return 0;pthread_mutex_lock(&a->lock);*s=a->status;pthread_mutex_unlock(&a->lock);return 1;}
int sb_ts_ready(sb_tinysc *a)
{sb_ts_status s;return sb_ts_snapshot(a,&s)&&s.ready&&sb_ts_clock()-s.observed_ms<=1250;}
int sb_ts_music_allowed(sb_tinysc *a)
{sb_ts_status s;return sb_ts_snapshot(a,&s)&&s.ready&&!s.conflicts&&sb_ts_clock()-s.observed_ms<=1250;}
void sb_ts_operational(sb_tinysc *a,int value)
{if(!a)return;pthread_mutex_lock(&a->lock);a->status.operational=!!value;a->status.revision++;pthread_mutex_unlock(&a->lock);}
int sb_ts_fd(sb_tinysc *a){return a?a->fd[0]:-1;}
void sb_ts_drain(sb_tinysc *a){if(a){char bytes[64];while(read(a->fd[0],bytes,sizeof bytes)>0){}}}
void sb_ts_close(sb_tinysc *a)
{
 if(!a)return;
 pthread_mutex_lock(&a->lock);a->stop=1;pthread_cond_signal(&a->changed);pthread_mutex_unlock(&a->lock);
 pthread_join(a->thread,NULL);if(a->lease>=0)close(a->lease);close(a->fd[0]);close(a->fd[1]);
 pthread_cond_destroy(&a->changed);pthread_mutex_destroy(&a->lock);free(a);
 /* Quarantine latch deliberately survives Base exit within this boot. */
}
int sb_ts_json(sb_tinysc *a,char *out,unsigned cap)
{
 if(!out||!cap)return 0;
 sb_ts_status s={0};if(a&&!sb_ts_snapshot(a,&s))return 0;
 int fresh=s.enabled&&s.observed_ms&&sb_ts_clock()-s.observed_ms<=1250;
 int n=snprintf(out,cap,"{\"actor\":\"sbbase\",\"api\":\"SBTS-WP4A2B-1\",\"pid\":%ld,\"start\":\"%llu\",\"boot\":\"%s\",\"enabled\":%s,\"state\":\"%s\",\"initialized\":%s,\"operational\":%s,\"fresh\":%s,\"music_admission\":%s,\"count\":%u,\"revision\":\"%llu\",\"observed_ms\":\"%llu\",\"error\":\"%s\",\"truncated\":%s,\"processes\":[",
  a?(long)a->pid:0,a?(unsigned long long)a->start:0,a?a->boot:"",
  s.enabled?"true":"false",sb_ts_state_name(s.state),s.operational?"true":"false",
  s.operational&&s.ready&&fresh?"true":"false",fresh?"true":"false",s.operational&&sb_ts_music_allowed(a)?"true":"false",s.count,
  (unsigned long long)s.revision,(unsigned long long)s.observed_ms,s.error,s.count>8?"true":"false");
 if(n<=0||(unsigned)n>=cap)return 0;
 unsigned used=(unsigned)n;
 for(unsigned i=0;i<s.count&&i<8;i++){
  const sb_ts_process *p=&s.processes[i];
  char state=p->state>='A'&&p->state<='Z'?p->state:'?';
  n=snprintf(out+used,cap-used,"%s{\"pid\":%ld,\"start\":\"%llu\",\"parent\":%ld,\"role\":%u,\"state\":\"%c\"}",
   i?",":"",(long)p->pid,(unsigned long long)p->start,(long)p->parent,p->role,state);
  if(n<0||(unsigned)n>=cap-used)return 0;
  used+=(unsigned)n;
 }
 n=snprintf(out+used,cap-used,"]}");return n==2&&used+2<cap;
}
