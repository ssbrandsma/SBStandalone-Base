/* Explicit offline library maintenance. No listener, decoder, TinySC or Jive API. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "music_snapshot.h"
#include "music_scanner.h"
#include "../common/sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t cancelled;
static int job_status=-1,job_result=2,job_directory=-1;
static void finish_job(void) {
 if(job_status>=0){char text[16];int n=snprintf(text,sizeof text,"%d\n",job_result);
  fflush(NULL);fsync(STDOUT_FILENO);
  ssize_t wrote;do{wrote=write(job_status,text,(size_t)n);}while(wrote<0&&errno==EINTR);
  if(wrote!=n)fputs("library: detached completion receipt write failed\n",stderr);
  fsync(job_status);close(job_status);}
 if(job_directory>=0)close(job_directory);
}
static int detach_scan(void) {
#ifdef SB_MUSIC_LIBRARY_TESTING
 const char *directory="/dev/shm/sbmusic-library-job";
 const char *parent="/dev/shm";
#else
 const char *directory="/dev/sbmusic-library-job";
 const char *parent="/dev";
#endif
 struct statfs parent_fs;if(statfs(parent,&parent_fs)||parent_fs.f_type!=0x01021994)return 2;
 if(mkdir(directory,0700))return 2;
 int directory_fd=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(directory_fd<0)return 2;
 char path[128];snprintf(path,sizeof path,"%s/result.json",directory);
 int output=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 snprintf(path,sizeof path,"%s/scan.log",directory);
 int log=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 snprintf(path,sizeof path,"%s/exit",directory);
 int status=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(output<0||log<0||status<0)return 2;
 signal(SIGHUP,SIG_IGN); /* inherited before parent can report the child */
 pid_t child=fork();if(child<0)return 2;
 if(child){close(directory_fd);close(output);close(log);close(status);printf("{\"pid\":%ld,\"detached\":true}\n",(long)child);return 0;}
 job_status=status;job_directory=directory_fd;atexit(finish_job);
 if(setsid()<0)exit(2);
 signal(SIGHUP,SIG_IGN);
 int input=open("/dev/null",O_RDONLY);
 if(input<0||dup2(input,0)<0||dup2(output,1)<0||dup2(log,2)<0)exit(2);
 close(input);close(output);close(log);
 return -1;
}
static void cancel(int sig){(void)sig;cancelled=1;}
static uint64_t clock_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void pause_ms(void){struct timespec t={0,10000000};nanosleep(&t,NULL);}
static void log_text(const char *text){for(const unsigned char *p=(const unsigned char *)text;*p;p++)fputc(*p>=32&&*p!=127?*p:'?',stderr);}
static long resident_peak(long fallback) {
 FILE *file=fopen("/proc/self/status","r");if(!file)return fallback;
 char line[256];long value=fallback;
 while(fgets(line,sizeof line,file))if(sscanf(line,"VmHWM: %ld kB",&value)==1)break;
 fclose(file);return value;
}
static int exchange(sb_music_library *h,sb_ml_request *r,sb_ml_result *v) {
    sb_ml_status got=sb_music_library_exchange(h,r,v);
    if(got!=SB_ML_OK||v->status!=SB_ML_OK){fprintf(stderr,"library: %s %s\n",sb_music_library_status(v->status),v->error);return 0;}
    return 1;
}
int main(int argc,char **argv) {
    if(argc!=6){fputs("usage: sbmusic-library init|open|integrity|scan|scan-detached|backup|restore|recover|migrate|rebuild|probe MOUNT .standalone-music HELPER MAX_DB_MIB\n",stderr);return 2;}
    int detached=!strcmp(argv[1],"scan-detached"),rebuilding=!strcmp(argv[1],"rebuild");
    int initialize=!strcmp(argv[1],"init")||rebuilding, scanning=!strcmp(argv[1],"scan")||detached||rebuilding,probing=!strcmp(argv[1],"probe");
    int backup=!strcmp(argv[1],"backup"),restore=!strcmp(argv[1],"restore");
    int migrating=!strcmp(argv[1],"migrate");
    int recovering=!strcmp(argv[1],"recover");
    if(!initialize&&!scanning&&!probing&&!backup&&!restore&&!migrating&&!recovering&&strcmp(argv[1],"open")&&strcmp(argv[1],"integrity"))return 2;
#if !defined(SB_TARGET_FAB4) && !defined(SB_MUSIC_LIBRARY_TESTING)
    if(initialize||scanning||backup||restore||migrating||recovering){fputs("library: hardware mutation is Fab4-only\n",stderr);return 2;}
#endif
    char *end;unsigned long mib=strtoul(argv[5],&end,10);
    if(!*argv[5]||*end||mib<1||mib>64)return 2;
    uint64_t maximum=(uint64_t)mib*1048576, began=clock_ms();
    umask(0077);signal(SIGTERM,cancel);signal(SIGINT,cancel);
    if(detached){int result=detach_scan();if(result>=0)return result;}
    errno=0;int priority=nice(10);
    if(priority==-1&&errno)fputs("library: low-priority scheduling unavailable\n",stderr);
    sb_storage_mounts *table=calloc(1,sizeof *table);
    if(!table||sb_storage_read_mounts(NULL,table)!=SB_STORAGE_OK)return 2;
    sb_storage_mount mount={0};
    for(unsigned i=0;i<table->count;i++) {
        sb_storage_mount *candidate=&table->rows[i];
        int match=!strcmp(candidate->path,argv[2]);
#ifdef SB_MUSIC_LIBRARY_TESTING
        size_t n=strlen(candidate->path);
        match=match||(!strncmp(candidate->path,argv[2],n)&&argv[2][n]=='/');
#endif
        if(match&&strlen(candidate->path)>strlen(mount.path))mount=*candidate;
    }
    free(table);
    const char *parent="/media";
    char work[128]="/dev/sbmusic-library.XXXXXX";
    sb_storage_evidence evidence={0};
#ifdef SB_MUSIC_LIBRARY_TESTING
    parent="/dev";strcpy(work,"/dev/shm/sbmusic-library.XXXXXX");
    snprintf(evidence.uuid,sizeof evidence.uuid,"host-fixture");
    snprintf(evidence.hardware_partition,sizeof evidence.hardware_partition,"%s",argv[2]);
#else
    if(sb_storage_identity_linux(&mount,&evidence)!=SB_STORAGE_OK)return 2;
#endif
    if(!mount.mount_id||(mount.read_only&&(initialize||scanning||backup||restore||migrating||recovering))||!evidence.uuid[0]||!evidence.hardware_partition[0]) {
        fprintf(stderr,"library: validated writable mount with stable UUID and hardware/partition identity required (mount=%llu readonly=%d uuid=%d hardware=%d)\n",
            (unsigned long long)mount.mount_id,mount.read_only,!!evidence.uuid[0],!!evidence.hardware_partition[0]);return 2;
    }
    sb_music_media media={0};media.generation=mount.mount_id;media.online=1;
    if(!sb_music_snapshot_source(&evidence,&media.source))return 2;
    if(probing){printf("{\"mount_id\":%llu,\"fs\":\"%s\",\"uuid\":\"%s\",\"hardware_partition\":\"%s\"}\n",
        (unsigned long long)mount.mount_id,mount.fs,evidence.uuid,evidence.hardware_partition);return 0;}
    sb_storage_root root={.fd=-1};sb_music_snapshot snapshot={.directory=-1};
    sb_music_library *library=NULL;sb_music_scanner *scanner=NULL;
    sb_ml_request request={0};sb_ml_result result;sb_scan_status progress={0};
    int fd=-1,ok=0,opened=0,work_created=0,migrated=0;uint64_t initial_revision=0,revision=0;
    const char *phase="validate-root";
    char db[256]={0};
    if(sb_storage_validate(parent,&mount,&media,NULL,NULL,&root)!=SB_STORAGE_OK)goto done;
#ifdef SB_MUSIC_LIBRARY_TESTING
    if(strcmp(mount.path,argv[2])) {
        sb_storage_root child={.fd=-1};
        if(sb_storage_root_child(&root,&media,argv[2]+strlen(mount.path)+1,NULL,NULL,&child)!=SB_STORAGE_OK)goto done;
        sb_storage_root_close(&root);root=child;
    }
#endif
    if(sb_storage_root_noatime(&root)!=SB_STORAGE_OK)goto done;
    phase="snapshot-directory";
    if(!sb_music_snapshot_open(&snapshot,&root,argv[3],initialize||scanning,maximum,NULL,NULL)) {
        fputs("library: snapshot directory, ownership or exclusive lock refused\n",stderr);goto done;
    }
    phase="working-ram";
    struct statfs fs;struct statvfs capacity;
#ifdef SB_MUSIC_LIBRARY_TESTING
    const char *ram_parent="/dev/shm";
#else
    const char *ram_parent="/dev";
#endif
    if(statfs(ram_parent,&fs)||fs.f_type!=0x01021994)goto done;
    if(!mkdtemp(work))goto done;
    work_created=1;
    if(statfs(work,&fs)||fs.f_type!=0x01021994||statvfs(work,&capacity)||
        !capacity.f_frsize||(uint64_t)capacity.f_bavail<(maximum+4194304)/capacity.f_frsize)goto done;
    snprintf(db,sizeof db,"%s/library.db",work);
    fd=open(db,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)goto done;
    phase="load-snapshot";
    int restored=restore?sb_music_snapshot_load_previous(&snapshot,fd):sb_music_snapshot_load(&snapshot,fd);
    if(restored<0||(!restored&&!initialize&&!scanning)||(restored&&initialize)) {
        fputs("library: missing/corrupt snapshot or initialization of existing library refused\n",stderr);goto done;
    }
    close(fd);fd=-1;
    sb_ml_options options={0};snprintf(options.path,sizeof options.path,"%s",db);
    options.create=!restored;options.query_ms=5000;options.busy_ms=100;
    options.read_only=!scanning&&!initialize&&!migrating&&!recovering;
    options.single_source=1;
    options.maximum_bytes=maximum;
    phase="open-database";
    library=sb_music_library_start(&options);
    uint64_t until=clock_ms()+10000;
    if(!library)goto done;
    while(sb_music_library_receive(library,&result)==SB_ML_BUSY){if(clock_ms()>until)goto done;pause_ms();}
    if(result.status!=SB_ML_OK){fprintf(stderr,"library open: %s\n",result.error);goto done;}
    migrated=restored&&result.record_id<SB_ML_SCHEMA_VERSION;
    opened=1;
    initial_revision=result.revision;
    phase="bind-source";
    request.operation=SB_ML_SOURCE;request.data.media=media;
    if(!exchange(library,&request,&result))goto done;
    if(scanning) {
        phase="queue-scan";
        sb_scan_options scan={0};scan.enabled=scan.approved_database=1;
        scan.library=library;scan.root=root;scan.batch_files=16;scan.helper_timeout_ms=5000;
        scan.work_duty_percent=25;scan.yield_ms=10;
        scan.inode_identity=!strcmp(mount.fs,"ext2")||!strcmp(mount.fs,"ext3")||!strcmp(mount.fs,"tmpfs");
        if(strlen(argv[4])>=sizeof scan.helper)goto done;
        strcpy(scan.helper,argv[4]);sb_scan_code code;
        scanner=sb_music_scan_create(&scan,&code);
        if(!scanner){fprintf(stderr,"library: scanner constructor refused (%d)\n",code);goto done;}
        uint64_t queue_until=clock_ms()+5000;
        code=SB_SCAN_BUSY;
        while(code==SB_SCAN_BUSY&&!cancelled&&clock_ms()<queue_until){
            code=sb_music_scan_request(scanner,1,0);if(code==SB_SCAN_BUSY)pause_ms();}
        if(code!=SB_SCAN_OK){fprintf(stderr,"library: scanner request not admitted (%d)\n",code);goto done;}
        phase="scanning";
        uint64_t report_due=0,scan_deadline=clock_ms()+UINT64_C(3600000);
        for(;;) {
            sb_music_scan_status(scanner,&progress);
            if(progress.state>=SB_SCAN_COMPLETED)break;
            if(job_directory>=0){struct stat marker;
                if(!fstatat(job_directory,"cancel",&marker,AT_SYMLINK_NOFOLLOW)&&S_ISREG(marker.st_mode)&&
                    marker.st_uid==geteuid()&&!(marker.st_mode&0022)&&!marker.st_size)cancelled=1;}
            if(cancelled||clock_ms()>=scan_deadline)sb_music_scan_cancel(scanner,1);
            if(clock_ms()>=report_due){fprintf(stderr,"SCAN state=%u files=%llu tracks=%llu errors=%llu\n",
                progress.state,(unsigned long long)progress.visited,(unsigned long long)progress.recognized,
                (unsigned long long)progress.errors);report_due=clock_ms()+1000;}
            pause_ms();
        }
        fprintf(stderr,"SCAN_FINISHED state=%u files=%llu tracks=%llu errors=%llu elapsed_ms=%llu\n",progress.state,
            (unsigned long long)progress.visited,(unsigned long long)progress.recognized,
            (unsigned long long)progress.errors,(unsigned long long)progress.elapsed_ms);
        for(unsigned i=0;i<progress.error_count&&i<SB_SCAN_ERRORS;i++){
            fprintf(stderr,"SCAN_ERROR recoverable=%d code=%d track_id=%llu size=%llu mtime=%lld fingerprint=",progress.error[i].recoverable,progress.error[i].code,
                (unsigned long long)progress.error[i].track_id,(unsigned long long)progress.error[i].file_size,(long long)progress.error[i].mtime);
            log_text(progress.error[i].fingerprint);fputs(" path=",stderr);log_text(progress.error[i].path);
            fputs(" message=",stderr);log_text(progress.error[i].message);fputc('\n',stderr);
        }
        if(progress.state!=SB_SCAN_COMPLETED||progress.fatal_errors)goto done;
        while(sb_music_scan_stop(scanner)==SB_SCAN_BUSY)pause_ms();
        while(sb_music_scan_destroy(scanner)==SB_SCAN_BUSY)pause_ms();
        scanner=NULL;
    }
    phase="integrity";
    request=(sb_ml_request){.operation=SB_ML_INTEGRITY};
    if(!exchange(library,&request,&result))goto done;
    request=(sb_ml_request){.operation=SB_ML_BROWSE_SUMMARY};
    request.data.query.media=media;request.data.query.limit=1;
    if(!exchange(library,&request,&result))goto done;
    unsigned tracks=result.data.summary.tracks,albums=result.data.summary.albums,artists=result.data.summary.artists;
    revision=result.revision;
    request.operation=SB_ML_QUERY;request.data.query.kind=SB_QUERY_GENRES;
    if(!exchange(library,&request,&result))goto done;
    unsigned genres=result.data.query.page.total;
    /* Exercise the same bounded native query contracts on reopen. No scanner
     * or decoder is constructed; pagination cannot run past a stable total. */
    sha256_t ids;sha_init(&ids);
    unsigned browse_counts[5]={0};
    for(unsigned kind=0;kind<5;kind++) {
        unsigned offset=0;
        do {
            request=(sb_ml_request){.operation=SB_ML_QUERY};
            request.data.query.media=media;request.data.query.kind=(sb_music_query_kind)kind;
            request.data.query.limit=SB_MUSIC_PAGE_MAX;request.data.query.offset=offset;
            if(!exchange(library,&request,&result))goto done;
            sb_music_page page=result.data.query.page;
            if(page.count>SB_MUSIC_PAGE_MAX||page.count>page.total||
                offset>page.total||page.count>page.total-offset||(!page.count&&offset<page.total))goto done;
            browse_counts[kind]=page.total;
            if(kind==SB_QUERY_TRACKS)for(unsigned i=0;i<page.count;i++) {
                char id[32];int length=snprintf(id,sizeof id,"%llu\n",(unsigned long long)result.data.query.rows[i].id);
                sha_add(&ids,(const unsigned char *)id,(size_t)length);
            }
            offset+=page.count;
        } while(offset<browse_counts[kind]);
    }
    char track_ids[65];sha_done(&ids,track_ids);
    request=(sb_ml_request){.operation=SB_ML_STATS};
    if(!exchange(library,&request,&result))goto done;
    sb_ml_stats statistics=result.data.stats;
    phase="close-database";
    request=(sb_ml_request){.operation=SB_ML_CLOSE};
    if(!exchange(library,&request,&result))goto done;
    opened=0;
    if(sb_music_library_destroy(library)!=SB_ML_OK)goto done;
    library=NULL;
    fd=open(db,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)goto done;
    /* A no-content-change scan never rewrites USB snapshots, even though its
     * private working database advanced the scan counter. */
    int published=!restored||migrated||revision!=initial_revision||backup||restore||recovering;
    phase="publish-snapshot";
    if(recovering&&!sb_music_snapshot_recover(&snapshot))goto done;
    if(published&&!sb_music_snapshot_publish(&snapshot,fd))goto done;
    struct stat st;struct rusage usage,children;
    if(fstat(fd,&st)||getrusage(RUSAGE_SELF,&usage)||getrusage(RUSAGE_CHILDREN,&children))goto done;
    printf("{\"tracks\":%u,\"albums\":%u,\"artists\":%u,\"revision\":%llu,\"bytes\":%llu,"
        "\"elapsed_ms\":%llu,\"scan\":%s,\"added\":%llu,\"updated\":%llu,\"unchanged\":%llu,\"removed\":%llu,"
        "\"files\":%llu,\"errors\":%llu,\"recoverable\":%llu,\"fatal\":%llu,\"snapshot_written\":%s,\"snapshot_sequence\":%llu,\"rss_kib\":%ld,"
        "\"genres\":%u,\"directories\":%llu,\"metadata_parses\":%llu,\"row_inserts\":%llu,\"row_updates\":%llu,"
        "\"row_deletes\":%llu,\"track_updates\":%llu,\"snapshot_bytes_written\":%llu,\"parser_rss_kib\":%u,\"cpu_ms\":%llu,"
        "\"sql_inserts\":%llu,\"sql_updates\":%llu,\"sql_deletes\":%llu,\"temporary_writes\":%llu,\"child_cpu_ms\":%llu,"
        "\"browse_counts\":[%u,%u,%u,%u,%u],\"track_ids_sha256\":\"%s\",\"snapshot_degraded\":%s,\"scan_state\":%u}\n",
        tracks,albums,artists,(unsigned long long)revision,(unsigned long long)st.st_size,
        (unsigned long long)(clock_ms()-began),scanning?"true":"false",
        (unsigned long long)progress.added,(unsigned long long)progress.updated,
        (unsigned long long)progress.unchanged,(unsigned long long)progress.removed,
        (unsigned long long)progress.visited,(unsigned long long)progress.errors,
        (unsigned long long)progress.recoverable_errors,(unsigned long long)progress.fatal_errors,published?"true":"false",
        (unsigned long long)snapshot.sequence,resident_peak(usage.ru_maxrss),genres,
        (unsigned long long)progress.directories,(unsigned long long)progress.metadata_parses,
        (unsigned long long)statistics.row_inserts,(unsigned long long)statistics.row_updates,
        (unsigned long long)statistics.row_deletes,(unsigned long long)statistics.track_updates,
        (unsigned long long)(published?(uint64_t)st.st_size+192:0),progress.parser_peak_rss_kib,
        (unsigned long long)((uint64_t)(usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)*1000+
            (usage.ru_utime.tv_usec+usage.ru_stime.tv_usec)/1000),
        (unsigned long long)statistics.sql_inserts,(unsigned long long)statistics.sql_updates,
        (unsigned long long)statistics.sql_deletes,(unsigned long long)statistics.temporary_writes,
        (unsigned long long)((uint64_t)(children.ru_utime.tv_sec+children.ru_stime.tv_sec)*1000+
            (children.ru_utime.tv_usec+children.ru_stime.tv_usec)/1000),
        browse_counts[0],browse_counts[1],browse_counts[2],browse_counts[3],browse_counts[4],track_ids,snapshot.damaged?"true":"false",progress.state);
    ok=1;
done:
    if(scanner){while(sb_music_scan_stop(scanner)==SB_SCAN_BUSY)pause_ms();while(sb_music_scan_destroy(scanner)==SB_SCAN_BUSY)pause_ms();}
    if(library){if(opened){request=(sb_ml_request){.operation=SB_ML_CLOSE};exchange(library,&request,&result);}sb_music_library_destroy(library);}
    if(fd>=0)close(fd);
    sb_music_snapshot_close(&snapshot);sb_storage_root_close(&root);
    if(work_created){if(db[0]){char journal[280];snprintf(journal,sizeof journal,"%s-journal",db);unlink(journal);unlink(db);}rmdir(work);}
    if(!ok)fprintf(stderr,"library: %s failed (errno %d: %s); persistent snapshots and source media preserved\n",phase,errno,strerror(errno));
    job_result=ok?0:2;return job_result;
}
