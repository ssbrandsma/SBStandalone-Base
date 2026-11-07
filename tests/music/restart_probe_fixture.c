#define _GNU_SOURCE
#include <sys/sem.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../../native/common/process_identity.h"
static int fake_ui(int argc,char **argv)
{
 if(argc!=5)return 2;
 int id=atoi(argv[2]),slots[2],allocated=0;const char *run=argv[3],*mode=argv[4];
 for(unsigned n=0;n<20&&allocated<2;n+=2){
  struct sembuf ops[]={{(unsigned short)n,-1,SEM_UNDO|IPC_NOWAIT},{(unsigned short)(n+1),1,IPC_NOWAIT}};
  if(!semop(id,ops,2))slots[allocated++]=(int)n;
 }
 if(allocated!=2)return 2;
 union {int val;} value;value.val=6;
 for(int i=0;i<2;i++)if(semctl(id,slots[i]+1,SETVAL,value)<0)return 2;
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
   value.val=3;for(int i=0;i<2;i++)if(semctl(id,slots[i]+1,SETVAL,value)<0)return 2;
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
 if(argc==4&&!strcmp(argv[1],"listen")){
  char *save=NULL;
  for(char *p=strtok_r(argv[2],",",&save);p;p=strtok_r(NULL,",",&save)){
   int fd=socket(AF_INET,SOCK_STREAM,0),one=1;
   struct sockaddr_in address={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK),.sin_port=htons((unsigned short)atoi(p))};
   if(fd<0||setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one)||bind(fd,(void *)&address,sizeof address)||listen(fd,2))return 2;
  }
  if(atoi(argv[3])){
   int fd=socket(AF_INET,SOCK_DGRAM,0);
   struct sockaddr_in address={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK),.sin_port=htons((unsigned short)atoi(argv[3]))};
   if(fd<0||bind(fd,(void *)&address,sizeof address))return 2;
  }
  puts("ready");fflush(stdout);for(;;)pause();
 }
 if(argc==6&&!strcmp(argv[1],"session")){
  char port[16];snprintf(port,sizeof port,"%d",atoi(argv[4]));
  for(int n=0;n<2;n++){
   pid_t child=fork();if(child<0)return 2;
   if(!child){execl(argv[2+n],argv[2+n],"listen",argv[4+n],n?"0":port,(char *)NULL);_exit(127);}
  }
  puts("ready");fflush(stdout);for(;;)pause();
 }
 if(argc>1&&!strcmp(argv[1],"ui"))return fake_ui(argc,argv);
 if(argc==2&&!strcmp(argv[1],"daemon")){puts("ready");fflush(stdout);for(;;)pause();}
 if(argc==3&&(!strcmp(argv[1],"feeder")||!strcmp(argv[1],"reader"))){
  if(open(argv[2],!strcmp(argv[1],"reader")?O_RDONLY:O_WRONLY)<0)return 2;
 }else if((argc==4||argc==5)&&!strcmp(argv[1],"jive")){
  int registrations=argc==5?atoi(argv[4]):2;
  if(registrations<1||registrations>10)return 2;
  union {int val;} value;
  for(int i=0;i<registrations;i++){
   value.val=0;if(semctl(atoi(argv[2]),2*i,SETVAL,value)<0)return 2;
   value.val=atoi(argv[3]);if(semctl(atoi(argv[2]),2*i+1,SETVAL,value)<0)return 2;
  }
 }else return 2;
 puts("ready");fflush(stdout);
 for(;;)pause();
}
