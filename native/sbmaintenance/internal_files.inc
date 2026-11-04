/* Whitelisted overlay files only. No global sync(), USB path or flash device. */
#include "../common/descriptor_walk.h"
#include "../common/sha256.h"
static const char *internal_stage(void)
{
#ifdef SB_MAINTENANCE_TEST
 const char *value=getenv("SB_TEST_INSTALL_STAGE");if(value)return value;
#endif
 return "/mnt/storage/standalonebase/wp4a2b-backup";
}
static int internal_target(const char *path)
{
 static const char *targets[]={"/etc/init.d/squeezecenter","/etc/init.d/squeezecenter.stock","/etc/init.d/squeezeplay",
  "/usr/share/jive/applets/SqueezeCenter/SqueezeCenterApplet.lua",
  "/usr/share/jive/applets/StandaloneBase/StandaloneBaseApplet.lua",
  "/usr/share/jive/applets/StandaloneBase/LifecycleHeartbeat.lua",
  "/usr/share/jive/applets/StandaloneBase/sbbase",
  "/usr/share/jive/applets/StandaloneBase/sb-tinysc-control",
  "/usr/share/jive/applets/StandaloneBase/sb-maintenance-helper",
  "/usr/share/jive/applets/StandaloneBase/restart-actions.sh",
  "/mnt/storage/standalonebase/tinysc-arbitration.enabled"};
 const char *prefix="";
#ifdef SB_MAINTENANCE_TEST
 const char *test=getenv("SB_TEST_INSTALL_TARGET");if(test)prefix=test;
#endif
 for(unsigned n=0;n<sizeof targets/sizeof targets[0];n++){
  char candidate[1024];if(snprintf(candidate,sizeof candidate,"%s%s",prefix,targets[n])>=(int)sizeof candidate)return 0;
  if(!strcmp(candidate,path))return 1;
 }return 0;
}
static int internal_syntax(const char *path)
{
 if(!path||path[0]!='/'||strlen(path)>=900||strstr(path,"//")||strstr(path,"/../")||
  strstr(path,"/./")||!strcmp(path+strlen(path)-1,"/")||strchr(path,'\\'))return 0;
 const char *leaf=strrchr(path,'/');if(!leaf||!strcmp(leaf,"/..")||!strcmp(leaf,"/."))return 0;
 for(const char *p=path;*p;p++)if((unsigned char)*p<32||(unsigned char)*p==127)return 0;
 return 1;
}
static int internal_staged(const char *path)
{
 size_t n=strlen(internal_stage());return !strncmp(path,internal_stage(),n)&&(!path[n]||path[n]=='/');
}
static int internal_open(const char *path,int directory)
{
 if(!internal_syntax(path))return -1;
#ifndef SB_MAINTENANCE_TEST
 char mounts[65536];
 if(!read_file("/proc/self/mountinfo",mounts,sizeof mounts)||
  !strstr(mounts," / / rw - unionfs none rw,dirs=/mnt/storage=rw:/=ro\n"))return -1;
#endif
 int root=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);struct stat st;
 if(root<0)return -1;
 if(fstat(root,&st)){close(root);return -1;}
 return sb_descriptor_walk(root,path+1,directory,st.st_dev,1,0);
}
static int internal_trusted(int fd,int directory)
{
 struct stat st;return !fstat(fd,&st)&&st.st_uid==geteuid()&&!(st.st_mode&0022)&&
  (directory?S_ISDIR(st.st_mode):S_ISREG(st.st_mode));
}
static int internal_parent(const char *path,char leaf[256])
{
 if(!internal_syntax(path))return -1;
 char parent[1024];strcpy(parent,path);char *slash=strrchr(parent,'/');
 if(!slash||slash==parent||strlen(slash+1)>=256)return -1;
 strcpy(leaf,slash+1);*slash=0;int fd=internal_open(parent,1);
 if(fd>=0&&!internal_trusted(fd,1)){close(fd);fd=-1;}return fd;
}
static int internal_sync(const char *path)
{
 if(!internal_syntax(path))return 2;
 int allowed=internal_staged(path)||internal_target(path);
 if(!allowed){
  const char *parents[]={"/etc/init.d","/usr/share/jive/applets/StandaloneBase",
   "/usr/share/jive/applets/SqueezeCenter","/mnt/storage/standalonebase"};
  for(unsigned n=0;n<sizeof parents/sizeof parents[0];n++)if(!strcmp(path,parents[n]))allowed=1;
 }
#ifdef SB_MAINTENANCE_TEST
 const char *test=getenv("SB_TEST_INSTALL_TARGET");
 if(test&&!strncmp(path,test,strlen(test))&&path[strlen(test)]=='/')allowed=1;
#endif
 if(!allowed)return 2;
 int fd=internal_open(path,1),directory=1;
 if(fd<0){fd=internal_open(path,0);directory=0;}
 if(fd<0)return 2;
 int ok=internal_trusted(fd,directory)&&!fsync(fd);close(fd);return ok?0:2;
}
static int internal_digest_fd(int fd,char digest[65])
{
 struct stat before,after;if(!internal_trusted(fd,0)||fstat(fd,&before)||before.st_size<0||before.st_size>2097152)return 0;
 sha256_t state;sha_init(&state);size_t total=0;unsigned char buffer[8192];
 for(;;){
  ssize_t bytes=read(fd,buffer,sizeof buffer);if(bytes<0&&errno==EINTR)continue;
  if(bytes<0)return 0;
  if(!bytes)break;
  total+=(size_t)bytes;if(total>2097152)return 0;sha_add(&state,buffer,(size_t)bytes);
 }
 if(fstat(fd,&after)||total!=(size_t)before.st_size||before.st_size!=after.st_size||
  before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec)return 0;
 sha_done(&state,digest);return 1;
}
static int internal_unchanged(const struct stat *a,const struct stat *b)
{
 return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mode==b->st_mode&&
  a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&
  a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int internal_copy(const char *source,const char *target,uint64_t mode,const char *expected)
{
 if(mode>0777||!internal_syntax(source)||!internal_staged(source)||!internal_target(target))return 2;
 int in=internal_open(source,0);if(in<0)return 2;
 struct stat original;if(!internal_trusted(in,0)||fstat(in,&original)||original.st_size<0||original.st_size>2097152){close(in);return 2;}
 char leaf[256];int parent=internal_parent(target,leaf);if(parent<0){close(in);return 2;}
 /* Refuse unsupported directory durability before modifying a target. */
 if(fsync(parent)){close(parent);close(in);return 2;}
 struct stat current;int exists=fstatat(parent,leaf,&current,AT_SYMLINK_NOFOLLOW)==0;
 if((exists&&(!S_ISREG(current.st_mode)||current.st_uid!=geteuid()||(current.st_mode&0022)))||(!exists&&errno!=ENOENT)){
  close(parent);close(in);return 2;
 }
 if(expected){
  int match=0;
  if(!strcmp(expected,"absent"))match=!exists;
  else if(exists&&strlen(expected)==64){
   int fd=openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);char digest[65];
   if(fd>=0){match=internal_digest_fd(fd,digest)&&!strcmp(expected,digest);close(fd);}
  }
  if(!match){close(parent);close(in);return 2;}
 }
 char temporary[1024];snprintf(temporary,sizeof temporary,"/proc/self/fd/%d/%s.wp4a2b-XXXXXX",parent,leaf);
 int out=mkstemp(temporary);if(out<0){close(parent);close(in);return 2;}
 const char *name=strrchr(temporary,'/')+1;int ok=fcntl(out,F_SETFD,FD_CLOEXEC)>=0;size_t total=0;
 unsigned char buffer[8192];
 while(ok){
  ssize_t bytes=read(in,buffer,sizeof buffer);if(bytes<0&&errno==EINTR)continue;
  if(bytes<0){ok=0;break;}if(!bytes)break;
  total+=(size_t)bytes;if(total>2097152){ok=0;break;}
  size_t done=0;while(done<(size_t)bytes){
   ssize_t written=write(out,buffer+done,(size_t)bytes-done);if(written<0&&errno==EINTR)continue;
   if(written<=0){ok=0;break;}done+=(size_t)written;
  }
 }
 struct stat after;if(fstat(in,&after)||after.st_size!=original.st_size||after.st_mtime!=original.st_mtime||total!=(size_t)original.st_size)ok=0;
 struct timespec times[]={original.st_atim,original.st_mtim};
 if(ok&&(fchown(out,original.st_uid,original.st_gid)||fchmod(out,(mode_t)mode)||futimens(out,times)||fsync(out)))ok=0;
 close(out);close(in);
 struct stat latest;int now_exists=fstatat(parent,leaf,&latest,AT_SYMLINK_NOFOLLOW)==0;
 if(now_exists!=exists||(exists&&!internal_unchanged(&current,&latest))||(!now_exists&&errno!=ENOENT))ok=0;
#ifdef SB_MAINTENANCE_TEST
 const char *failure=getenv("SB_TEST_INSTALL_FAIL");if(failure&&!strcmp(failure,"before-rename"))ok=0;
#endif
 if(ok&&renameat(parent,name,parent,leaf))ok=0;
#ifdef SB_MAINTENANCE_TEST
 if(failure&&!strcmp(failure,"after-rename"))ok=0;
#endif
 if(!ok)unlinkat(parent,name,0);
 if(ok&&fsync(parent))ok=0;
 close(parent);return ok?0:2;
}
static int internal_remove(const char *target)
{
 if(!internal_target(target))return 2;
 char leaf[256];int parent=internal_parent(target,leaf);if(parent<0)return 2;
 struct stat st;
 if(fstatat(parent,leaf,&st,AT_SYMLINK_NOFOLLOW)){
  int absent=errno==ENOENT;close(parent);return absent?0:2;
 }
 if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0022)||fsync(parent)){
  close(parent);return 2;
 }
 int ok=!unlinkat(parent,leaf,0)&&!fsync(parent);close(parent);return ok?0:2;
}
static int internal_audit(const char *path)
{
 const char *paths[]={"/etc/watchdog.conf","/usr/sbin/watchdog","/etc/squeezeos.version",
  "/mnt/storage/standalonebase/config.json","/usr/share/jive/applets/StandaloneBase/StandaloneBaseMeta.lua",
  "/usr/share/jive/applets/StandaloneBase/sbproxy","/usr/share/jive/applets/StandaloneBase/sbwebserver",
  "/usr/share/jive/applets/StandaloneBase/sb-storage-helper",
  "/usr/share/jive/applets/SqueezeCenter/SqueezeCenterMeta.lua",
  "/usr/share/jive/applets/StandaloneRadio/StandaloneRadioMeta.lua",
  "/usr/share/jive/applets/SpotifyConnect/SpotifyConnectMeta.lua",
  "/usr/share/jive/applets/QobuzConnect/QobuzConnectMeta.lua"};
 for(unsigned n=0;n<sizeof paths/sizeof paths[0];n++)if(!strcmp(path,paths[n]))return 1;
 return 0;
}
static int internal_sha256(const char *path)
{
 if(!internal_staged(path)&&!internal_target(path)&&!internal_audit(path))return 2;
 int fd=internal_open(path,0);if(fd<0)return 2;
 char digest[65];int ok=internal_digest_fd(fd,digest);close(fd);if(!ok)return 2;
 puts(digest);return 0;
}
