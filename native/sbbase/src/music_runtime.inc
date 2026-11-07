/* Explicit production test-session bootstrap. Never registers mock adapters. */
#include "music_scanner.h"
#include "music_snapshot.h"
#include "music_snapshot_view.h"
#include <sys/statfs.h>
#include <sys/resource.h>
#include <limits.h>
#include <dirent.h>

static struct {
	const char *root, *database, *helper, *store, *source_id;
	char store_work[128], store_path[256];
	unsigned database_mib;
	sb_music_snapshot_view view;
	int resource_fd, scan, scan_only, open_existing, initialize, check_only, database_opened;
	unsigned port;
	sb_storage_root media_root, db_root;
	sb_storage_model *model;
	sb_storage_worker *worker;
	sb_storage_discovery *discovery;
	uint64_t due, sequence;
	int pending, revoked;
} music_runtime = {
	.resource_fd = -1, .media_root = {.fd = -1}, .db_root = {.fd = -1}};

static int music_runtime_argument(int argc, char **argv, int *index)
{
	const char *flag = argv[*index];
	if(!strcmp(flag,"--music-open-check")){if(music_runtime.check_only)return -1;music_runtime.check_only=1;return 1;}
	if (!strcmp(flag,"--music-open") || !strcmp(flag,"--music-init")) {
		int *mode=!strcmp(flag,"--music-open")?&music_runtime.open_existing:&music_runtime.initialize;
		if(*mode)return -1;
		*mode=1;return 1;
	}
	if (!strcmp(flag, "--music-idle-only")) {
		if (music_idle_only) return -1;
		music_idle_only = 1; return 1;
	}
 if(!strcmp(flag,"--music-browse-only")){
  if(music_browse_only)return -1;
  music_browse_only=1;return 1;
 }
	if (!strcmp(flag, "--music-scan-only")) {
		if (music_runtime.scan_only) return -1;
		music_runtime.scan_only = 1;
		return 1;
	}
	if (!strcmp(flag, "--music-scan")) {
		if (music_runtime.scan)
			return -1;
		music_runtime.scan = 1;
		return 1;
	}
	const char **target = NULL;
	if (!strcmp(flag, "--music-root"))
		target = &music_runtime.root;
	else if (!strcmp(flag, "--music-database"))
		target = &music_runtime.database;
	else if (!strcmp(flag, "--music-helper"))
		target = &music_runtime.helper;
	else if (!strcmp(flag,"--music-store"))
		target = &music_runtime.store;
	else if (!strcmp(flag,"--music-source-id"))
		target = &music_runtime.source_id;
	else if (strcmp(flag, "--music-resource-fd") &&
		 strcmp(flag, "--music-http-port") && strcmp(flag,"--music-db-limit-mib"))
		return 0;
	if (*index + 1 >= argc)
		return -1;
	const char *value = argv[++*index];
	if (target) {
		if (*target || !*value)
			return -1;
		*target = value;
		return 1;
	}
	uint64_t n;
	if (!playback_number(value, INT_MAX, &n))
		return -1;
	if (!strcmp(flag,"--music-db-limit-mib")) {
		if(!n||n>64||music_runtime.database_mib)return -1;
		music_runtime.database_mib=(unsigned)n;
	} else if (!strcmp(flag, "--music-resource-fd")) {
		if (n < 3 || music_runtime.resource_fd >= 0)
			return -1;
		music_runtime.resource_fd = (int)n;
	} else {
		if (!n || n > 65535 || music_runtime.port)
			return -1;
		music_runtime.port = (unsigned)n;
	}
	return 1;
}

static int runtime_wait(sb_ml_result *result)
{
	uint64_t until = np_monotonic_ms() + 5000;
	sb_ml_status received;
	while ((received = sb_music_library_receive(music_library, result)) == SB_ML_BUSY) {
		if (!running || np_monotonic_ms() >= until)
			return 0;
		struct timespec pause = {0, 1000000};
		nanosleep(&pause, NULL);
	}
	return received == SB_ML_OK && result->status == SB_ML_OK;
}

