#ifndef SB_DESCRIPTOR_WALK_H
#define SB_DESCRIPTOR_WALK_H
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
/* WP6a descriptor walk, shared with the internal-file maintenance installer.
 * Caller validates canonical relative syntax. Consumes fd on every path. */
static inline int sb_descriptor_walk(int fd,const char *relative,int directory,dev_t expected,int enforce,int noatime)
{
 char path[1024];
 if(!relative||strlen(relative)>=sizeof path){close(fd);errno=ENAMETOOLONG;return -1;}
 snprintf(path,sizeof path,"%s",relative);char *save=NULL,*p=strtok_r(path,"/",&save);
 while(p){char *next=strtok_r(NULL,"/",&save);
  int flags=O_RDONLY|O_NOFOLLOW|O_NONBLOCK|(noatime?O_NOATIME:0);
  if(next||directory)flags|=O_DIRECTORY;
  int child=openat(fd,p,flags);int error=errno;close(fd);errno=error;if(child<0)return -1;
  struct stat st;if(fcntl(child,F_SETFD,FD_CLOEXEC)<0||fstat(child,&st)||
   (enforce&&st.st_dev!=expected)||(!next&&!directory&&!S_ISREG(st.st_mode))){close(child);errno=EPERM;return -1;}
  fd=child;p=next;
 }return fd;
}
#endif
