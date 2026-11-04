#ifndef SB_PROCESS_SPAWN_H
#define SB_PROCESS_SPAWN_H
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <limits.h>

/* Query before fork. Child cleanup uses only close/syscall, never allocation
 * or /proc directory iteration after a multithreaded fork. */
static inline int sb_process_fd_limit(unsigned *limit)
{
 struct rlimit bound;if(getrlimit(RLIMIT_NOFILE,&bound))return 0;
 if(bound.rlim_max==RLIM_INFINITY||bound.rlim_max>1048576)return 0;
 *limit=(unsigned)bound.rlim_max;return *limit>=3;
}
static inline void sb_process_close_fds(unsigned limit,int preserve)
{
#ifdef SYS_close_range
 if(preserve<3){if(!syscall(SYS_close_range,3u,UINT_MAX,0u))return;}
 else{
  int first=preserve==3?0:(int)syscall(SYS_close_range,3u,(unsigned)preserve-1,0u);
  int second=(int)syscall(SYS_close_range,(unsigned)preserve+1,UINT_MAX,0u);
  if(!first&&!second)return;
 }
#endif
 /* Original Fab4 kernels lack close_range; their descriptor limit is small. */
 for(unsigned fd=3;fd<limit;fd++)if((int)fd!=preserve)close((int)fd);
}
#endif