static int runtime_store_roots(void)
{
 /* Snapshots serve browse-only catalogues and idle-gated playback. Idle-only
  * constructs the playback controller without scanning or autoplay; every
  * snapshot invariant below holds identically for both modes. */
 if(!(music_browse_only||music_idle_only)||music_runtime.scan||music_runtime.initialize||music_runtime.database||
    music_runtime.source_id||!music_runtime.root)return 0;
 sb_storage_mounts *mounts=calloc(1,sizeof *mounts);if(!mounts)return 0;
 if(sb_storage_read_mounts(NULL,mounts)!=SB_STORAGE_OK){free(mounts);return 0;}
 sb_storage_mount mount={0};
 for(unsigned i=0;i<mounts->count;i++)if(!strcmp(mounts->rows[i].path,music_runtime.root))mount=mounts->rows[i];
 free(mounts);
 sb_storage_evidence evidence;
 if(!mount.mount_id||sb_storage_identity_linux(&mount,&evidence)!=SB_STORAGE_OK||
    !sb_music_snapshot_source(&evidence,&music_media_snapshot.source))return 0;
 music_media_snapshot.generation=mount.mount_id;music_media_snapshot.online=1;
 if(sb_storage_validate("/media",&mount,&music_media_snapshot,NULL,NULL,&music_runtime.media_root)!=SB_STORAGE_OK||
    sb_storage_root_noatime(&music_runtime.media_root)!=SB_STORAGE_OK)return 0;
 sb_music_snapshot snapshot;
 uint64_t maximum=(uint64_t)(music_runtime.database_mib?music_runtime.database_mib:32)*1048576;
 if(!sb_music_snapshot_open(&snapshot,&music_runtime.media_root,music_runtime.store,0,maximum,NULL,NULL))return 0;
 struct statfs fs;
 if(statfs("/dev",&fs)||fs.f_type!=0x01021994){sb_music_snapshot_close(&snapshot);return 0;}
 strcpy(music_runtime.store_work,"/dev/sbmusic-browse.XXXXXX");
 int ok=mkdtemp(music_runtime.store_work)!=NULL;
 int created=ok;
 if(ok)ok=!statfs(music_runtime.store_work,&fs)&&fs.f_type==0x01021994;
 if(ok)snprintf(music_runtime.store_path,sizeof music_runtime.store_path,"%s/library.db",music_runtime.store_work);
 int fd=ok?open(music_runtime.store_path,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600):-1;
 if(fd<0)ok=0;
 if(ok)ok=sb_music_snapshot_load(&snapshot,fd)==1;
 if(ok&&snapshot.damaged)ok=0; /* explicit recovery before serving a degraded store */
 if(ok){
  music_runtime.view.initial_root=music_runtime.media_root;
  snprintf(music_runtime.view.directory,sizeof music_runtime.view.directory,"%s",music_runtime.store);
  snprintf(music_runtime.view.work,sizeof music_runtime.view.work,"%s",music_runtime.store_work);
  music_runtime.view.maximum_bytes=maximum;music_runtime.view.sequence=snapshot.sequence;
  music_runtime.view.generation=music_media_snapshot.generation;
 }
 if(fd>=0)close(fd);
 sb_music_snapshot_close(&snapshot);
 if(!ok){
  if(created){if(music_runtime.store_path[0])unlink(music_runtime.store_path);rmdir(music_runtime.store_work);}
  music_runtime.store_work[0]=music_runtime.store_path[0]=0;return 0;
 }
 /* Browse-only never opens media files, so it drops the descriptor after the
  * snapshot is extracted. Idle-gated playback resolves track files through
  * this same validated root; shutdown still closes it. */
 if(music_browse_only)sb_storage_root_close(&music_runtime.media_root);
 music_runtime.view.initial_root.fd=-1; /* only identity/mount/scope metadata retained */
 music_runtime.database=music_runtime.store_path;
 music_runtime.open_existing=1;
 return 1;
}

