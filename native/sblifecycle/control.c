#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tinysc.h"
#include "../common/process_spawn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *option(const char *name,const char *value)
{
#ifdef SB_TINYSC_TEST
 const char *test=getenv(name);if(test)return test;
#else
 (void)name;
#endif
 return value;
}
int main(int argc,char **argv)
{
 unsigned limit;if(!sb_process_fd_limit(&limit))return 77;sb_process_close_fds(limit,-1);
 if(argc==2&&!strcmp(argv[1],"--version")){puts("SBTS-WP4A2B-1");return 0;}
#ifndef SB_TINYSC_TEST
 if(geteuid()!=0)return 77;
#endif
 sb_ts_options options={0};
 options.run_dir=option("SB_TS_TEST_RUN","/var/run/standalonebase");
 options.policy_path=option("SB_TS_TEST_POLICY","/mnt/storage/standalonebase/tinysc-arbitration.enabled");
 options.original_service=option("SB_TS_TEST_SERVICE","/etc/init.d/squeezecenter.stock");
 if(argc==2&&!strcmp(argv[1],"release"))return sb_ts_release(&options);
 if(argc==3&&!strcmp(argv[1],"service")){
  static const char *actions[]={"start","stop","restart","rescan","startserver","stopserver","startscan","stopscan","startresizer","stopresizer","status"};
  for(unsigned n=0;n<sizeof actions/sizeof actions[0];n++)
   if(!strcmp(argv[2],actions[n]))return sb_ts_service(&options,argv[2]);
  return 64;
 }
 if(argc==2&&!strcmp(argv[1],"detect")){
  sb_ts_scan scan;if(!sb_ts_detect(&scan))return 77;
  printf("count=%u uncertain=%u\n",scan.count,scan.uncertain);
  for(unsigned n=0;n<scan.count;n++){
   sb_ts_process *p=&scan.rows[n];
   printf("pid=%ld start=%llu role=%u state=%c parent=%ld\n",(long)p->pid,(unsigned long long)p->start,p->role,p->state,(long)p->parent);
  }
  return scan.uncertain?77:0;
 }
 fputs("usage: sb-tinysc-control service ACTION | detect | release\n",stderr);return 64;
}
