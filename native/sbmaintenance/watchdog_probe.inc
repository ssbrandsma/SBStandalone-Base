/* Read-only restart preflight. Never opens the watchdog device or mutates IPC. */
#include <ctype.h>
typedef struct { identity feeder,watched,children[4];int semid;unsigned slots,fd,child_count; } restart_profile;
/* Fab4 7.7.3 r16676 has two observed Jive watchdog registrations.
 * Countdown GETPID is not ownership: the stock feeder also decrements it.
 * Registration evidence is distinct from the challenge-bound UI witness. */
#define FAB4_JIVE_KEEPALIVES 2
static const char *watchdog_executable(void)
{
#ifdef SB_MAINTENANCE_TEST
 const char *value=getenv("SB_TEST_WATCHDOG");if(value)return value;
#endif
 return "/usr/sbin/watchdog";
}
static const char *watchdog_node(void)
{
#ifdef SB_MAINTENANCE_TEST
 const char *value=getenv("SB_TEST_WATCHDOG_NODE");if(value)return value;
#endif
 return "/dev/watchdog";
}
static const char *watchdog_config(void)
{
#ifdef SB_MAINTENANCE_TEST
 const char *value=getenv("SB_TEST_WATCHDOG_CONFIG");if(value)return value;
#endif
 return "/etc/watchdog.conf";
}
static int restart_error(const char *message)
{fprintf(stderr,"restart preflight refused: %s\n",message);return 0;}
static int verify_watchdog_config(void)
{
 static const char *keys[]={"pidfile","min-memory","semkey","repair-binary",
  "watchdog-device","admin","startup-delay","interval","logtick","realtime","priority"};
 const char *values[]={pidfile,"0","1234","/usr/sbin/repair.sh",watchdog_node(),"","20","5","0","yes","1"};
 char buffer[4096];struct stat st;
 if(lstat(watchdog_config(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||
    (st.st_mode&0022)||!read_file(watchdog_config(),buffer,sizeof buffer))return 0;
 unsigned found=0;char *save=NULL;
 for(char *line=strtok_r(buffer,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
  while(isspace((unsigned char)*line))line++;
  if(!*line||*line=='#')continue;
  char *equal=strchr(line,'=');if(!equal)return 0;
  char *end=equal;while(end>line&&isspace((unsigned char)end[-1]))end--;*end=0;
  char *value=equal+1;while(isspace((unsigned char)*value))value++;
  end=value+strlen(value);while(end>value&&isspace((unsigned char)end[-1]))*--end=0;
  unsigned n;for(n=0;n<sizeof keys/sizeof keys[0];n++)if(!strcmp(line,keys[n]))break;
  if(n==sizeof keys/sizeof keys[0]||(found&(1u<<n))||strcmp(value,values[n]))return 0;
  found|=1u<<n;
 }
 return found==((1u<<(sizeof keys/sizeof keys[0]))-1);
}
static int watchdog_owner(restart_profile *out)
{
 struct stat node;
 if(lstat(watchdog_node(),&node))return restart_error("watchdog node unavailable");
#ifndef SB_MAINTENANCE_TEST
 if(!S_ISCHR(node.st_mode))return restart_error("watchdog node type mismatch");
#endif
 DIR *processes=opendir("/proc");if(!processes)return 0;
 struct dirent *entry;unsigned count=0,descriptors=0,owners=0,feeders=0;int ok=1;
 while((entry=readdir(processes))){
  uint64_t pid;if(!number(entry->d_name,&pid)||pid<2||pid>INT_MAX)continue;
  if(++count>4096){ok=0;break;}
  identity id;if(!process_stat((pid_t)pid,&id)||id.state=='Z'||id.state=='X')continue;
  char path[128],exe[512];snprintf(path,sizeof path,"/proc/%ld/exe",(long)pid);
  ssize_t bytes=readlink(path,exe,sizeof exe-1);if(bytes<0)continue;
  if(bytes>=(ssize_t)sizeof exe-1){ok=0;break;}exe[bytes]=0;
  if(!strcmp(exe,alsa_executable())&&id.parent==out->watched.pid&&id.start>=out->watched.start){
   if(out->child_count==4||!identify(id.pid,&out->children[out->child_count])){ok=0;break;}
   out->child_count++;
  }
  int feeder=!strcmp(exe,watchdog_executable());if(feeder)feeders++;
  snprintf(path,sizeof path,"/proc/%ld/fd",(long)pid);
  DIR *fds=opendir(path);if(!fds){identity live;if(process_stat((pid_t)pid,&live)&&live.start==id.start)ok=0;continue;}
  struct dirent *fd;unsigned per_process=0;
  while((fd=readdir(fds))){
   uint64_t descriptor;if(!number(fd->d_name,&descriptor))continue;
   if(++per_process>512||++descriptors>8192){ok=0;break;}
   if(snprintf(path,sizeof path,"/proc/%ld/fd/%llu",(long)pid,(unsigned long long)descriptor)>=(int)sizeof path){ok=0;break;}
   struct stat target;if(stat(path,&target))continue;
#ifdef SB_MAINTENANCE_TEST
   int match=target.st_dev==node.st_dev&&target.st_ino==node.st_ino;
#else
   int match=S_ISCHR(target.st_mode)&&target.st_rdev==node.st_rdev;
#endif
   if(match){
    struct stat link;
    if(lstat(path,&link)||!S_ISLNK(link.st_mode)||!(link.st_mode&S_IWUSR))ok=0;
    owners++;
    if(!feeder||!identify((pid_t)pid,&out->feeder)||out->feeder.start!=id.start||descriptor>INT_MAX)ok=0;
    out->fd=(unsigned)descriptor;
   }
  }
  closedir(fds);if(!ok)break;
 }
 closedir(processes);
 if(!ok||owners!=1||feeders!=1)return restart_error("require exactly one identified feeder and one watchdog descriptor");
 return 1;
}
static int restart_preflight(restart_profile *out)
{
 memset(out,0,sizeof *out);
 if(!profile()||!verify_watchdog_config())return restart_error("firmware/watchdog configuration mismatch");
 struct stat st;
 if(lstat(pidfile,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0022))
  return restart_error("untrusted watched PID file");
 char bytes[64];uint64_t pid;
 if(!read_file(pidfile,bytes,sizeof bytes))return restart_error("watched PID file unavailable");
 bytes[strcspn(bytes,"\r\n")]=0;
 if(!number(bytes,&pid)||pid<2||pid>INT_MAX||!identify((pid_t)pid,&out->watched)||strcmp(out->watched.exe,jive))
  return restart_error("watched PID is not the current Jive incarnation");
 if(!watchdog_owner(out))return 0;
 out->semid=semget((key_t)1234,0,0);if(out->semid<0)return restart_error("firmware IPC set unavailable");
 struct semid_ds meta;union semarg arg;arg.buf=&meta;
 if(semctl(out->semid,0,IPC_STAT,arg)<0||meta.sem_nsems!=20)return restart_error("unexpected firmware IPC layout");
 out->slots=(unsigned)meta.sem_nsems;
 unsigned short values[20];arg.array=values;
 if(semctl(out->semid,0,GETALL,arg)<0)return restart_error("cannot inspect firmware IPC");
 unsigned ui=0;
 for(unsigned n=0;n<out->slots;n+=2){
  if(values[n]>1)return restart_error("uncertain IPC allocation");
  if(values[n]==0){
   if(!values[n+1])return restart_error("an allocated watchdog IPC countdown has expired");
   int actor=semctl(out->semid,(int)n,GETPID);
   if(actor<0)return restart_error("cannot identify IPC keepalive actor");
   if(actor==out->watched.pid)ui++;
  }
 }
 if(ui!=FAB4_JIVE_KEEPALIVES||!same(out->watched.pid,out->watched.start)||!same(out->feeder.pid,out->feeder.start))
  return restart_error("missing/stale Fab4 Jive registrations or feeder identity");
 return 1;
}
static int show_restart_preflight(void)
{
 restart_profile p;if(!restart_preflight(&p))return 2;
 printf("{\"actor\":\"restart_preflight\",\"read_only\":true,\"jive_pid\":%ld,\"jive_start\":\"%llu\",\"feeder_pid\":%ld,\"feeder_start\":\"%llu\",\"watchdog_fd\":%u,\"semid\":%d,\"slots\":%u,\"jive_registrations\":%u}\n",
  (long)p.watched.pid,(unsigned long long)p.watched.start,(long)p.feeder.pid,
  (unsigned long long)p.feeder.start,p.fd,p.semid,p.slots,FAB4_JIVE_KEEPALIVES);
 return 0;
}