static int runtime_roots(void)
{
	if(music_runtime.store)return runtime_store_roots();
	const char *db = music_runtime.database, *selected = music_runtime.root;
	if (!db || (strcmp(db, "/dev/standalone-music-test/library.db") &&
	    !(music_idle_only && !strcmp(db,"/dev/standalone-music-test/library-wp4a3b.db"))) ||
	    !selected || strncmp(selected, "/media/", 7) ||
	    (!music_runtime.helper && music_runtime.scan) ||
	    (!music_runtime.scan && !music_runtime.open_existing && !music_runtime.initialize))
		return 0;
	struct stat st;
	if(music_runtime.open_existing) {
		if(lstat(db,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||
		   (st.st_mode&0022)||st.st_nlink!=1||st.st_size<4096)return 0;
	} else if (!lstat(db, &st) || errno != ENOENT)
		return 0; /* creation is explicit; never replace an existing DB */
	char parent[512];
	snprintf(parent, sizeof parent, "%s", db);
	*strrchr(parent, '/') = 0;
	if (lstat(parent, &st) || !S_ISDIR(st.st_mode) ||
	    st.st_uid != geteuid() || (st.st_mode & 0777) != 0700)
		return 0;
	unsigned char entropy[32];
	if (!sb_music_resource_entropy(entropy))
		return 0;
	uint64_t incarnation = 0;
	memcpy(&incarnation, entropy, sizeof incarnation);
	if (!incarnation)
		incarnation = 1;
	music_runtime.model = calloc(1, sizeof *music_runtime.model);
	sb_storage_mounts *mounts = calloc(1, sizeof *mounts);
	if (!music_runtime.model || !mounts) {
		free(mounts);
		return 0;
	}
	sb_storage_init(music_runtime.model, 1, 1, incarnation);
	if (sb_storage_read_mounts(NULL, mounts) != SB_STORAGE_OK) {
		free(mounts);
		return 0;
	}
	sb_storage_mount media = {0}, ram = {0};
	for (unsigned i = 0; i < mounts->count; i++) {
		sb_storage_mount *m = &mounts->rows[i];
		size_t length = strlen(m->path);
		if (!strcmp(m->path, parent))
			ram = *m;
		if (!strncmp(m->path, "/media/", 7) &&
		    !strncmp(selected, m->path, length) &&
		    selected[length] == '/' && length > strlen(media.path))
			media = *m;
	}
	free(mounts);
	if (!media.mount_id || !ram.mount_id || strcmp(ram.fs, "tmpfs") ||
	    ram.read_only)
		return 0;
	sb_storage_event event = {0};
	sb_storage_change change;
	event.kind = SB_STORAGE_INSERT;
	event.slot = 0;
	event.sequence = 1;
	event.mount = media;
	event.type = SB_STORAGE_USB;
	(void)sb_storage_identity_linux(&media, &event.evidence);
	if (sb_storage_apply(music_runtime.model, &event, &change) !=
	    SB_STORAGE_OK)
		return 0;
	/* The historical disposable RAM catalogue used a session identity. Recovery
	 * must explicitly pin its recorded source; never guess from names/tags. */
	if(music_runtime.source_id) {
		if(!music_runtime.open_existing||strlen(music_runtime.source_id)!=32)return 0;
		unsigned nonzero=0;
		for(unsigned i=0;i<16;i++) {
			unsigned byte;char pair[3]={music_runtime.source_id[2*i],music_runtime.source_id[2*i+1],0};
			for(unsigned j=0;j<2;j++)if(!((pair[j]>='0'&&pair[j]<='9')||(pair[j]>='a'&&pair[j]<='f')))return 0;
			if(sscanf(pair,"%2x",&byte)!=1)return 0;
			music_runtime.model->media[0].media.source.bytes[i]=(unsigned char)byte;nonzero|=byte;
		}
		if(!nonzero)return 0;
	}
	sb_music_media provisional = music_runtime.model->media[0].media;
	provisional.online = 1;
	sb_storage_root whole = {.fd = -1};
	if (sb_storage_validate(
		    "/media", &media, &provisional, NULL, NULL, &whole) !=
	    SB_STORAGE_OK)
		return 0;
	if (sb_storage_root_noatime(&whole) != SB_STORAGE_OK) {
		sb_storage_root_close(&whole);
		return 0;
	}
	sb_storage_status scoped =
		sb_storage_root_child(&whole,
				      &provisional,
				      selected + strlen(media.path) + 1,
				      NULL,
				      NULL,
				      &music_runtime.media_root);
	sb_storage_root_close(&whole);
	if (scoped != SB_STORAGE_OK)
		return 0;
	event.kind = SB_STORAGE_VALIDATED;
	event.sequence = 2;
	event.expected_generation = provisional.generation;
	if (sb_storage_apply(music_runtime.model, &event, &change) !=
	    SB_STORAGE_OK)
		return 0;
	music_media_snapshot = music_runtime.model->media[0].media;
	if (sb_storage_validate("/dev",
				&ram,
				&music_media_snapshot,
				NULL,
				NULL,
				&music_runtime.db_root) != SB_STORAGE_OK)
		return 0;
	struct statfs fs;
	if (fstatfs(music_runtime.db_root.fd, &fs) || fs.f_type != 0x01021994 ||
	    fs.f_bsize <= 0 ||
	    (uint64_t)fs.f_blocks >
		    UINT64_C(8388608) / (unsigned long)fs.f_bsize)
		return 0;
	if (music_runtime.scan && (lstat(music_runtime.helper, &st) || !S_ISREG(st.st_mode) ||
	    st.st_uid != geteuid() || (st.st_mode & 0022) ||
	    !(st.st_mode & 0100)))
		return 0;
	return 1;
}

/* Startup-only evidence. Not decoder proof, not a disappearance shortcut.
 * GUI/service restart fencing must independently precede idle acknowledgement.
 */
static unsigned runtime_native_absence(void)
{
	DIR *proc = opendir("/proc");
	if (!proc)
		return 0;
	unsigned absent = (1u << SB_OWNER_SPOTIFY) | (1u << SB_OWNER_QOBUZ),
		 count = 0;
	struct dirent *entry;
	while ((entry = readdir(proc)) != NULL) {
		if (++count > 4096) {
			absent = 0;
			break;
		}
		if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
			continue;
		char path[128], line[256], name[64];
		if (snprintf(path,
			     sizeof path,
			     "/proc/%s/status",
			     entry->d_name) >= (int)sizeof path) {
			absent = 0;
			break;
		}
		FILE *f = fopen(path, "r");
		if (!f) {
			if (errno == ENOENT)
				continue;
			absent = 0;
			break;
		}
		int good = fgets(line, sizeof line, f) != NULL;
		fclose(f);
		if (!good || sscanf(line, "Name: %63s", name) != 1) {
			absent = 0;
			break;
		}
		if (!strncmp(name, "spotify-super", 13) ||
		    !strcmp(name, "librespot") ||
		    !strncmp(name, "ogg-http-bridge", 15))
			absent &= ~(1u << SB_OWNER_SPOTIFY);
		if (!strncmp(name, "qobuzconnectd", 12))
			absent &= ~(1u << SB_OWNER_QOBUZ);
	}
	closedir(proc);
	return absent;
}

/* Shared by serving startup and the listener-free recovery qualification. */
static int runtime_open_library(void)
{
 sb_ml_options options={0};
 snprintf(options.path,sizeof options.path,"%s",music_runtime.database);
 options.create=!music_runtime.open_existing;options.read_only=music_runtime.open_existing;
 options.single_source=1;
 if(music_runtime.store){
  options.maximum_bytes=(uint64_t)(music_runtime.database_mib?music_runtime.database_mib:32)*1048576;
  options.refresh=sb_music_snapshot_refresh;options.refresh_context=&music_runtime.view;
 }
 options.busy_ms=20;options.query_ms=2000;
 music_library=sb_music_library_start(&options);
 sb_ml_result result;
 if(!music_library||!runtime_wait(&result))return 0;
 music_runtime.database_opened=1;
 sb_ml_request source={.operation=SB_ML_SOURCE};source.data.media=music_media_snapshot;
 return sb_music_library_exchange(music_library,&source,&result)==SB_ML_OK&&result.status==SB_ML_OK;
}

static int runtime_open_check(void)
{
 music_runtime.open_existing=1;
 int ok=runtime_roots()&&runtime_open_library();
 sb_ml_result result;unsigned tracks=0,albums=0,artists=0;
 if(ok){sb_ml_request integrity={.operation=SB_ML_INTEGRITY};ok=sb_music_library_exchange(music_library,&integrity,&result)==SB_ML_OK;}
 if(ok){sb_ml_request summary={.operation=SB_ML_BROWSE_SUMMARY};summary.data.query.media=music_media_snapshot;summary.data.query.limit=1;
  ok=sb_music_library_exchange(music_library,&summary,&result)==SB_ML_OK;
  if(ok){tracks=result.data.summary.tracks;albums=result.data.summary.albums;artists=result.data.summary.artists;}
 }
 uint64_t revision=ok?result.revision:0;
 if(music_library){
  if(music_runtime.database_opened){sb_ml_request close_request={.operation=SB_ML_CLOSE};
   if(sb_music_library_exchange(music_library,&close_request,&result)!=SB_ML_OK)ok=0;
   music_runtime.database_opened=0;}
  sb_ml_status destroyed;uint64_t until=np_monotonic_ms()+5000;
  while((destroyed=sb_music_library_destroy(music_library))==SB_ML_BUSY&&np_monotonic_ms()<until){struct timespec pause={0,1000000};nanosleep(&pause,NULL);}
  if(destroyed!=SB_ML_OK)ok=0;else music_library=NULL;
 }
 if(ok)printf("{\"mode\":\"read-only-open-check\",\"tracks\":%u,\"albums\":%u,\"artists\":%u,\"revision\":%llu,\"scan_started\":false,\"listeners_started\":false}\n",
  tracks,albums,artists,(unsigned long long)revision);
 else fputs("music: read-only existing-library qualification failed\n",stderr);
 return ok;
}

static int music_runtime_start(void)
{
	/* Normal browse startup opens the existing catalogue and never implies scan. */
	if(music_browse_only&&!music_runtime.scan&&!music_runtime.initialize)
		music_runtime.open_existing=1;
	if((music_runtime.open_existing&&(music_runtime.scan||music_runtime.initialize))||
	   (music_runtime.initialize&&!music_browse_only)||
	   (music_runtime.store&&!(music_browse_only||music_idle_only)))return 0;
 /* Verify before roots, database opening, scan and resource adoption. */
 if(!sb_ts_music_allowed(tinysc_lifecycle)){
  fputs("music: verified TinySC arbitration required\n",stderr);return 0;
 }
	if (!music_runtime.scan_only && !music_browse_only && music_runtime.resource_fd < 0) {
		/* Separately supervised webserver: rendezvous instead of an
		 * inherited descriptor. Absence fails startup; the supervisor
		 * ladder degrades to the plain core, never to a half-wired
		 * playback controller. */
		int conn = sb_resource_rendezvous_connect(12000);
		int pair[2] = {-1, -1};
		if (conn >= 0 && sb_resource_pair(pair) &&
		    sb_resource_push_fd(conn, pair[1]) &&
		    sb_resource_adopt(pair[0])) {
			close(pair[1]);
			music_runtime.resource_fd = pair[0];
		} else {
			if (pair[0] >= 0)
				close(pair[0]);
			if (pair[1] >= 0)
				close(pair[1]);
		}
		if (conn >= 0)
			close(conn);
		if (music_runtime.resource_fd < 0) {
			fputs("music: resource rendezvous unavailable\n", stderr);
			return 0;
		}
	}
	if ((!music_runtime.scan_only && !music_browse_only && music_runtime.resource_fd >= 0 &&
	    !sb_resource_adopt(music_runtime.resource_fd)) ||
	    (!music_browse_only && !music_runtime.port) ||
     (music_browse_only && music_runtime.resource_fd >= 0) || !runtime_roots()) {
		fputs("music: explicit validated USB scope and private <=8MiB "
		      "tmpfs DB required\n",
		      stderr);
		return 0;
	}
	if(!sb_ts_music_allowed(tinysc_lifecycle)||!runtime_open_library()||!sb_ts_music_allowed(tinysc_lifecycle))return 0;
	sb_ml_result result;
	if(music_runtime.scan) {
	sb_scan_options scan = {0};
	scan.enabled = scan.approved_database = 1;
	scan.library = music_library;
	scan.root = music_runtime.media_root;
	scan.helper_timeout_ms = 5000;
	scan.batch_files = 4;
	scan.work_duty_percent = 25;
	scan.yield_ms = 10;
	if (strlen(music_runtime.helper) >= sizeof scan.helper)
		return 0;
	strcpy(scan.helper, music_runtime.helper);
	sb_scan_code code;
	sb_music_scanner *scanner = sb_music_scan_create(&scan, &code);
	if (!scanner)
		return 0;
	uint64_t queue_until=np_monotonic_ms()+5000;
	code=SB_SCAN_BUSY;
	while(code==SB_SCAN_BUSY&&running&&np_monotonic_ms()<queue_until&&sb_ts_music_allowed(tinysc_lifecycle)){
		code=sb_music_scan_request(scanner,1,1);if(code==SB_SCAN_BUSY){struct timespec pause={0,1000000};nanosleep(&pause,NULL);}}
	if (!sb_ts_music_allowed(tinysc_lifecycle) || code != SB_SCAN_OK) {
		(void)sb_music_scan_stop(scanner);
		(void)sb_music_scan_destroy(scanner);
		return 0;
	}
	sb_scan_status progress = {0};
 clock_t scan_cpu=clock();
 struct rusage children_before={0},children_after={0};
 (void)getrusage(RUSAGE_CHILDREN,&children_before);
	uint64_t until = np_monotonic_ms() + 120000;
	do {
		if (!running || !sb_ts_music_allowed(tinysc_lifecycle) || np_monotonic_ms() >= until) {
			sb_music_scan_cancel(scanner, 1);
			progress.state = SB_SCAN_CANCELLED;
			break;
		}
		(void)sb_music_scan_status(scanner, &progress);
		struct timespec pause = {0, 10000000};
		nanosleep(&pause, NULL);
	} while (progress.state < SB_SCAN_COMPLETED);
	fprintf(stderr,
		"MUSIC_SCAN state=%u files=%" PRIu64 " tracks=%" PRIu64
		" errors=%" PRIu64 " ms=%" PRIu64 " helper_rss_kib=%u\n",
		progress.state,
		progress.visited,
		progress.added,
		progress.errors,
		progress.elapsed_ms,
		progress.parser_peak_rss_kib);
	while (sb_music_scan_stop(scanner) == SB_SCAN_BUSY) {
		struct timespec pause = {0, 1000000};
		nanosleep(&pause, NULL);
	}
	while (sb_music_scan_destroy(scanner) == SB_SCAN_BUSY) {
		struct timespec pause = {0, 1000000};
		nanosleep(&pause, NULL);
	}
 (void)getrusage(RUSAGE_CHILDREN,&children_after);
 uint64_t child_before_ms=(uint64_t)(children_before.ru_utime.tv_sec+children_before.ru_stime.tv_sec)*1000+
  (uint64_t)(children_before.ru_utime.tv_usec+children_before.ru_stime.tv_usec)/1000;
 uint64_t child_after_ms=(uint64_t)(children_after.ru_utime.tv_sec+children_after.ru_stime.tv_sec)*1000+
  (uint64_t)(children_after.ru_utime.tv_usec+children_after.ru_stime.tv_usec)/1000;
 fprintf(stderr,"MUSIC_SCAN_CPU base_ms=%llu children_ms=%llu\n",
  (unsigned long long)((uint64_t)(clock()-scan_cpu)*1000/CLOCKS_PER_SEC),
  (unsigned long long)(child_after_ms-child_before_ms));
	if (progress.state != SB_SCAN_COMPLETED || progress.errors)
		return 0;
	}
 if(!sb_ts_music_allowed(tinysc_lifecycle))return 0;
 if(music_browse_only || music_idle_only){
  sb_ml_request verify={0};verify.operation=SB_ML_INTEGRITY;
  if(sb_music_library_exchange(music_library,&verify,&result)!=SB_ML_OK||result.status!=SB_ML_OK)return 0;
  fputs("MUSIC_INTEGRITY ok; same SQLite worker, delete journal\n",stderr);
  verify.operation=SB_ML_STATS;
  if(sb_music_library_exchange(music_library,&verify,&result)!=SB_ML_OK||result.status!=SB_ML_OK)return 0;
  fprintf(stderr,"MUSIC_LIBRARY bytes=%llu revision=%llu journal=%.15s\n",
   (unsigned long long)result.data.stats.db_bytes,(unsigned long long)result.data.stats.revision,result.data.stats.journal);
 }
	if (music_runtime.scan_only) return 1;
	music_browse =
		sb_browse_start(music_library, &music_media_snapshot, 1, 1);
	if (!music_browse)
		return 0;
	if(music_runtime.store)sb_browse_snapshot_refresh(music_browse,1);
	if (music_idle_only) sb_browse_single_track(music_browse, 1);
 if(!sb_ts_music_allowed(tinysc_lifecycle))return 0;
 if(!music_browse_only){
	local_music = sb_playback_create(1,
					 sb_browse_dispatcher(music_browse),
					 &music_runtime.media_root,
					 &production_owner,
					 nowplaying,
					 music_runtime.resource_fd,
					 music_runtime.port,
					 playback_send,
					 NULL);
	if (!local_music)
		return 0;
	if (music_idle_only && !sb_playback_single_mp3(local_music))
		return 0;
	if (!sb_playback_probe_resources(local_music, np_monotonic_ms()))
		return 0;
	sb_ob_bind_music(&ownership_bridge,
			 sb_playback_bridge_proof,
			 sb_playback_bridge_begin,
			 sb_playback_bridge_ready,
			 local_music);
 }
	if(!music_runtime.store){
	sb_storage_worker_options worker = {0};
	worker.enabled = worker.fab4 = 1;
	strcpy(worker.approved_parent, "/media");
	music_runtime.discovery = calloc(1, sizeof *music_runtime.discovery);
	if (!music_runtime.discovery)
		return 0;
	music_runtime.worker = sb_storage_worker_start(&worker);
	if (!music_runtime.worker)
		return 0;
	music_runtime.due = np_monotonic_ms();
	}
 if(!music_browse_only){
	ownership_bridge.unavailable_mask = (1u << SB_OWNER_RADIO) |
					    (1u << SB_OWNER_SPOTIFY) |
					    (1u << SB_OWNER_QOBUZ);
	ownership_bridge.startup_native_absent_mask = runtime_native_absence();
	ownership_bridge.idle_only = music_idle_only;
 }
	if(music_browse_only)fputs("MUSIC_BROWSE_ONLY ready; playback/ownership/resources disabled\n",stderr);
	fputs("MUSIC_RUNTIME ready; no ownership inferred, no autoplay\n",
	      stderr);
	return 1;
}

static void music_runtime_tick(void)
{
	/* Snapshot validation/reopen/removal is serialized on the DB worker. */
	if(music_runtime.store){
		if(!music_runtime.revoked&&!sb_ts_music_allowed(tinysc_lifecycle)){
			music_runtime.revoked=1;sb_browse_inhibit(music_browse);
			fputs("MUSIC_STORE TinySC lifecycle revoked; browsing inhibited\n",stderr);
		}
		return;
	}
	if (!music_runtime.worker || music_runtime.revoked)
		return;
 if(!sb_ts_music_allowed(tinysc_lifecycle)){
  music_runtime.revoked=1;music_media_snapshot.online=0;
  ownership_bridge.admission_holds|=1u<<31;
  sb_playback_media_removed(local_music,&music_media_snapshot);
  sb_browse_media(music_browse,&music_media_snapshot,0);
  fputs("MUSIC_RUNTIME TinySC lifecycle revoked; no automatic resume\n",stderr);
  return;
 }
	uint64_t now = np_monotonic_ms();
	if (!music_runtime.pending && now >= music_runtime.due) {
		if (sb_storage_worker_submit(music_runtime.worker,
					     ++music_runtime.sequence) ==
		    SB_STORAGE_OK)
			music_runtime.pending = 1;
	}
	if (!music_runtime.pending)
		return;
	sb_storage_discovery *discovery = music_runtime.discovery;
	sb_storage_status got =
		sb_storage_worker_receive(music_runtime.worker, discovery);
	if (got == SB_STORAGE_BUSY)
		return;
	music_runtime.pending = 0;
	int current = 0;
	if (got == SB_STORAGE_OK && discovery->status == SB_STORAGE_OK)
		for (unsigned i = 0; i < discovery->mounts.count; i++) {
			sb_storage_mount *m = &discovery->mounts.rows[i];
			if (m->mount_id ==
				    music_runtime.media_root.mount.mount_id &&
			    m->major == music_runtime.media_root.mount.major &&
			    m->minor == music_runtime.media_root.mount.minor &&
			    !strcmp(m->path,
				    music_runtime.media_root.mount.path) &&
			    discovery->validation[i] == SB_STORAGE_OK)
				current = 1;
		}
	if (!current) {
		music_runtime.revoked = 1;
		music_media_snapshot.online = 0;
		ownership_bridge.admission_holds |= 1u << 31;
		sb_playback_media_removed(local_music, &music_media_snapshot);
		sb_browse_media(music_browse, &music_media_snapshot, 0);
		fputs("MUSIC_RUNTIME media revoked; no automatic reinsertion "
		      "or rescan\n",
		      stderr);
	}
	sb_queue_status q = sb_queue_snapshot(sb_playback_queue(local_music));
	music_runtime.due =
		now + ((q.mode == SB_Q_PLAYING || q.mode == SB_Q_BUFFERING)
			       ? 1000
			       : 30000);
}

static void music_runtime_stop(void)
{
	if (music_runtime.worker) {
		while (sb_storage_worker_stop(music_runtime.worker) ==
		       SB_STORAGE_BUSY) {
			struct timespec pause = {0, 1000000};
			nanosleep(&pause, NULL);
		}
		while (sb_storage_worker_destroy(music_runtime.worker) ==
		       SB_STORAGE_BUSY) {
			struct timespec pause = {0, 1000000};
			nanosleep(&pause, NULL);
		}
	}
	sb_storage_root_close(&music_runtime.media_root);
	sb_storage_root_close(&music_runtime.db_root);
	free(music_runtime.model);
	free(music_runtime.discovery);
	if(music_runtime.store_work[0]){
		if(music_runtime.store_path[0])unlink(music_runtime.store_path);
		char next[256];snprintf(next,sizeof next,"%s/library.next",music_runtime.store_work);unlink(next);
		rmdir(music_runtime.store_work);
	}
}
