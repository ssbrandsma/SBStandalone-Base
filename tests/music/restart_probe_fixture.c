#define _GNU_SOURCE
#include <sys/sem.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include "../../native/common/process_identity.h"
static int fake_ui(int argc,char **argv)
{
 if(argc!=5)return 2;
 int id=atoi(argv[2]),slot=-1;const char *run=argv[3],*mode=argv[4];
 for(unsigned n=0;n<20;n+=2){
  struct sembuf ops[]={{(unsigned short)n,-1,SEM_UNDO|IPC_NOWAIT},{(unsigned short)(n+1),1,IPC_NOWAIT}};
  if(!semop(id,ops,2)){slot=(int)n;break;}
 }
 if(slot<0)return 2;
 union {int val;} value;value.val=6;if(semctl(id,slot+1,SETVAL,value)<0)return 2;
 identity me;if(!process_stat(getpid(),&me))return 2;
 if(!strcmp(mode,"with-decoder")){
  const char *child=getenv("SB_TEST_ALSA");if(!child)return 2;
  pid_t pid=fork();if(pid<0)return 2;
  if(!pid){execl(child,child,"ui",argv[2],run,"decoder",(char *)NULL);_exit(127);}
 }
 if(!strcmp(mode,"ignore"))signal(SIGTERM,SIG_IGN);
 puts("ready");fflush(stdout);uint64_t sequence=0;
 if(!strcmp(mode,"init-exit")){struct timespec delay={0,200000000};nanosleep(&delay,NULL);return 23;}
 for(;;){
  if(strcmp(mode,"frozen")){
   value.val=3;if(semctl(id,slot+1,SETVAL,value)<0)return 2;
   if(strcmp(mode,"no-health")&&strcmp(mode,"old")&&strcmp(mode,"decoder")){
    char path[640],nonce[128],temporary[680];snprintf(path,sizeof path,"%s/restart.challenge",run);
    if(read_file(path,nonce,sizeof nonce)==65){
     nonce[64]=0;if(!strcmp(mode,"wrong-challenge"))nonce[0]='g';
     snprintf(path,sizeof path,"%s/jive.health",run);snprintf(temporary,sizeof temporary,"%s.tmp",path);
     FILE *f=fopen(temporary,"w");if(!f)return 2;
     struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
     fprintf(f,"SBUI1 %s %ld %llu %llu %llu\n",nonce,(long)me.pid,(unsigned long long)me.start,
      (unsigned long long)((uint64_t)now.tv_sec*1000+now.tv_nsec/1000000),(unsigned long long)++sequence);
     fclose(f);if(rename(temporary,path))return 2;
    }
   }
  }
  struct timespec delay={0,20000000};nanosleep(&delay,NULL);
 }
}
int main(int argc,char **argv)
{
 if(argc>1&&!strcmp(argv[1],"ui"))return fake_ui(argc,argv);
 if(argc==2&&!strcmp(argv[1],"daemon")){puts("ready");fflush(stdout);for(;;)pause();}
 if(argc==3&&(!strcmp(argv[1],"feeder")||!strcmp(argv[1],"reader"))){
  if(open(argv[2],!strcmp(argv[1],"reader")?O_RDONLY:O_WRONLY)<0)return 2;
 }else if(argc==4&&!strcmp(argv[1],"jive")){
  union {int val;} value;value.val=0;
  if(semctl(atoi(argv[2]),0,SETVAL,value)<0)return 2;
  value.val=atoi(argv[3]);if(semctl(atoi(argv[2]),1,SETVAL,value)<0)return 2;
 }else return 2;
 puts("ready");fflush(stdout);
 for(;;)pause();
}
