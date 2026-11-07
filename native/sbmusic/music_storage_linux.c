#define _GNU_SOURCE
#include "music_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <dirent.h>
#include <pthread.h>
#include <ctype.h>
#include "../common/descriptor_walk.h"

static int decode(const char *in,char *out,size_t cap)
{
 size_t n=0;
 for(size_t i=0;in[i];i++){
  unsigned char c=(unsigned char)in[i];
  if(c=='\\'){
   if(!in[i+1]||!in[i+2]||!in[i+3])return 0;
   if(!strncmp(in+i,"\\040",4))c=' ';
   else if(!strncmp(in+i,"\\011",4))c='\t';
   else if(!strncmp(in+i,"\\012",4))c='\n';
   else if(!strncmp(in+i,"\\134",4))c='\\';
   else return 0;
   i+=3;
  }
  if(n+1>=cap)return 0;
  out[n++]=(char)c;
 }out[n]=0;return 1;
}
static int option(const char *options,const char *key)
{
 size_t n=strlen(key);
 for(const char *p=options;*p;){const char *end=strchr(p,',');size_t len=end?(size_t)(end-p):strlen(p);
  if(len==n&&!memcmp(p,key,n))return 1;
  if(!end)break;
 p=end+1;
 }return 0;
}
sb_storage_status sb_storage_parse(const char *input,size_t bytes,int info,sb_storage_mounts *out)
{
 if(!input||!out||bytes>1024*1024||memchr(input,0,bytes))return SB_STORAGE_INVALID;
 memset(out,0,sizeof *out);out->has_mount_ids=!!info;
 size_t pos=0;
 while(pos<bytes){
  size_t end=pos;while(end<bytes&&input[end]!='\n')end++;
  if(end-pos>=8192||end==bytes)return SB_STORAGE_INVALID;
  char line[8192];memcpy(line,input+pos,end-pos);line[end-pos]=0;pos=end+1;
  if(!line[0])continue;
  char *fields[80],*save=NULL;unsigned n=0;
  for(char *p=strtok_r(line," ",&save);p;p=strtok_r(NULL," ",&save)){
   if(n==80)return SB_STORAGE_LIMIT;
 fields[n++]=p;
  }
  if(out->count==SB_STORAGE_MOUNTS_MAX)return SB_STORAGE_LIMIT;
  sb_storage_mount *m=&out->rows[out->count];char *dev,*path,*fs,*opts,*root="/",*super="";
  if(info){
   unsigned sep=6;while(sep<n&&strcmp(fields[sep],"-"))sep++;
   /* Some kernel filesystem drivers emit literal spaces in super options
    * (WSL 9p is one). Only the first options token is needed for ro; mount
    * identity fields before it still have strict positions/decoding. */
   if(n<10||sep+4>n)return SB_STORAGE_INVALID;
   char tail;unsigned long long id;
   if(sscanf(fields[0],"%llu%c",&id,&tail)!=1||!id||sscanf(fields[2],"%u:%u%c",&m->major,&m->minor,&tail)!=2)return SB_STORAGE_INVALID;
   m->mount_id=id;root=fields[3];path=fields[4];opts=fields[5];fs=fields[sep+1];dev=fields[sep+2];super=fields[sep+3];
  }else{
   if(n!=6)return SB_STORAGE_INVALID;
 dev=fields[0];path=fields[1];fs=fields[2];opts=fields[3];
  }
  if(!decode(dev,m->device,sizeof m->device)||!decode(path,m->path,sizeof m->path)||!decode(root,m->root,sizeof m->root)||!decode(fs,m->fs,sizeof m->fs))return SB_STORAGE_LIMIT;
  if(m->path[0]!='/'||m->root[0]!='/')return SB_STORAGE_INVALID;
  m->read_only=option(opts,"ro")||option(super,"ro");out->count++;
 }return SB_STORAGE_OK;
}
sb_storage_status sb_storage_read_mounts(void *unused,sb_storage_mounts *out)
{
 (void)unused;
 FILE *f=fopen("/proc/self/mountinfo","r");int info=1;
 if(!f){info=0;f=fopen("/proc/mounts","r");}if(!f)return SB_STORAGE_IO;
 char *buffer=malloc(1024*1024+1);if(!buffer){fclose(f);return SB_STORAGE_LIMIT;}
 size_t n=fread(buffer,1,1024*1024+1,f);int error=ferror(f);fclose(f);
 sb_storage_status rc=error?SB_STORAGE_IO:sb_storage_parse(buffer,n,info,out);free(buffer);return rc;
}
static int canonical(const char *path,int absolute)
{
 if(!path||!path[0]||strlen(path)>=SB_STORAGE_PATH||!!(path[0]=='/')!=!!absolute)return 0;
 const char *p=path+absolute;if(!*p)return absolute;
 while(*p){const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);
  if(!n||(n==1&&p[0]=='.')||(n==2&&p[0]=='.'&&p[1]=='.'))return 0;
  for(size_t i=0;i<n;i++)if((unsigned char)p[i]<32||p[i]=='\\'||(unsigned char)p[i]==127)return 0;
  if(!end)break;
 p=end+1;if(!*p)return 0;
 }return 1;
}
static int beneath(const char *parent,const char *path)
{size_t n=strlen(parent);return !strncmp(parent,path,n)&&path[n]=='/'&&path[n+1];}
/* openat/fstatat arrived before 2.6.26. No openat2/statx/O_PATH dependency. */
static int walk(int fd,const char *relative,int directory,dev_t expected,int enforce,int noatime)
{
 return sb_descriptor_walk(fd,relative,directory,expected,enforce,noatime);
}
static int same_mount(const sb_storage_mount *a,const sb_storage_mount *b)
{return a->mount_id==b->mount_id&&a->major==b->major&&a->minor==b->minor&&a->read_only==b->read_only&&
 !strcmp(a->root,b->root)&&!strcmp(a->path,b->path)&&!strcmp(a->device,b->device)&&!strcmp(a->fs,b->fs);}
