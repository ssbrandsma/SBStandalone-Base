#ifndef SB_PROCESS_IDENTITY_H
#define SB_PROCESS_IDENTITY_H
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
typedef struct { pid_t pid,parent; uint64_t start; char state,name[64],exe[512],args[1024]; } identity;
static inline int read_file(const char *path,char *out,size_t cap)
{
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 ssize_t n=read(fd,out,cap-1);int extra=0;
 if(n>=0&&(size_t)n==cap-1){char c;extra=read(fd,&c,1)!=0;}
 close(fd);if(n<0||extra)return 0;out[n]=0;return (int)n;
}
static inline int number(const char *s,uint64_t *n)
{
 if(!s||!*s)return 0;
 uint64_t v=0;
 for(;*s;s++){unsigned d=(unsigned)(*s-'0');if(d>9||v>(UINT64_MAX-d)/10)return 0;v=v*10+d;}
 *n=v;return 1;
}
static inline int process_stat(pid_t pid,identity *out)
{
 char path[128],buffer[4096];memset(out,0,sizeof *out);out->pid=pid;
 snprintf(path,sizeof path,"/proc/%ld/stat",(long)pid);
 if(!read_file(path,buffer,sizeof buffer))return 0;
 char *p=strrchr(buffer,')'),*begin=strchr(buffer,'(');
 if(!p||!begin||p<=begin||p[1]!=' ')return 0;
 size_t length=(size_t)(p-begin-1);if(length>=sizeof out->name)length=sizeof out->name-1;
 memcpy(out->name,begin+1,length);out->name[length]=0;p+=2;
 unsigned field=3;char *save=NULL;
 for(char *v=strtok_r(p," ",&save);v;v=strtok_r(NULL," ",&save),field++){
  if(field==3)out->state=v[0];
  if(field==4){uint64_t parent;if(!number(v,&parent)||parent>INT_MAX)return 0;out->parent=(pid_t)parent;}
  if(field==22){if(!number(v,&out->start))return 0;break;}
 }
 return out->start!=0;
}
static inline int identify(pid_t pid,identity *out)
{
 char path[128];
 if(!process_stat(pid,out)||out->state=='Z'||out->state=='X')return 0;
 snprintf(path,sizeof path,"/proc/%ld/exe",(long)pid);
 ssize_t n=readlink(path,out->exe,sizeof out->exe-1);if(n<0||n>=(ssize_t)sizeof out->exe-1)return 0;out->exe[n]=0;
 snprintf(path,sizeof path,"/proc/%ld/cmdline",(long)pid);
 if(!read_file(path,out->args,sizeof out->args))return 0;
 struct stat st;snprintf(path,sizeof path,"/proc/%ld",(long)pid);
 return !stat(path,&st)&&st.st_uid==geteuid();
}
static inline int same(pid_t pid,uint64_t start)
{identity i;return identify(pid,&i)&&i.start==start;}

#endif
