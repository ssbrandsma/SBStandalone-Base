/* Boundary-only identity proof. No pathname-hash fallback and no persistent
 * PID receipt can substitute for readable kernel-correlated executable bytes. */
#include <sys/sysmacros.h>
typedef struct {
 identity process;
 struct stat file;
 int level,readlink_error,open_error,read_error,stat_error,identity_error,mapping_error,pinned;
 size_t bytes;
 uint64_t elapsed_us;
 char digest[65];
 const char *failure;
} executable_proof;
static int executable_maps(uint64_t pid,executable_proof *proof)
{
 char path[64],line[2048];snprintf(path,sizeof path,"/proc/%llu/maps",(unsigned long long)pid);
 FILE *file=fopen(path,"r");if(!file){proof->mapping_error=errno;proof->failure="maps_open";return 0;}
 unsigned rows=0,bytes=0,matching=0;int ok=1;
 while(fgets(line,sizeof line,file)){
  size_t length=strlen(line);if(++rows>4096||(bytes+=(unsigned)length)>262144||!length||line[length-1]!='\n'){
   proof->failure="maps_bound";ok=0;break;
  }
  unsigned long long first,last,offset,inode;unsigned major_id,minor_id;char permissions[5];int used=0;
  if(sscanf(line,"%llx-%llx %4s %llx %x:%x %llu %n",&first,&last,permissions,&offset,&major_id,&minor_id,&inode,&used)!=7){
   proof->failure="maps_parse";ok=0;break;
  }
  (void)offset;
  char *name=line+used;name[strcspn(name,"\r\n")]=0;
  size_t expected=strlen(proof->process.exe);
  int same_name=!strcmp(name,proof->process.exe)||
   (!strncmp(name,proof->process.exe,expected)&&!strcmp(name+expected," (deleted)"));
  if(permissions[2]!='x'||!same_name)continue;
  if(first>=last||inode!=(unsigned long long)proof->file.st_ino||
     major_id!=major(proof->file.st_dev)||minor_id!=minor(proof->file.st_dev)){
   proof->failure="mapping_inode_mismatch";ok=0;break;
  }
  matching++;
 }
 if(ferror(file)){proof->mapping_error=errno;proof->failure="maps_read";ok=0;}
 fclose(file);
#ifdef SB_MAINTENANCE_TEST
 const char *fault=getenv("SB_TEST_EXE_FAULT");
 if(fault&&!strcmp(fault,"mapping")){proof->failure="mapping_inode_mismatch";return 0;}
#endif
 if(ok&&!matching){proof->failure="executable_mapping_missing";return 0;}
 return ok;
}
static uint64_t executable_clock(void)
{struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static int executable_hash(int fd,executable_proof *proof)
{
 struct stat before,after;
 if(fstat(fd,&before)){proof->stat_error=errno;proof->failure="fstat";return 0;}
 proof->file=before;
 if(!S_ISREG(before.st_mode)||before.st_uid!=geteuid()||(before.st_mode&0022)||
    before.st_size<1||before.st_size>2097152){proof->failure="untrusted_executable_metadata";return 0;}
 if(lseek(fd,0,SEEK_SET)<0){proof->read_error=errno;proof->failure="seek";return 0;}
 sha256_t hash;sha_init(&hash);unsigned char buffer[8192];size_t total=0;
 for(;;){
  ssize_t n;
#ifdef SB_MAINTENANCE_TEST
  const char *fault=getenv("SB_TEST_EXE_FAULT");
  if(fault&&!strcmp(fault,"read")){errno=EIO;n=-1;}else
#endif
  n=read(fd,buffer,sizeof buffer);
  if(n<0&&errno==EINTR)continue;
  if(n<0){proof->read_error=errno;proof->failure="read";return 0;}
  if(!n)break;
  total+=(size_t)n;if(total>2097152){proof->failure="executable_size_bound";return 0;}
  sha_add(&hash,buffer,(size_t)n);
 }
 proof->bytes=total;
 if(fstat(fd,&after)){proof->stat_error=errno;proof->failure="fstat_after";return 0;}
 if(total!=(size_t)before.st_size||!internal_unchanged(&before,&after)){
  proof->failure="executable_changed_during_hash";return 0;
 }
 sha_done(&hash,proof->digest);return 1;
}
static int executable_inspect(uint64_t pid,uint64_t start,int64_t parent,uint64_t size,
 const char *expected,int pin,executable_proof *proof)
{
 memset(proof,0,sizeof *proof);proof->level=3;proof->failure="invalid_expectation";
 uint64_t began=executable_clock();int fd=-1;char path[64];identity after;
 if(pid<2||pid>INT_MAX||!start||strlen(expected)!=64||size>2097152)goto done;
 for(const char *p=expected;*p;p++)if(!strchr("0123456789abcdef",*p))goto done;
 errno=0;
 if(!process_stat((pid_t)pid,&proof->process)||proof->process.state=='Z'||proof->process.state=='X'){
  proof->identity_error=errno;proof->failure="process_stat";goto done;
 }
 if(proof->process.start!=start){proof->failure="start_mismatch";goto done;}
 if(parent>=0&&proof->process.parent!=parent){proof->failure="supervisor_mismatch";goto done;}
 snprintf(path,sizeof path,"/proc/%ld/exe",(long)pid);
 ssize_t n;
#ifdef SB_MAINTENANCE_TEST
 const char *fault=getenv("SB_TEST_EXE_FAULT");
 if(fault&&!strcmp(fault,"readlink")){errno=EACCES;n=-1;}else
#endif
 n=readlink(path,proof->process.exe,sizeof proof->process.exe-1);
 if(n<0){proof->readlink_error=errno;proof->failure="readlink";goto done;}
 if(n>=(ssize_t)sizeof proof->process.exe-1){proof->failure="executable_path_bound";goto done;}
 proof->process.exe[n]=0;
 if(!base_role(&proof->process)){proof->failure="unknown_role";goto done;}
#ifdef SB_MAINTENANCE_TEST
 if(fault&&!strcmp(fault,"open")){errno=ENOENT;fd=-1;}else
#endif
 fd=open(path,O_RDONLY|O_CLOEXEC);
 if(fd<0){proof->open_error=errno;proof->failure="proc_exe_open";goto done;}
 if(!executable_hash(fd,proof))goto done;
 if((size&&(uint64_t)proof->file.st_size!=size)||strcmp(expected,proof->digest)){
  proof->failure="artifact_mismatch";goto done;
 }
 if(pin){
#ifdef SB_MAINTENANCE_TEST
  const char *pause=getenv("SB_TEST_EXE_PAUSE_MS");
  if(pause){uint64_t ms;if(!number(pause,&ms)||ms>2000){proof->failure="test_pause";goto done;}
   puts("PIN_READY");fflush(stdout);(void)poll(NULL,0,(int)ms);}
  else
#endif
  (void)poll(NULL,0,100);
  if(!executable_hash(fd,proof))goto done;
  if(strcmp(expected,proof->digest)){proof->failure="pinned_hash_mismatch";goto done;}
  proof->pinned=1;
 }
 /* This compares the active kernel executable identity with the held FD.
  * Missing correlation is LEVEL 3 even when the pinned bytes remain readable.
  * LEVEL 2 is deliberately not enabled on an unqualified old filesystem. */
 struct stat current;
#ifdef SB_MAINTENANCE_TEST
 if(fault&&!strcmp(fault,"correlation")){errno=ENOENT;n=-1;}else
#endif
 n=stat(path,&current);
 if(n<0){proof->stat_error=errno;proof->failure="live_inode_correlation";goto done;}
 if(current.st_dev!=proof->file.st_dev||current.st_ino!=proof->file.st_ino){
  proof->failure="live_inode_changed";goto done;
 }
 if(!internal_unchanged(&current,&proof->file)){proof->failure="live_file_metadata_changed";goto done;}
 if(!executable_maps(pid,proof))goto done;
 if(!identify((pid_t)pid,&after)||after.start!=start||after.parent!=proof->process.parent){
  proof->failure="process_changed";goto done;
 }
 size_t original_length=strlen(proof->process.exe);
 if(strcmp(after.exe,proof->process.exe)&&
    (strncmp(after.exe,proof->process.exe,original_length)||strcmp(after.exe+original_length," (deleted)"))){
  proof->failure="process_changed";goto done;
 }
 proof->level=1;proof->failure="none";
done:
 if(fd>=0)close(fd);
 proof->elapsed_us=executable_clock()-began;return proof->level==1;
}
static int executable_report(uint64_t pid,uint64_t start,int64_t parent,uint64_t size,const char *sha,int pin)
{
 executable_proof p;int ok=executable_inspect(pid,start,parent,size,sha,pin,&p);
 printf("{\"schema\":\"SB_EXEC_IDENTITY_1\",\"level\":%d,\"allowed\":%s,\"level2_enabled\":false,\"pid\":%llu,\"start\":\"%llu\",\"parent\":%ld,\"role\":%d,\"exe\":",
  p.level,ok?"true":"false",(unsigned long long)pid,(unsigned long long)start,(long)p.process.parent,base_role(&p.process));
 json_string(p.process.exe);printf(",\"failure\":");json_string(p.failure);
 printf(",\"readlink_errno\":%d,\"open_errno\":%d,\"read_errno\":%d,\"stat_errno\":%d,\"identity_errno\":%d,\"mapping_errno\":%d,\"bytes_hashed\":%zu,\"sha256\":",
  p.readlink_error,p.open_error,p.read_error,p.stat_error,p.identity_error,p.mapping_error,p.bytes);json_string(p.digest);
 printf(",\"file_device\":\"%llu\",\"file_inode\":\"%llu\",\"file_size\":%llu,\"file_nlink\":%llu,\"pin_rehashed\":%s,\"elapsed_us\":%llu}\n",
  (unsigned long long)p.file.st_dev,(unsigned long long)p.file.st_ino,(unsigned long long)p.file.st_size,
  (unsigned long long)p.file.st_nlink,p.pinned?"true":"false",(unsigned long long)p.elapsed_us);
 return ok?0:2;
}
