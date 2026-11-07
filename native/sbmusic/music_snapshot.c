#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "music_snapshot.h"
#include "../common/sha256.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <unistd.h>
#define HEADER 192
static const char *slots[] = {"library.0.snapshot", "library.1.snapshot"};
int sb_music_snapshot_source(const sb_storage_evidence *e,sb_music_source_id *source) {
    if(!e||!source||!e->uuid[0]||!e->hardware_partition[0]||
        !memchr(e->uuid,0,sizeof e->uuid)||!memchr(e->hardware_partition,0,sizeof e->hardware_partition))return 0;
    sha256_t identity;sha_init(&identity);
    sha_add(&identity,(const unsigned char *)e->uuid,strlen(e->uuid)+1);
    sha_add(&identity,(const unsigned char *)e->hardware_partition,strlen(e->hardware_partition)+1);
    char digest[65];sha_done(&identity,digest);
    for(unsigned i=0;i<16;i++){unsigned byte;sscanf(digest+2*i,"%2x",&byte);source->bytes[i]=(unsigned char)byte;}
    return 1;
}
static int current(sb_music_snapshot *s) {
    if (sb_storage_root_check(&s->root, &s->root.media, s->reader, s->context) != SB_STORAGE_OK)
        return 0;
    struct stat a,b;
    return !fstat(s->directory,&a) &&
        !fstatat(s->root.fd,s->name,&b,AT_SYMLINK_NOFOLLOW) &&
        S_ISDIR(b.st_mode) && a.st_dev==b.st_dev && a.st_ino==b.st_ino;
}
static int regular(int fd, uint64_t maximum, struct stat *st) {
    return fd>=0 && !fstat(fd,st) && S_ISREG(st->st_mode) && st->st_uid==geteuid() &&
        !(st->st_mode&0022) && st->st_nlink==1 && st->st_size>=0 &&
        (uint64_t)st->st_size<=maximum;
}
static int transfer(sb_music_snapshot *s,int in,int out,uint64_t length,char digest[65]) {
    sha256_t hash; sha_init(&hash);
    unsigned char buffer[8192];
    while(length) {
        if(!current(s))return 0;
        size_t amount=length<sizeof buffer?(size_t)length:sizeof buffer;
        ssize_t n=read(in,buffer,amount);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return 0;
        sha_add(&hash,buffer,(size_t)n);
        if(out>=0)for(ssize_t at=0;at<n;) {
            ssize_t wrote=write(out,buffer+at,(size_t)(n-at));
            if(wrote<0&&errno==EINTR)continue;
            if(wrote<=0)return 0;
            at+=wrote;
        }
        length-=(uint64_t)n;
    }
    sha_done(&hash,digest);
    return 1;
}
static void source_hex(sb_music_snapshot *s,char out[33]) {
    for(unsigned i=0;i<16;i++)snprintf(out+2*i,3,"%02x",s->root.media.source.bytes[i]);
}
int sb_music_snapshot_open(sb_music_snapshot *s,const sb_storage_root *r,
    const char *name,int create,uint64_t maximum,sb_storage_mount_reader reader,void *ctx) {
    memset(s,0,sizeof *s);s->directory=-1;s->slot=-1;s->root=*r;
    s->reader=reader;s->context=ctx;s->maximum_bytes=maximum;
    if(r->fd<0||(r->mount.read_only&&create)||!maximum||maximum>UINT64_C(536870912)||
        !name||strlen(name)>=sizeof s->name||strncmp(name,".standalone-music",17)||strchr(name,'/')||
        strchr(name,'\\')||strstr(name,"..")||!strcmp(name,".Squeezebox")||
        !strcmp(name,".")||sb_storage_root_check(r,&r->media,reader,ctx)!=SB_STORAGE_OK)return 0;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)if(*p<32||*p==127)return 0;
    strcpy(s->name,name);
    struct stat st;
    if(fstatat(r->fd,name,&st,AT_SYMLINK_NOFOLLOW)) {
        if(errno!=ENOENT||!create||mkdirat(r->fd,name,0700))return 0;
        if(fsync(r->fd))return 0;
    }
    s->directory=openat(r->fd,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC|O_NOATIME);
    if(s->directory<0||fstat(s->directory,&st)||st.st_uid!=geteuid()||
        (st.st_mode&0022)||flock(s->directory,LOCK_EX|LOCK_NB)||!current(s)) {
        sb_music_snapshot_close(s);return 0;
    }
    int duplicate=dup(s->directory);
    DIR *dir=duplicate<0?NULL:fdopendir(duplicate);
    if(!dir){if(duplicate>=0)close(duplicate);sb_music_snapshot_close(s);return 0;}
    struct dirent *entry;unsigned count=0;int safe=1;
    errno=0;
    while((entry=readdir(dir))) {
        if(++count>8){safe=0;break;}
        if(strcmp(entry->d_name,".")&&strcmp(entry->d_name,"..")&&
            strcmp(entry->d_name,slots[0])&&strcmp(entry->d_name,slots[1])&&
            strcmp(entry->d_name,"library.pending")&&
            strcmp(entry->d_name,"library.interrupted.0")&&strcmp(entry->d_name,"library.interrupted.1")){safe=0;break;}
    }
    if(errno)safe=0;
    closedir(dir);
    if(!safe){sb_music_snapshot_close(s);return 0;}
    return 1;
}
static int inspect(sb_music_snapshot *s,int slot,uint64_t *seq,uint64_t *size,char hash[65],int verify) {
    int fd=openat(s->directory,slots[slot],O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NOATIME);
    if(fd<0)return errno==ENOENT?0:-1;
    struct stat st;char header[HEADER+1],source[33],expected[33],extra;
    unsigned long long a,b;
    int ok=regular(fd,s->maximum_bytes+HEADER,&st)&&read(fd,header,HEADER)==HEADER;
    header[HEADER]=0;source_hex(s,expected);
    if(ok)ok=sscanf(header,"SBMUSIC1 %llu %llu %64s %32s %c",&a,&b,hash,source,&extra)==4&&
        a>0&&a<INT64_MAX&&b>=4096&&b<=s->maximum_bytes&&
        (uint64_t)st.st_size==b+HEADER&&!strcmp(source,expected)&&strlen(hash)==64;
    char computed[65];
    if(ok&&verify)ok=transfer(s,fd,-1,b,computed)&&!strcmp(computed,hash);
    close(fd);if(!ok)return -1;*seq=a;*size=b;return 1;
}
uint64_t sb_music_snapshot_hint(sb_music_snapshot *s) {
    uint64_t sequence[2]={0},size;char hash[65];
    if(!current(s))return 0;
    for(int i=0;i<2;i++)if(inspect(s,i,&sequence[i],&size,hash,0)<0)sequence[i]=0;
    return sequence[0]>sequence[1]?sequence[0]:sequence[1];
}
static int load_snapshot(sb_music_snapshot *s,int output,int previous) {
    uint64_t seq[2]={0},size[2]={0};char hash[2][65];
    if(!current(s))return -1;
    int a=inspect(s,0,&seq[0],&size[0],hash[0],1);
    int b=inspect(s,1,&seq[1],&size[1],hash[1],1);
    if(a==0&&b==0){
        struct stat pending;
        /* An interrupted first publication is existing recoverable data,
         * never permission to initialize an empty replacement. */
        if(!fstatat(s->directory,"library.pending",&pending,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return -1;
        if(!fstatat(s->directory,"library.interrupted.0",&pending,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return -1;
        if(!fstatat(s->directory,"library.interrupted.1",&pending,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return -1;
        return 0;
    }
    if(a<=0&&b<=0)return -1;
    s->damaged=a<0||b<0;
    if(s->damaged&&!s->quiet)fputs("library: corrupt snapshot retained; verified surviving generation is read-only until explicit recovery\n",stderr);
    if(a>0&&b>0&&seq[0]==seq[1]&&strcmp(hash[0],hash[1]))return -1;
    int slot=b>0&&(a<=0||seq[1]>seq[0])?1:0;
    int newest=slot;
    if(previous){if(a<=0||b<=0||seq[0]==seq[1])return -1;slot=1-slot;}
    struct stat output_stat;struct statfs output_fs;struct statvfs available;
    if(!regular(output,s->maximum_bytes,&output_stat)||output_stat.st_size||
        fstatfs(output,&output_fs)||output_fs.f_type!=0x01021994){errno=EINVAL;return -1;}
    if(fstatvfs(output,&available)||!available.f_frsize)return -1;
    if((uint64_t)available.f_bavail<(size[slot]+1048576)/available.f_frsize+1){errno=ENOSPC;return -1;}
    int fd=openat(s->directory,slots[slot],O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NOATIME);
    char computed[65];
    int ok=fd>=0&&lseek(fd,HEADER,SEEK_SET)==HEADER&&
        ftruncate(output,0)==0&&lseek(output,0,SEEK_SET)==0&&
        transfer(s,fd,output,size[slot],computed)&&!strcmp(computed,hash[slot])&&fsync(output)==0;
    if(fd>=0)close(fd);
    if(!ok)return -1;
    s->sequence=seq[newest];s->slot=newest;return 1;
}
int sb_music_snapshot_load(sb_music_snapshot *s,int output){return load_snapshot(s,output,0);}
int sb_music_snapshot_load_previous(sb_music_snapshot *s,int output){return load_snapshot(s,output,1);}
static int preserve_interrupted(sb_music_snapshot *s,const char *file) {
    struct stat st;
    if(!s->sequence||fstatat(s->directory,file,&st,AT_SYMLINK_NOFOLLOW)||!S_ISREG(st.st_mode)||
        st.st_uid!=geteuid()||st.st_nlink!=1||(st.st_mode&0022)||st.st_size<0||
        (uint64_t)st.st_size>s->maximum_bytes+HEADER)return 0;
    for(unsigned i=0;i<2;i++) {
        char name[48];snprintf(name,sizeof name,"library.interrupted.%u",i);
        struct stat prior;
        if(!fstatat(s->directory,name,&prior,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)continue;
        if(!current(s)||renameat(s->directory,file,s->directory,name)||fsync(s->directory))return 0;
        fprintf(stderr,"library: preserved %s as %s\n",file,name);return 1;
    }
    return 0;
}
int sb_music_snapshot_recover(sb_music_snapshot *s) {
    if(!current(s)||s->slot<0||s->root.mount.read_only)return 0;
    if(!s->damaged)return 1;
    uint64_t maximum=sb_music_snapshot_hint(s);
    if(maximum>s->sequence)s->sequence=maximum;
    if(s->sequence>=INT64_MAX-1||!preserve_interrupted(s,slots[1-s->slot]))return 0;
    s->damaged=0;return 1;
}
int sb_music_snapshot_publish(sb_music_snapshot *s,int db) {
    struct stat st;struct statvfs space;
    if(s->damaged||s->root.mount.read_only||!current(s)||!regular(db,s->maximum_bytes,&st)||st.st_size<4096||
        fstatvfs(s->directory,&space)||!space.f_frsize||
        (uint64_t)space.f_bavail < ((uint64_t)st.st_size+HEADER+1048576)/space.f_frsize+1||
        s->sequence>=INT64_MAX-1)return 0;
    /* Retain bounded interrupted publications before a new explicitly requested
     * write. No corrupt committed snapshot is overwritten by this procedure. */
    struct stat interrupted;
    if(!fstatat(s->directory,"library.pending",&interrupted,AT_SYMLINK_NOFOLLOW)) {
        if(!preserve_interrupted(s,"library.pending"))return 0;
    } else if(errno!=ENOENT)return 0;
    int out=openat(s->directory,"library.pending",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(out<0)return 0;
    char header[HEADER]={0},hash[65],source[33];
    int ok=write(out,header,HEADER)==HEADER&&lseek(db,0,SEEK_SET)==0&&
        transfer(s,db,out,(uint64_t)st.st_size,hash);
    source_hex(s,source);
    if(ok) {
        snprintf(header,sizeof header,"SBMUSIC1 %llu %llu %s %s\n",
            (unsigned long long)(s->sequence+1),(unsigned long long)st.st_size,hash,source);
        ok=lseek(out,0,SEEK_SET)==0&&write(out,header,HEADER)==HEADER&&fsync(out)==0;
    }
    if(close(out))ok=0;
    int slot=s->slot==0?1:0;
    if(ok)ok=current(s)&&renameat(s->directory,"library.pending",s->directory,slots[slot])==0&&
        fsync(s->directory)==0;
    /* Never discard failed writes; pending is retained for explicit diagnosis. */
    if(ok){s->sequence++;s->slot=slot;}
    return ok;
}
void sb_music_snapshot_close(sb_music_snapshot *s) {
    if(s->directory>=0)close(s->directory);
    s->directory=-1;
}
