/* Bounded maintenance primitives. Never opens /dev/watchdog or changes IPC.
 * No boot/startup caller. Test path overrides are absent from production.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/statfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../common/process_identity.h"
#include "../common/process_spawn.h"
static char self[512];
static const char *pidfile="/var/run/squeezeplay.pid",*jive="/usr/bin/jive";
static const char *media="/media/sda1";
static int holder(const identity *i)
{
 size_t first=strlen(i->args);
 return !strcmp(i->exe,self)&&first+1<sizeof i->args&&
  (!strcmp(i->args+first+1,"hold")||!strcmp(i->args+first+1,"coordinate"));
}
static int watchable(const identity *i)
{return holder(i)||!strcmp(i->exe,jive);}
static const char *base_root(void)
{
#ifdef SB_MAINTENANCE_TEST
 const char *value=getenv("SB_TEST_BASE_ROOT");if(value)return value;
#endif
 return "/usr/share/jive/applets/StandaloneBase";
}
static const char *alsa_executable(void)
{
#ifdef SB_MAINTENANCE_TEST
 const char *value=getenv("SB_TEST_ALSA");if(value)return value;
#endif
 return "/usr/bin/jive_alsa";
}
static int base_role(const identity *i)
{
 const char *names[]={"sbbase","sbbase-core","sbbase-music","sbproxy",
  "sbwebserver","sbwebserver-music","sbmusic-session"};
 const int roles[]={1,1,1,2,3,3,4};char path[600];
 for(unsigned n=0;n<sizeof roles/sizeof roles[0];n++){
  snprintf(path,sizeof path,"%s/%s",base_root(),names[n]);if(!strcmp(i->exe,path))return roles[n];
  snprintf(path,sizeof path,"%s/%s (deleted)",base_root(),names[n]);if(!strcmp(i->exe,path))return roles[n];
 }
 const char *leaf=NULL;size_t root_length=strlen(base_root());
 if(!strncmp(i->exe,base_root(),root_length)&&!strncmp(i->exe+root_length,"/generations/",13)){
  const char *generation=i->exe+root_length+13;
  if(strlen(generation)>=66){
   unsigned n=0;while(n<64&&strchr("0123456789abcdef",generation[n]))n++;
   if(n==64&&generation[64]=='/')leaf=generation+65;
  }
 }
 if(leaf)for(unsigned n=0;n<sizeof roles/sizeof roles[0];n++)
  if(!strcmp(leaf,names[n]))return roles[n];
 return 0;
}
static int managed(const identity *i)
{
 if(watchable(i))return 1;
 return base_role(i)!=0;
}
static const char *base_generation(const identity *i)
{
 size_t length=strlen(base_root());
 if(!strncmp(i->exe,base_root(),length)&&!strncmp(i->exe+length,"/generations/",13)&&base_role(i))
  return i->exe+length+13;
 return NULL;
}
static int profile(void)
{
#ifdef SB_MAINTENANCE_TEST
 return 1;
#else
 char cpu[8192],version[512];
 return geteuid()==0&&read_file("/proc/cpuinfo",cpu,sizeof cpu)&&
  strstr(cpu,"Logitech Fab4 Board")&&read_file("/etc/squeezeos.version",version,sizeof version)&&
  !strncmp(version,"7.7.3 r16676\n",sizeof "7.7.3 r16676\n"-1);
#endif
}
static void json_string(const char *s)
{
 putchar('"');for(;*s;s++){
  unsigned char c=(unsigned char)*s;
  if(c=='"'||c=='\\'){putchar('\\');putchar(c);}
  else if(c<32||c>=127)printf("\\u%04x",c);else putchar(c);
 }putchar('"');
}
static void show(const identity *i)
{
 printf("{\"pid\":%ld,\"start\":\"%llu\",\"exe\":",(long)i->pid,(unsigned long long)i->start);
 json_string(i->exe);printf(",\"state\":\"%c\"}\n",i->state);
}
static int spawn_identified(int holding,identity *result)
{
 unsigned limit;if(!sb_process_fd_limit(&limit))return 2;
 int channel[2];if(pipe(channel))return 2;
 if(fcntl(channel[1],F_SETFD,FD_CLOEXEC)<0){close(channel[0]);close(channel[1]);return 2;}
 pid_t pid=fork();if(pid<0){close(channel[0]);close(channel[1]);return 2;}
 if(!pid){
  close(channel[0]);if(setsid()<0)goto failed;
  signal(SIGHUP,SIG_IGN);
  int null=open("/dev/null",O_RDWR);if(null<0)goto failed;
  for(int fd=0;fd<3;fd++)if(dup2(null,fd)<0)goto failed;
  sb_process_close_fds(limit,channel[1]);
  if(holding==2)execl(self,self,"coordinate",(char *)NULL);
  else if(holding)execl(self,self,"hold",(char *)NULL);
  else{
   if(chdir("/usr/bin"))goto failed;
#ifdef SB_MAINTENANCE_TEST
   const char *sem=getenv("SB_TEST_UI_SEMID"),*run=getenv("SB_TEST_COORDINATOR_RUN"),*mode=getenv("SB_TEST_UI_MODE");
   if(sem&&run&&mode)execl(jive,jive,"ui",sem,run,mode,(char *)NULL);
#endif
   char *env[]={"SDL_NOMOUSE=1","ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf",
    "SQUEEZEPLAY_HOME=/etc/squeezeplay","HOME=/root","LANG=en_US.utf8",
    "PATH=/usr/sbin:/usr/bin:/sbin:/bin",NULL};
   char *args[]={(char *)jive,NULL};execve(jive,args,env);
  }
 failed: {int error=errno;ssize_t sent=write(channel[1],&error,sizeof error);(void)sent;_exit(127);}
 }
 close(channel[1]);int error;ssize_t n;
 do{n=read(channel[0],&error,sizeof error);}while(n<0&&errno==EINTR);
 close(channel[0]);if(n!=0){waitpid(pid,NULL,0);return 2;}
 identity i;if(!identify(pid,&i)){waitpid(pid,NULL,WNOHANG);return 2;}*result=i;return 0;
}
static int spawn(int holding)
{identity result;if(spawn_identified(holding,&result))return 2;show(&result);return 0;}
static int dead(pid_t pid,uint64_t start)
{
 identity i;
 if(process_stat(pid,&i))return i.start==start&&(i.state=='Z'||i.state=='X');
 return kill(pid,0)<0&&errno==ESRCH;
}
static int swap_pid_locked(uint64_t old,uint64_t oldstart,uint64_t next,uint64_t nextstart,int recover)
{
 identity a,b;char bytes[64];struct stat st;
 if(old<2||old>INT_MAX||next<2||next>INT_MAX||old==next||!oldstart||!nextstart||
  (recover?!dead((pid_t)old,oldstart):
   (!identify((pid_t)old,&a)||a.start!=oldstart||!watchable(&a)))||
  !identify((pid_t)next,&b)||b.start!=nextstart||!watchable(&b))return 2;
 if(lstat(pidfile,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0022))return 2;
 if(!read_file(pidfile,bytes,sizeof bytes))return 2;
 bytes[strcspn(bytes,"\r\n")]=0;uint64_t current;
 if(!number(bytes,&current)||current!=old)return 2;
 char temporary[600];if(snprintf(temporary,sizeof temporary,"%s.maintenance-XXXXXX",pidfile)>=(int)sizeof temporary)return 2;
 int fd=mkstemp(temporary);if(fd<0)return 2;
 int length=snprintf(bytes,sizeof bytes,"%llu\n",(unsigned long long)next);
 int ok=!fchmod(fd,0644)&&write(fd,bytes,(size_t)length)==length&&!fsync(fd);
 close(fd);
 if((recover?!dead((pid_t)old,oldstart):!same((pid_t)old,oldstart))||
    !same((pid_t)next,nextstart))ok=0;
 if(ok&&rename(temporary,pidfile))ok=0;
 if(!ok)unlink(temporary);
 return ok?0:2;
}
static int swap_pid(uint64_t old,uint64_t oldstart,uint64_t next,uint64_t nextstart,int recover)
{
 char path[600];struct stat st;
 if(snprintf(path,sizeof path,"%s.maintenance-lock",pidfile)>=(int)sizeof path)return 2;
 int fd=open(path,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return 2;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||
    (st.st_mode&0022)||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return 2;}
 int result=swap_pid_locked(old,oldstart,next,nextstart,recover);close(fd);return result;
}
union semarg { int val; struct semid_ds *buf; unsigned short *array; };
static int sem_snapshot(void)
{
 int id=semget((key_t)1234,0,0);if(id<0)return 2;
 struct semid_ds meta;union semarg arg;arg.buf=&meta;
 if(semctl(id,0,IPC_STAT,arg)<0||!meta.sem_nsems||meta.sem_nsems>64)return 2;
 unsigned short values[64];arg.array=values;if(semctl(id,0,GETALL,arg)<0)return 2;
 printf("{\"semid\":%d,\"slots\":[",id);
 for(unsigned n=0;n<meta.sem_nsems;n++){
  int pid=semctl(id,(int)n,GETPID);if(pid<0)return 2;
  printf("%s{\"value\":%u,\"pid\":%d}",n?",":"",values[n],pid);
 }puts("]}");return 0;
}
static int inventory(int fd,const char *prefix,unsigned *total)
{
 DIR *dir=fdopendir(fd);if(!dir){close(fd);return 0;}
 unsigned count=0;struct dirent *entry;int ok=1;
 errno=0;
 while((entry=readdir(dir))!=NULL){
  if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
  if(++count>512){ok=0;break;}
  struct stat st;char path[1024];
  if(snprintf(path,sizeof path,"%s%s",prefix,entry->d_name)>=(int)sizeof path||
     fstatat(dirfd(dir),entry->d_name,&st,AT_SYMLINK_NOFOLLOW)){ok=0;break;}
  printf("%s{\"path\":",(*total)++?",":"");json_string(path);
  printf(",\"inode\":%llu,\"mode\":%u,\"size\":%lld,\"atime\":%lld,\"mtime\":%lld,\"ctime\":%lld}",
   (unsigned long long)st.st_ino,(unsigned)st.st_mode,(long long)st.st_size,
   (long long)st.st_atime,(long long)st.st_mtime,(long long)st.st_ctime);
  errno=0;
 }
 if(errno)ok=0;
 closedir(dir);return ok;
}
static int mount_line(char *out,size_t size)
{
#ifdef SB_MAINTENANCE_TEST
 (void)size;strcpy(out,"host fixture");return 1;
#else
 FILE *f=fopen("/proc/self/mountinfo","r");if(!f)return 0;
 char line[2048];unsigned matches=0;
 while(fgets(line,sizeof line,f)){
  if(!strchr(line,'\n')){matches=0;break;}
  if(strstr(line," /media/sda1 ")){
   if(!strstr(line," - vfat /dev/sda1 ")||strlen(line)>=size){matches=0;break;}
   strcpy(out,line);matches++;
  }
 }
 fclose(f);return matches==1;
#endif
}
static int audit(void)
{
 if(!profile())return 2;
 char before[2048],after[2048];if(!mount_line(before,sizeof before))return 2;
 const int flags=O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC|O_NOATIME;
 int root=open(media,flags);if(root<0)return 2;
 struct statfs fs;struct stat root_stat;
 if(fstatfs(root,&fs)||fstat(root,&root_stat)){close(root);return 2;}
#ifndef SB_MAINTENANCE_TEST
 if((unsigned long)fs.f_type!=0x4d44UL){close(root);return 2;}
#endif
 int scan=openat(root,".",flags);if(scan<0){close(root);return 2;}
 unsigned total=0;
 printf("{\"root_inode\":%llu,\"root_atime\":%lld,\"root_mtime\":%lld,\"root_ctime\":%lld,\"entries\":[",
  (unsigned long long)root_stat.st_ino,(long long)root_stat.st_atime,
  (long long)root_stat.st_mtime,(long long)root_stat.st_ctime);
 int ok=inventory(scan,"",&total);
 int sc=openat(root,".Squeezebox",flags),missing_sc=sc<0&&errno==ENOENT,missing_cache=missing_sc;
 if(sc<0&&!missing_sc)ok=0;
 if(sc>=0){
  int cache=openat(sc,"cache",flags);missing_cache=cache<0&&errno==ENOENT;
  if(cache<0&&!missing_cache)ok=0;
  if(cache>=0&&!inventory(cache,".Squeezebox/cache/",&total))ok=0;
  if(!inventory(sc,".Squeezebox/",&total))ok=0;
 }
 close(root);
 if(!mount_line(after,sizeof after)||strcmp(before,after))ok=0;
 printf("],\"missing_squeezebox\":%s,\"missing_cache\":%s,\"complete\":%s}\n",
  missing_sc?"true":"false",missing_cache?"true":"false",ok?"true":"false");
 return ok?0:2;
}
#include "watchdog_probe.inc"
#include "coordinator.inc"
#include "quiesce.inc"
#include "internal_files.inc"
#include "executable_identity.inc"

int main(int argc,char **argv)
{
 unsigned limit;if(!sb_process_fd_limit(&limit))return 2;sb_process_close_fds(limit,-1);
 if(argc==2&&!strcmp(argv[1],"--version")){puts("SBMAINT-WP4A2B-1");return 0;}
 ssize_t n=readlink("/proc/self/exe",self,sizeof self-1);if(n<0||n>=(ssize_t)sizeof self-1)return 2;self[n]=0;
#ifdef SB_MAINTENANCE_TEST
 const char *p=getenv("SB_TEST_PIDFILE"),*j=getenv("SB_TEST_JIVE"),*m=getenv("SB_TEST_MEDIA");
 if(p)pidfile=p;
 if(j)jive=j;
 if(m)media=m;
#endif
 if(argc==2&&!strcmp(argv[1],"usb-audit"))return audit();
 if(argc==2&&!strcmp(argv[1],"hold")){
  if(!profile())return 2;
  /* Passive emergency maintenance holder, not a watchdog-device writer.
   * The coordinator has bounded startup/recovery deadlines. On failure the
   * holder stays until explicit SSH recovery; it must never expire into reset. */
  for(;;)pause();
 }
 if(argc==2&&!strcmp(argv[1],"sem"))return sem_snapshot();
 if(argc==2&&!strcmp(argv[1],"restart-preflight"))return show_restart_preflight();
 uint64_t pid,start;
 if(argc==3&&!strcmp(argv[1],"identity")&&number(argv[2],&pid)&&pid>=2&&pid<=INT_MAX){
  identity i;if(!identify((pid_t)pid,&i))return 2;show(&i);return 0;
 }
 if(!profile())return 2;
 if(argc==2&&!strcmp(argv[1],"coordinate"))return coordinate_restart();
 if(argc==2&&!strcmp(argv[1],"launch-restart"))return spawn(2);
 if(argc==2&&!strcmp(argv[1],"launch-hold"))return spawn(1);
 if(argc==2&&!strcmp(argv[1],"launch-jive"))return spawn(0);
 if(argc==3&&!strcmp(argv[1],"sync-internal"))return internal_sync(argv[2]);
 if(argc==3&&!strcmp(argv[1],"remove-file"))return internal_remove(argv[2]);
 if(argc==3&&!strcmp(argv[1],"sha256-internal"))return internal_sha256(argv[2]);
 if(argc==3&&!strcmp(argv[1],"check-inactive")){
  if(!internal_target(argv[2])||internal_active_target(argv[2])){
   fprintf(stderr,"active or unverifiable executable target: %s\n",argv[2]);return 2;
  }
  return 0;
 }
 if((argc==5||argc==6)&&!strcmp(argv[1],"install-file")&&number(argv[4],&start))return internal_copy(argv[2],argv[3],start,argc==6?argv[5]:NULL);
 if(argc==2&&!strcmp(argv[1],"maintenance-active")){identity owner;return coordinator_owner(&owner)?0:1;}
 if(argc==2&&!strcmp(argv[1],"quiesce-new"))return coordinator_quiesce("restart.new",0,0,1);
 if(argc==2&&!strcmp(argv[1],"recover-old"))return coordinator_quiesce("restart.old",0,0,1);
 if(argc==4&&!strcmp(argv[1],"maintenance-stop")&&number(argv[2],&pid)&&number(argv[3],&start))
  return coordinator_quiesce("restart.old",pid,start,0);
 if(argc==5&&!strcmp(argv[1],"verify-ui")&&number(argv[2],&pid)&&number(argv[3],&start))
  return coordinator_verify_ui(pid,start,argv[4]);
 if(argc==4&&!strcmp(argv[1],"verify-stack")&&number(argv[2],&pid)&&number(argv[3],&start))
  return coordinator_verify_stack(pid,start);
 if(argc==5&&!strcmp(argv[1],"verify-exe")&&number(argv[2],&pid)&&number(argv[3],&start))
  {executable_proof proof;return executable_inspect(pid,start,-1,0,argv[4],0,&proof)?0:2;}
 if(argc==7&&(!strcmp(argv[1],"inspect-exe")||!strcmp(argv[1],"inspect-pin"))&&
    number(argv[2],&pid)&&number(argv[3],&start)){
  uint64_t parent,size;if(!number(argv[4],&parent)||parent>INT_MAX||!number(argv[5],&size))return 2;
  return executable_report(pid,start,(int64_t)parent,size,argv[6],!strcmp(argv[1],"inspect-pin"));
 }
 if(argc==4&&!strcmp(argv[1],"terminate")&&number(argv[2],&pid)&&number(argv[3],&start)&&pid>=2&&pid<=INT_MAX){
  identity i;if(!identify((pid_t)pid,&i)||i.start!=start||!managed(&i)||pid==(uint64_t)getpid())return 2;
  return kill((pid_t)pid,SIGTERM)?2:0;
 }
 uint64_t old,oldstart,next,nextstart;
 if(argc==6&&(!strcmp(argv[1],"pid-swap")||!strcmp(argv[1],"pid-recover"))&&
    number(argv[2],&old)&&number(argv[3],&oldstart)&&number(argv[4],&next)&&number(argv[5],&nextstart))
  return swap_pid(old,oldstart,next,nextstart,!strcmp(argv[1],"pid-recover"));
 return 2;
}
