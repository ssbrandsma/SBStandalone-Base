#define _GNU_SOURCE
#include "../../native/sblifecycle/tinysc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>

int main(int argc,char **argv)
{
 if(argc>1&&!strcmp(argv[1],"worker")){
  sb_ts_options o={.enabled=1,.poll_ms=50,.stop_timeout_ms=500,
   .run_dir=getenv("SB_TS_TEST_RUN"),.policy_path=getenv("SB_TS_TEST_POLICY"),
   .original_service=getenv("SB_TS_TEST_SERVICE"),.control_path=getenv("SB_TS_TEST_CONTROL")};
  sb_tinysc *a=sb_ts_start(&o);if(!a)return 77;
  setvbuf(stdout,NULL,_IOLBF,0);
  struct pollfd fd[2]={{STDIN_FILENO,POLLIN,0},{sb_ts_fd(a),POLLIN,0}};
  for(;;){
   if(poll(fd,2,100)<0)break;
   if(fd[0].revents&(POLLIN|POLLHUP)){
    char command;ssize_t n=read(0,&command,1);if(n<=0||command=='q')break;
    if(command=='o')sb_ts_operational(a,1);
   }
   sb_ts_drain(a);char json[1024];if(sb_ts_json(a,json,sizeof json))puts(json);
  }
  sb_ts_close(a);return 0;
 }
 if(argc>2&&!strcmp(argv[1],"child")){
  pid_t child=fork();if(child<0)return 77;
  if(!child){execl(argv[2],argv[2],"unrelated",(char *)NULL);_exit(127);}
  printf("%ld\n",(long)child);fflush(stdout);
 }
 if(argc>1&&!strcmp(argv[1],"ignore"))signal(SIGTERM,SIG_IGN);
 for(;;)pause(); /* Host-only fake daemon, never installed on a device. */
}
