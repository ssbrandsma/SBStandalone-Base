/* Only hashes of explicitly remembered browser credentials reach disk. */
static int remembered_store_present;
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
static int session_directory(int create)
{
 const char *dir=getenv("SBWEBSERVER_CONFIG_DIR");
 if(!dir||!*dir)dir="/mnt/storage/sbwebserver";
 if(create&&mkdir(dir,0700)&&errno!=EEXIST)return -1;
 /* The configured directory is trusted launcher input and can be the
  * firmware's managed /mnt/storage/standalonebase -> /etc alias. Resolve
  * it once, then validate/pin the real directory. Credential files below
  * it still never follow symlinks. */
 char resolved[PATH_MAX];if(!realpath(dir,resolved))return -1;
 int fd=open(resolved,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
 if(fd>=0&&(fstat(fd,&st)||st.st_uid!=geteuid()||(st.st_mode&0022))){close(fd);fd=-1;}
 return fd;
}
static int save_sessions(void)
{
 int dir=session_directory(1);if(dir<0)return 0;
 unsigned char random[8];char temporary[64];
 if(secure_random(random,sizeof random)){close(dir);return 0;}
 snprintf(temporary,sizeof temporary,".remembered-%02x%02x%02x%02x%02x%02x%02x%02x.tmp",
 random[0],random[1],random[2],random[3],random[4],random[5],random[6],random[7]);
 int fd=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0){close(dir);return 0;}
 char data[16+SESSION_MAX*65];size_t used=0;
 memcpy(data,"SBREMEMBER1\n",12);used=12;
 for(int i=0;i<SESSION_MAX;i++)if(A.sessions[i].remembered){
  for(int j=0;j<32;j++){snprintf(data+used,3,"%02x",A.sessions[i].hash[j]);used+=2;}
  data[used++]='\n';
 }
 int ok=1;size_t at=0;
 while(at<used){ssize_t n=write(fd,data+at,used-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}at+=(size_t)n;}
 if(ok)ok=!fsync(fd);
 if(close(fd))ok=0;
 if(ok)ok=!renameat(dir,temporary,dir,"remembered-browsers.conf");
 if(ok){remembered_store_present=1;ok=!fsync(dir);}
 if(!ok)unlinkat(dir,temporary,0);
 close(dir);memset(data,0,sizeof data);return ok;
}
static void load_sessions(void)
{
 remembered_store_present=0;
 int dir=session_directory(0);if(dir<0)return;
 struct stat entry;if(!fstatat(dir,"remembered-browsers.conf",&entry,AT_SYMLINK_NOFOLLOW))remembered_store_present=1;
 int fd=openat(dir,"remembered-browsers.conf",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);close(dir);
 if(fd<0)return;
 struct stat st;char data[16+SESSION_MAX*65];size_t got=0;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&
 st.st_uid==geteuid()&&!(st.st_mode&0077)&&st.st_nlink==1&&st.st_size>=12&&st.st_size<=12+SESSION_MAX*65;
 while(ok&&got<(size_t)st.st_size){ssize_t n=read(fd,data+got,(size_t)st.st_size-got);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}got+=(size_t)n;}
 close(fd);
 if(!ok||got<12||memcmp(data,"SBREMEMBER1\n",12)||(got-12)%65){memset(data,0,sizeof data);return;}
 Session loaded[SESSION_MAX]={0};unsigned count=(unsigned)(got-12)/65;
 for(unsigned i=0;i<count&&ok;i++){
  const char *line=data+12+i*65;if(line[64]!='\n'){ok=0;break;}
  for(int j=0;j<32;j++){int a=hexval(line[2*j]),b=hexval(line[2*j+1]);if(a<0||b<0){ok=0;break;}loaded[i].hash[j]=(unsigned char)(a*16+b);}
  loaded[i].remembered=1;loaded[i].serial=i+1;
 }
 if(ok){memcpy(A.sessions,loaded,sizeof loaded);A.serial=count;}
 memset(loaded,0,sizeof loaded);memset(data,0,sizeof data);
}
#else
static int save_sessions(void){return 0;}
static void load_sessions(void){}
#endif