static sb_storage_status present(const sb_storage_root *r,sb_storage_mount_reader read,void *context,const char *relative)
{
 sb_storage_mounts *table=malloc(sizeof *table);if(!table)return SB_STORAGE_LIMIT;
 sb_storage_status rc=read(context,table);unsigned matches=0;
 if(rc==SB_STORAGE_OK&&!table->has_mount_ids)rc=SB_STORAGE_UNSUPPORTED;
 if(rc==SB_STORAGE_OK)for(unsigned i=0;i<table->count;i++){
  const sb_storage_mount *m=&table->rows[i];
  if(!strcmp(m->path,r->mount.path)){matches++;if(!same_mount(m,&r->mount))rc=SB_STORAGE_STALE;}
  /* Disallow traversal into nested mounts, including same-device bind mounts. */
  if(relative&&beneath(r->mount.path,m->path)){
   const char *sub=m->path+strlen(r->mount.path)+1;size_t n=strlen(sub);
   if(!strncmp(relative,sub,n)&&(!relative[n]||relative[n]=='/'))rc=SB_STORAGE_INVALID;
  }
 }
 if(rc==SB_STORAGE_OK&&matches!=1)rc=SB_STORAGE_NOT_MOUNTED;
 free(table);return rc;
}
static int supported(const char *fs)
{return !strcmp(fs,"vfat")||!strcmp(fs,"ext2")||!strcmp(fs,"ext3")||!strcmp(fs,"ntfs")||!strcmp(fs,"fuseblk")||!strcmp(fs,"tmpfs");}
sb_storage_status sb_storage_validate(const char *parent,const sb_storage_mount *mount,const sb_music_media *media,
 sb_storage_mount_reader read,void *context,sb_storage_root *out)
{
 if(!out)return SB_STORAGE_INVALID;
 memset(out,0,sizeof *out);out->fd=-1;
 if(!mount||!media||!memchr(mount->path,0,sizeof mount->path)||!memchr(mount->root,0,sizeof mount->root)||
 !memchr(mount->device,0,sizeof mount->device)||!memchr(mount->fs,0,sizeof mount->fs)||
 !sb_music_media_current(media,media)||!canonical(parent,1)||!canonical(mount->path,1)||
 !beneath(parent,mount->path)||strcmp(mount->root,"/")||!mount->mount_id)return SB_STORAGE_INVALID;
 /* Explicit volatile roots are useful for host fixtures/future bounded RAM
  * policy, but the Fab4 adapter below never advertises these as USB/SD. */
 if(!supported(mount->fs))return SB_STORAGE_UNSUPPORTED;
 if(strncmp(mount->device,"/dev/",5)&&strcmp(mount->fs,"tmpfs"))return SB_STORAGE_INVALID;
 if(!read)read=sb_storage_read_mounts;
 out->media=*media;out->mount=*mount;
 sb_storage_status rc=present(out,read,context,NULL);if(rc!=SB_STORAGE_OK)return rc;
 int fd=open("/",O_RDONLY|O_DIRECTORY);if(fd<0)return SB_STORAGE_IO;
 fd=walk(fd,mount->path+1,1,0,0,0);if(fd<0)return SB_STORAGE_IO;
 struct stat st;if(fstat(fd,&st)||major(st.st_dev)!=mount->major||minor(st.st_dev)!=mount->minor){close(fd);return SB_STORAGE_STALE;}
 out->fd=fd;out->device=st.st_dev;out->inode=st.st_ino;
 rc=sb_storage_root_check(out,media,read,context);if(rc!=SB_STORAGE_OK)sb_storage_root_close(out);
 return rc;
}
sb_storage_status sb_storage_root_check(const sb_storage_root *r,const sb_music_media *current,sb_storage_mount_reader read,void *context)
{
 if(!r||r->fd<0||!current||!sb_music_media_current(&r->media,current))return SB_STORAGE_STALE;
 if(!read)read=sb_storage_read_mounts;
 sb_storage_status rc=present(r,read,context,r->relative[0]?r->relative:NULL);if(rc!=SB_STORAGE_OK)return rc;
 if(read==sb_storage_read_mounts&&strcmp(r->mount.fs,"tmpfs")){
  struct stat node;
  if(stat(r->mount.device,&node)||!S_ISBLK(node.st_mode))return SB_STORAGE_IO;
  if(strcmp(r->mount.fs,"fuseblk")&&(major(node.st_rdev)!=r->mount.major||minor(node.st_rdev)!=r->mount.minor))return SB_STORAGE_STALE;
 }
 struct stat a,b;if(fstat(r->fd,&a)||a.st_dev!=r->device||a.st_ino!=r->inode)return SB_STORAGE_IO;
 int fd=open("/",O_RDONLY|O_DIRECTORY);if(fd<0)return SB_STORAGE_IO;
 fd=walk(fd,r->mount.path+1,1,0,0,0);if(fd<0)return SB_STORAGE_IO;
 if(r->relative[0]){fd=walk(fd,r->relative,1,r->device,1,!!(fcntl(r->fd,F_GETFL)&O_NOATIME));if(fd<0)return SB_STORAGE_IO;}
 int good=!fstat(fd,&b)&&b.st_dev==a.st_dev&&b.st_ino==a.st_ino;close(fd);
 return good?SB_STORAGE_OK:SB_STORAGE_STALE;
}
sb_storage_status sb_storage_open_relative(const sb_storage_root *r,const sb_music_media *current,const char *relative,int directory,
 sb_storage_mount_reader read,void *context,int *out)
{
 if(!out)return SB_STORAGE_INVALID;
 *out=-1;
 if(!canonical(relative,0))return SB_STORAGE_INVALID;
 if(!read)read=sb_storage_read_mounts;
 sb_storage_status rc=sb_storage_root_check(r,current,read,context);if(rc!=SB_STORAGE_OK)return rc;
 char scoped[SB_STORAGE_PATH];
 if(snprintf(scoped,sizeof scoped,"%s%s%s",r->relative,r->relative[0]?"/":"",relative)>=(int)sizeof scoped)return SB_STORAGE_LIMIT;
 rc=present(r,read,context,scoped);if(rc!=SB_STORAGE_OK)return rc;
 int base=dup(r->fd);if(base<0)return SB_STORAGE_IO;
 int fd=walk(base,relative,directory,r->device,1,!!(fcntl(r->fd,F_GETFL)&O_NOATIME));if(fd<0)return SB_STORAGE_IO;
 rc=sb_storage_root_check(r,current,read,context);
 if(rc==SB_STORAGE_OK)rc=present(r,read,context,scoped);
 if(rc!=SB_STORAGE_OK){close(fd);return rc;}*out=fd;return SB_STORAGE_OK;
}
void sb_storage_root_close(sb_storage_root *r){if(r&&r->fd>=0){close(r->fd);r->fd=-1;}}
sb_storage_status sb_storage_root_child(const sb_storage_root *r,const sb_music_media *media,const char *relative,
 sb_storage_mount_reader read,void *context,sb_storage_root *out)
{
 if(!out||out==r)return SB_STORAGE_INVALID;
 memset(out,0,sizeof *out);out->fd=-1;int fd=-1;
 sb_storage_status status=sb_storage_open_relative(r,media,relative,1,read,context,&fd);if(status!=SB_STORAGE_OK)return status;
 *out=*r;out->fd=fd;
 if(snprintf(out->relative,sizeof out->relative,"%s%s%s",r->relative,r->relative[0]?"/":"",relative)>=(int)sizeof out->relative){sb_storage_root_close(out);return SB_STORAGE_LIMIT;}
 struct stat st;if(fstat(fd,&st)){sb_storage_root_close(out);return SB_STORAGE_IO;}
 out->device=st.st_dev;out->inode=st.st_ino;
 status=sb_storage_root_check(out,media,read,context);if(status!=SB_STORAGE_OK)sb_storage_root_close(out);return status;
}

