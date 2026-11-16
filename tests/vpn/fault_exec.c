#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* Test-only fault injection into a real firewall subprocess, never shipped. */
int execv(const char *path,char *const argv[]){
 const char *file=getenv("SB_VPN_FAULT_FILE");char mode[64]={0};if(file){int fd=open(file,O_RDONLY);if(fd>=0){ssize_t n=read(fd,mode,sizeof mode-1);close(fd);if(n>0){int six=strstr(path,"ip6tables")!=NULL;int match=(!strcmp(mode,"6append")&&six&&argv[1]&&!strcmp(argv[1],"-A"))||(!strcmp(mode,"4insert")&&!six&&strstr(path,"iptables")&&argv[1]&&!strcmp(argv[1],"-I"));if(match){unlink(file);errno=ENOMEM;return -1;}}}}
 int (*real)(const char *,char *const[])=(int (*)(const char *,char *const[]))dlsym(RTLD_NEXT,"execv");return real(path,argv);
}