/* Old firmware persistent-storage rules supply both directories. Never use
 * volume labels or a by-path location as persistent evidence. Ambiguous aliases
 * fail closed. Missing serial/UUID deliberately gives a session identity. */
static int link_identity(const char *dir,const struct stat *device,char *out,size_t cap)
{
 DIR *d=opendir(dir);if(!d)return 0;struct dirent *e;unsigned count=0,found=0;
 while((e=readdir(d))){if(++count>512){found=0;break;}if(e->d_name[0]=='.')continue;
  char path[512];if(snprintf(path,sizeof path,"%s/%s",dir,e->d_name)>=(int)sizeof path)continue;
  struct stat st;if(stat(path,&st)||!S_ISBLK(st.st_mode)||st.st_rdev!=device->st_rdev)continue;
  /* Multiple aliases are conservatively uncertain rather than order-dependent. */
  if(++found>1||strlen(e->d_name)>=cap){found=0;break;}strcpy(out,e->d_name);
 }closedir(d);if(found!=1)out[0]=0;return found==1;
}
static int sys_attribute(const char *base,const char *name,char *out,size_t cap)
{
 char path[2048];
 if(snprintf(path,sizeof path,"%s/%s",base,name)>=(int)sizeof path)return 0;
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
 ssize_t n=read(fd,out,cap);close(fd);
 if(n<=0||(size_t)n>=cap)return 0;
 out[n]=0;out[strcspn(out,"\r\n")]=0;
 return out[0]!=0;
}
/* Original Fab4 udev creates by-uuid but may omit by-id. Recover only actual
 * USB serial + vendor/product + partition evidence from the trusted sysfs tree;
 * never substitute a port path, volume label or sdX allocation as identity. */
static int usb_partition_identity(const sb_storage_mount *m,char *out,size_t cap)
{
 const char *leaf=strrchr(m->device,'/');if(!leaf||!leaf[1])return 0;leaf++;
 for(const unsigned char *p=(const unsigned char *)leaf;*p;p++)if(!isalnum(*p)&&*p!='_'&&*p!='-')return 0;
 DIR *dir=opendir("/sys/block");if(!dir)return 0;
 struct dirent *entry;char path[2048],resolved[4096],value[64],partition[32];
 unsigned count=0;int found=0;
 while((entry=readdir(dir))) {
  if(++count>128)break;
  if(entry->d_name[0]=='.')continue;
  if(snprintf(path,sizeof path,"/sys/block/%s/%s",entry->d_name,leaf)>=(int)sizeof path)continue;
  unsigned maj,min,part;char extra;
  if(!sys_attribute(path,"dev",value,sizeof value)||sscanf(value,"%u:%u%c",&maj,&min,&extra)!=2||
     maj!=m->major||min!=m->minor)continue;
  if(!sys_attribute(path,"partition",partition,sizeof partition)) {
   /* 2.6.26 exposes the partition child/dev but may omit its number attribute. */
   size_t n=strlen(entry->d_name);
   if(strncmp(leaf,entry->d_name,n))continue;
   const char *suffix=leaf+n;if(*suffix=='p')suffix++;
   if(!*suffix||strlen(suffix)>=sizeof partition)continue;
   strcpy(partition,suffix);
  }
  if(sscanf(partition,"%u%c",&part,&extra)!=1||!part||part>65535)continue;
  if(snprintf(path,sizeof path,"/sys/block/%s/device",entry->d_name)>=(int)sizeof path||
     !realpath(path,resolved)||strncmp(resolved,"/sys/devices/",13))continue;
  for(unsigned depth=0;depth<10;depth++) {
   char serial[128],vendor[16],product[16];
   if(sys_attribute(resolved,"serial",serial,sizeof serial)&&
      sys_attribute(resolved,"idVendor",vendor,sizeof vendor)&&
      sys_attribute(resolved,"idProduct",product,sizeof product)&&strlen(vendor)==4&&strlen(product)==4) {
    int valid=strcmp(serial,"0")!=0;
    for(const unsigned char *p=(const unsigned char *)serial;*p;p++)if(!isalnum(*p)&&*p!='-'&&*p!='_')valid=0;
    for(unsigned i=0;i<4;i++)if(!isxdigit((unsigned char)vendor[i])||!isxdigit((unsigned char)product[i]))valid=0;
    if(valid&&snprintf(out,cap,"usb:%s:%s:%s:partition:%u",vendor,product,serial,part)<(int)cap)found=1;
    break;
   }
   char *slash=strrchr(resolved,'/');if(!slash||slash<=resolved+12)break;*slash=0;
  }
  break;
 }
 closedir(dir);return found;
}
sb_storage_status sb_storage_identity_linux(const sb_storage_mount *m,sb_storage_evidence *out)
{
 if(!m||!out)return SB_STORAGE_INVALID;
 memset(out,0,sizeof *out);
 struct stat device;if(strncmp(m->device,"/dev/",5)||stat(m->device,&device)||!S_ISBLK(device.st_mode))return SB_STORAGE_IO;
 if(strcmp(m->fs,"fuseblk")&&(major(device.st_rdev)!=m->major||minor(device.st_rdev)!=m->minor))return SB_STORAGE_STALE;
 link_identity("/dev/disk/by-uuid",&device,out->uuid,sizeof out->uuid);
 link_identity("/dev/disk/by-id",&device,out->hardware_partition,sizeof out->hardware_partition);
 if(!out->hardware_partition[0])usb_partition_identity(m,out->hardware_partition,sizeof out->hardware_partition);
 return SB_STORAGE_OK;
}

struct sb_storage_worker {
 pthread_t thread;pthread_mutex_t lock;pthread_cond_t condition;
 sb_storage_worker_options options;sb_storage_discovery result;
 int pipe[2],pending,ready,stop,exited;uint64_t last;
};
static void *worker(void *context)
{
 sb_storage_worker *w=context;pthread_mutex_lock(&w->lock);
 for(;;){
  while(!w->pending&&!w->stop)pthread_cond_wait(&w->condition,&w->lock);
  if(w->stop)break;
  pthread_mutex_unlock(&w->lock);
  w->result.status=w->options.reader(w->options.reader_context,&w->result.mounts);
  /* Only approved child mountpoints are exposed by the Fab4 bridge. Root
   * authorization remains a separate worker operation before any file use. */
  if(w->result.status==SB_STORAGE_OK){unsigned n=0;
   for(unsigned i=0;i<w->result.mounts.count;i++)if(canonical(w->result.mounts.rows[i].path,1)&&beneath(w->options.approved_parent,w->result.mounts.rows[i].path)&&!strncmp(w->result.mounts.rows[i].device,"/dev/",5))
    w->result.mounts.rows[n++]=w->result.mounts.rows[i];
   w->result.mounts.count=n;
   for(unsigned i=0;i<n;i++){
    sb_music_media provisional={{{1}},1,1};sb_storage_root root;
    w->result.validation[i]=sb_storage_validate(w->options.approved_parent,&w->result.mounts.rows[i],&provisional,w->options.reader,w->options.reader_context,&root);
    if(w->result.validation[i]==SB_STORAGE_OK)sb_storage_root_close(&root);
    memset(&w->result.evidence[i],0,sizeof w->result.evidence[i]);
    if(w->result.validation[i]==SB_STORAGE_OK&&w->options.reader==sb_storage_read_mounts)
     w->result.validation[i]=sb_storage_identity_linux(&w->result.mounts.rows[i],&w->result.evidence[i]);
   }
  }
  pthread_mutex_lock(&w->lock);w->pending=0;w->ready=1;
  char c=1;ssize_t n;do{n=write(w->pipe[1],&c,1);}while(n<0&&errno==EINTR);
 }
 w->exited=1;pthread_mutex_unlock(&w->lock);char c=1;ssize_t n;do{n=write(w->pipe[1],&c,1);}while(n<0&&errno==EINTR);return NULL;
}
sb_storage_worker *sb_storage_worker_start(const sb_storage_worker_options *options)
{
 if(!options||!options->enabled||!options->fab4||!memchr(options->approved_parent,0,sizeof options->approved_parent)||!canonical(options->approved_parent,1))return NULL;
 sb_storage_worker *w=calloc(1,sizeof *w);if(!w)return NULL;
 w->options=*options;
 if(!w->options.reader)w->options.reader=sb_storage_read_mounts;
 if(pipe(w->pipe)){free(w);return NULL;}
 for(int i=0;i<2;i++)if(fcntl(w->pipe[i],F_SETFL,O_NONBLOCK)||fcntl(w->pipe[i],F_SETFD,FD_CLOEXEC)){close(w->pipe[0]);close(w->pipe[1]);free(w);return NULL;}
 if(pthread_mutex_init(&w->lock,NULL)){close(w->pipe[0]);close(w->pipe[1]);free(w);return NULL;}
 if(pthread_cond_init(&w->condition,NULL)){pthread_mutex_destroy(&w->lock);close(w->pipe[0]);close(w->pipe[1]);free(w);return NULL;}
 pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,256*1024);
 int rc=pthread_create(&w->thread,&attr,worker,w);pthread_attr_destroy(&attr);
 if(rc){pthread_cond_destroy(&w->condition);pthread_mutex_destroy(&w->lock);close(w->pipe[0]);close(w->pipe[1]);free(w);return NULL;}return w;
}
int sb_storage_worker_fd(const sb_storage_worker *w){return w?w->pipe[0]:-1;}
sb_storage_status sb_storage_worker_submit(sb_storage_worker *w,uint64_t id)
{
 if(!w)return SB_STORAGE_DISABLED;
 if(pthread_mutex_trylock(&w->lock))return SB_STORAGE_BUSY;
 sb_storage_status rc=w->stop?SB_STORAGE_STALE:w->pending||w->ready?SB_STORAGE_BUSY:!id||id<=w->last?SB_STORAGE_STALE:SB_STORAGE_OK;
 if(rc==SB_STORAGE_OK){w->last=id;w->result.request_id=id;w->pending=1;pthread_cond_signal(&w->condition);}
 pthread_mutex_unlock(&w->lock);return rc;
}
sb_storage_status sb_storage_worker_receive(sb_storage_worker *w,sb_storage_discovery *out)
{
 if(!w)return SB_STORAGE_DISABLED;
 if(!out)return SB_STORAGE_INVALID;
 if(pthread_mutex_trylock(&w->lock))return SB_STORAGE_BUSY;
 sb_storage_status rc=w->ready?SB_STORAGE_OK:SB_STORAGE_BUSY;
 if(w->ready){*out=w->result;w->ready=0;char c;ssize_t n;do{n=read(w->pipe[0],&c,1);}while(n<0&&errno==EINTR);}
 pthread_mutex_unlock(&w->lock);return rc;
}
sb_storage_status sb_storage_worker_stop(sb_storage_worker *w)
{if(!w)return SB_STORAGE_DISABLED;
 if(pthread_mutex_trylock(&w->lock))return SB_STORAGE_BUSY;
 w->stop=1;pthread_cond_signal(&w->condition);pthread_mutex_unlock(&w->lock);return SB_STORAGE_OK;}
sb_storage_status sb_storage_worker_destroy(sb_storage_worker *w)
{
 if(!w)return SB_STORAGE_DISABLED;
 if(pthread_mutex_trylock(&w->lock))return SB_STORAGE_BUSY;
 int done=w->exited;pthread_mutex_unlock(&w->lock);if(!done)return SB_STORAGE_BUSY;
 pthread_join(w->thread,NULL);pthread_cond_destroy(&w->condition);pthread_mutex_destroy(&w->lock);close(w->pipe[0]);close(w->pipe[1]);free(w);return SB_STORAGE_OK;
}

/* Explicit privileged read-only-content policy; fail closed if unsupported.
 * The flag follows dup/SCM_RIGHTS descriptors and every relative open. */
sb_storage_status sb_storage_root_noatime(sb_storage_root *root)
{
 if(!root||root->fd<0)return SB_STORAGE_INVALID;
 int flags=fcntl(root->fd,F_GETFL);
 if(flags<0||fcntl(root->fd,F_SETFL,flags|O_NOATIME)<0)return SB_STORAGE_IO;
 flags=fcntl(root->fd,F_GETFL);
 return flags>=0&&(flags&O_NOATIME)?SB_STORAGE_OK:SB_STORAGE_UNSUPPORTED;
}
