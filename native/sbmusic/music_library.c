#define _POSIX_C_SOURCE 200809L
#include "music_library.h"
#include "schema.h"
#include "third_party/sqlite/sqlite3.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct { int type;
 sqlite3_int64 number;
 const void *data;
 } binding;
#define I(v) (binding){0,(sqlite3_int64)(v),NULL}
#define T(v) (binding){1,0,(v)}
#define B(v) (binding){2,0,(v)}
static pthread_once_t memory_policy_once=PTHREAD_ONCE_INIT;
static void memory_policy(void)
{
	/* Shared by this vendored SQLite instance, never raise a stricter cap. */
	sqlite3_int64 limit=sqlite3_hard_heap_limit64(-1);
	if(!limit||limit>8*1024*1024)sqlite3_hard_heap_limit64(8*1024*1024);
}

typedef struct {
	sb_music_media state;
	sqlite3_int64 id;
} media_slot;

struct sb_music_library {
	pthread_t thread;
	pthread_mutex_t lock, broker;
	pthread_cond_t condition;
	int pipe[2], pending, ready, exited, cancel, browse_attached;
	uint64_t browse_until;
	sb_ml_options options;
	sb_ml_request request;
	sb_ml_result result;
	sqlite3 *db;
	/* exclusively used by worker */
	media_slot media[16];
	unsigned media_count;
	uint64_t revision, data_revision, pending_revision, transaction,
		deadline, last_request;
	int dirty, unavailable;
 uint64_t scan_epoch,scan_start_revision; sb_music_media scan_media;
 uint64_t row_inserts,row_updates,row_deletes,track_updates;
 uint64_t sql_inserts,sql_updates,sql_deletes,temporary_writes;
};
static uint64_t milliseconds(void)
{
	struct timespec t;
	 clock_gettime(CLOCK_MONOTONIC,&t);
	return (uint64_t)t.tv_sec*1000+(unsigned long)t.tv_nsec/1000000;
}
static sb_ml_status failure(sb_music_library *h,sb_ml_status status,const char *message)
{
	h->result.status=status;
	snprintf(h->result.error,sizeof h->result.error,"%s",message);
	return status;
}
static int sql_error(sb_music_library *h,int rc)
{
	int code=rc&255;
	sb_ml_status status=code==SQLITE_BUSY||code==SQLITE_LOCKED?SB_ML_BUSY:
		code==SQLITE_NOMEM||code==SQLITE_TOOBIG?SB_ML_LIMIT:
		code==SQLITE_READONLY?SB_ML_READ_ONLY:code==SQLITE_CORRUPT||code==SQLITE_NOTADB?SB_ML_CORRUPT:
		code==SQLITE_CONSTRAINT?SB_ML_CONFLICT:code==SQLITE_INTERRUPT?SB_ML_CANCELLED:
		code==SQLITE_CANTOPEN||code==SQLITE_IOERR||code==SQLITE_FULL?SB_ML_IO:SB_ML_INTERNAL;
	h->result.sqlite_code=rc;
	if(code==SQLITE_IOERR){if(!h->unavailable&&h->revision<INT64_MAX-1)h->revision++;
	h->unavailable=1;
	for(unsigned i=0;i<h->media_count;i++)h->media[i].state.online=0;
	}
	failure(h,status,h->db?sqlite3_errmsg(h->db):"SQLite operation failed");
	return 0;
}
static sqlite3_stmt *prepare(sb_music_library *h,const char *sql,const binding *values,unsigned count)
{
	sqlite3_stmt *s=NULL;
	 int rc=sqlite3_prepare_v2(h->db,sql,-1,&s,NULL);
	if(rc!=SQLITE_OK){sql_error(h,rc);
	return NULL;
	}
	for(unsigned i=0;i<count;i++) {
		rc=values[i].type==0?sqlite3_bind_int64(s,(int)i+1,values[i].number):
			values[i].type==1?sqlite3_bind_text(s,(int)i+1,values[i].data,-1,SQLITE_TRANSIENT):
			sqlite3_bind_blob(s,(int)i+1,values[i].data,16,SQLITE_TRANSIENT);
		if(rc!=SQLITE_OK){sql_error(h,rc);
		sqlite3_finalize(s);
		return NULL;
		}
	}
	return s;
}
static int execute(sb_music_library *h,const char *sql,const binding *values,unsigned count)
{
	sqlite3_stmt *s=prepare(h,sql,values,count);
	if(!s)return 0;
	int rc;
	while((rc=sqlite3_step(s))==SQLITE_ROW){}
	int ok=rc==SQLITE_DONE;
	if(!ok)sql_error(h,rc);
	sqlite3_finalize(s);
	return ok;
}
static int script(sb_music_library *h,const char *sql)
{
	while(*sql) {
		sqlite3_stmt *s=NULL;
		const char *tail=NULL;
		int rc=sqlite3_prepare_v2(h->db,sql,-1,&s,&tail);
		if(rc!=SQLITE_OK)return sql_error(h,rc);
		if(s){while((rc=sqlite3_step(s))==SQLITE_ROW){}sqlite3_finalize(s);
		if(rc!=SQLITE_DONE)return sql_error(h,rc);
		}
		if(tail==sql)return 0;
		sql=tail;
	}
	return 1;
}
static int scalar(sb_music_library *,const char *,const binding *,unsigned,sqlite3_int64 *);
static int refresh_revision(sb_music_library *h)
{
 sqlite3_int64 revision=0;
 if(!scalar(h,"SELECT revision FROM library_meta WHERE id=1",NULL,0,&revision)){if(h->result.status==SB_ML_NOT_FOUND)failure(h,SB_ML_CORRUPT,"Missing library revision record");return 0;}
 if(revision<1||revision>=INT64_MAX){failure(h,SB_ML_CORRUPT,"Invalid library revision");return 0;}
 if(h->data_revision!=(uint64_t)revision){
  if(h->revision>=INT64_MAX-1){failure(h,SB_ML_LIMIT,"Revision exhausted");return 0;}
  h->revision=h->revision>=(uint64_t)revision?h->revision+1:(uint64_t)revision;
  h->data_revision=(uint64_t)revision;
 }
 return 1;
}
static int bump_revision(sb_music_library *h)
{
 if(h->revision>=INT64_MAX-1){failure(h,SB_ML_LIMIT,"Revision exhausted");return 0;}
 binding v[]={I(h->revision)};sqlite3_int64 revision;
 if(!scalar(h,"UPDATE library_meta SET revision=MAX(revision,?)+1 WHERE id=1 RETURNING revision",v,1,&revision))return 0;
 h->pending_revision=(uint64_t)revision;return 1;
}
static void revision_committed(sb_music_library *h)
{
 h->revision=h->pending_revision;h->data_revision=h->pending_revision;
}
static int name_id(sb_music_library *h,const char *table,sqlite3_int64 media,const char *name,sqlite3_int64 *id)
{
	char sql[256];
	binding values[]={I(media),T(name)};
	snprintf(sql,sizeof sql,"INSERT INTO %s(media_id,name) VALUES(?,?) ON CONFLICT DO NOTHING",table);
	if(!execute(h,sql,values,2))return 0;
	snprintf(sql,sizeof sql,"SELECT id FROM %s WHERE media_id=? AND name=?",table);
	return scalar(h,sql,values,2,id);
}
static int folder_id(sb_music_library *h,sqlite3_int64 media,const char *path,sqlite3_int64 *id)
{
	char partial[SB_ML_PATH_MAX];
	partial[0]=0;
	binding root[]={I(media)};
	if(!execute(h,"INSERT INTO folders(media_id,parent_id,path,name) VALUES(?,NULL,'','') ON CONFLICT DO NOTHING",root,1)||
	   !scalar(h,"SELECT id FROM folders WHERE media_id=? AND path=''",root,1,id))return 0;
	const char *cursor=path;
	while(*cursor) {
		const char *slash=strchr(cursor,'/');
		size_t length=slash?(size_t)(slash-path):strlen(path);
		memcpy(partial,path,length);
		partial[length]=0;
		char name[SB_ML_PATH_MAX];
		size_t bytes=slash?(size_t)(slash-cursor):strlen(cursor);
		memcpy(name,cursor,bytes);
		name[bytes]=0;
		binding values[]={I(media),I(*id),T(partial),T(name)};
		if(!execute(h,"INSERT INTO folders(media_id,parent_id,path,name) VALUES(?,?,?,?) ON CONFLICT DO NOTHING",values,4))return 0;
		binding key[]={I(media),T(partial)};
		if(!scalar(h,"SELECT id FROM folders WHERE media_id=? AND path=?",key,2,id))return 0;
		if(!slash)break;
		cursor=slash+1;
	}
	return 1;
}
static int source_update(sb_music_library *h,const sb_music_media *media)
{
	if(h->transaction){failure(h,SB_ML_CONFLICT,"Source transitions cannot occur inside metadata transactions");
	return 0;
	}
	if(!media->generation||media->generation>INT64_MAX||!sb_music_source_equal(&media->source,&media->source)){
		failure(h,SB_ML_INVALID,"Invalid source identity/generation");
		return 0;
	}
	unsigned slot;
	for(slot=0;slot<h->media_count;slot++)if(sb_music_source_equal(&media->source,&h->media[slot].state.source))break;
	if(slot==16){failure(h,SB_ML_BUSY,"Source slot limit reached");
	return 0;
	}
	if(slot<h->media_count) {
		const sb_music_media *old=&h->media[slot].state;
		if(media->generation<old->generation||(!old->online&&media->online&&media->generation==old->generation)){
			failure(h,SB_ML_STALE,"Reinsertion requires a new generation");
			return 0;
		}
		if(media->generation==old->generation&&!!media->online==!!old->online)return 1;
	}
	if(h->revision>=INT64_MAX-1){failure(h,SB_ML_INTERNAL,"Revision exhausted");
	return 0;
	}
	/* Removal must revoke availability without touching a vanished filesystem. */
	if(!media->online){h->media[slot].state=*media;
	if(slot==h->media_count){h->media[slot].id=0;
	h->media_count++;
	}h->revision++;
	return 1;
	}
	if(h->unavailable){failure(h,SB_ML_IO,"I/O failed; close and explicitly reopen validated storage");
	return 0;
	}
	sqlite3_int64 id=0;
	int source_changed=0;
	binding key[]={B(media->source.bytes)};
	if(h->options.single_source){
		sqlite3_int64 other=0,matching=0;
		if(!scalar(h,"SELECT count(*) FROM media WHERE source!=?",key,1,&other)||
		   !scalar(h,"SELECT count(*) FROM media WHERE source=?",key,1,&matching))return 0;
		if(other||(!matching&&!h->options.create)){failure(h,SB_ML_CONFLICT,"Selected-volume database source mismatch; original preserved");return 0;}
	}
	if(h->options.read_only) {
		if(!scalar(h,"SELECT id FROM media WHERE source=?",key,1,&id)){
			if(h->result.status==SB_ML_OK)failure(h,SB_ML_NOT_FOUND,"Source is absent from read-only library");
			return 0;
		}
	} else {
		binding values[]={B(media->source.bytes),I(media->generation),I(!!media->online)};
		sqlite3_int64 same=0;
		if(!scalar(h,"SELECT count(*) FROM media WHERE source=? AND generation=? AND online=?",values,3,&same))return 0;
		if(same){if(!scalar(h,"SELECT id FROM media WHERE source=?",key,1,&id))return 0;}
		else {
		if(!execute(h,"BEGIN IMMEDIATE",NULL,0))return 0;
		int ok=execute(h,"INSERT INTO media(source,generation,online) VALUES(?,?,?) ON CONFLICT(source) DO UPDATE SET generation=excluded.generation,online=excluded.online WHERE media.generation!=excluded.generation OR media.online!=excluded.online",values,3);
		if(ok)source_changed=sqlite3_changes(h->db)!=0;
		ok=ok&&(!source_changed||bump_revision(h))&&scalar(h,"SELECT id FROM media WHERE source=?",key,1,&id)&&execute(h,"COMMIT",NULL,0);
		if(!ok){execute(h,"ROLLBACK",NULL,0);
		return 0;
		}
		}
	}
	h->media[slot]=(media_slot){*media,id};
	if(slot==h->media_count)h->media_count++;
	if(source_changed)revision_committed(h);
	return 1;
}
static int upsert_track(sb_music_library *h,const sb_ml_track *track,sqlite3_int64 media)
{
	sqlite3_int64 artist=0,album=0,folder=0,artwork=0,id=0;
	char directory[SB_ML_PATH_MAX],key[8192];
	snprintf(directory,sizeof directory,"%s",track->path);
	char *slash=strrchr(directory,'/');
	if(slash)*slash=0;
	else directory[0]=0;
	if(!folder_id(h,media,directory,&folder))return 0;
	if(track->album_artist[0]&&!name_id(h,"contributors",media,track->album_artist,&artist))return 0;
	const char *album_title=track->album[0]?track->album:"(Unknown album)";
	if(track->album_key[0])snprintf(key,sizeof key,"key:%s",track->album_key);
	else snprintf(key,sizeof key,"auto:%zu:%s%zu:%s%zu:%s",strlen(directory),directory,strlen(album_title),album_title,strlen(track->album_artist),track->album_artist);
	binding av[]={I(media),T(key),T(album_title),I(artist),I(!!track->compilation),I(track->year)};
	if(!execute(h,"INSERT INTO albums(media_id,album_key,title,artist_id,compilation,year) VALUES(?,?,?,NULLIF(?,0),?,?) ON CONFLICT(media_id,album_key) DO UPDATE SET title=excluded.title,artist_id=excluded.artist_id,compilation=excluded.compilation,year=excluded.year",av,6))return 0;
	binding ak[]={I(media),T(key)};
	if(!scalar(h,"SELECT id FROM albums WHERE media_id=? AND album_key=?",ak,2,&album))return 0;
	if(track->artwork_ref[0]) {
		binding art[]={I(media),T(track->artwork_ref)};
		if(!execute(h,"INSERT INTO artwork(media_id,reference) VALUES(?,?) ON CONFLICT DO NOTHING",art,2)||!scalar(h,"SELECT id FROM artwork WHERE media_id=? AND reference=?",art,2,&artwork))return 0;
	}
	const char *title=track->title[0]?track->title:(strrchr(track->path,'/')?strrchr(track->path,'/')+1:track->path);
	if(track->identity.track_id) {
		binding old[]={I(media),I(track->identity.track_id)};
		if(!scalar(h,"SELECT id FROM tracks WHERE media_id=? AND id=?",old,2,&id)){
			if(h->result.status==SB_ML_OK)failure(h,SB_ML_NOT_FOUND,"Cannot rename/update an absent track ID");
			return 0;
		}
		binding rename[]={T(track->path),I(id)};
		if(!execute(h,"UPDATE tracks SET path=? WHERE id=?",rename,2))return 0;
	}
	binding tv[]={I(media),T(track->path),T(title),I(album),I(folder),I(track->disc),I(track->track),I(track->year),I(track->duration_ms),I(track->file_size),I(track->mtime),T(track->codec),I(track->sample_rate),I(track->channels),I(track->bitrate),I(track->audio_offset),I(track->audio_length),I(artwork),T(track->fingerprint),I(track->scan_epoch),I(track->bit_depth),I(track->track_total),I(track->disc_total),T(track->release_date),I(track->total_samples)};
	if(!execute(h,"INSERT INTO tracks(media_id,path,title,album_id,folder_id,disc,track,year,duration,size,mtime,codec,sample_rate,channels,bitrate,audio_offset,audio_length,artwork_id,fingerprint,scan_epoch,bit_depth,track_total,disc_total,release_date,total_samples) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,NULLIF(?,0),?,?,?,?,?,?,?) ON CONFLICT(media_id,path) DO UPDATE SET title=excluded.title,album_id=excluded.album_id,folder_id=excluded.folder_id,disc=excluded.disc,track=excluded.track,year=excluded.year,duration=excluded.duration,size=excluded.size,mtime=excluded.mtime,codec=excluded.codec,sample_rate=excluded.sample_rate,channels=excluded.channels,bitrate=excluded.bitrate,audio_offset=excluded.audio_offset,audio_length=excluded.audio_length,artwork_id=excluded.artwork_id,fingerprint=excluded.fingerprint,scan_epoch=excluded.scan_epoch,bit_depth=excluded.bit_depth,track_total=excluded.track_total,disc_total=excluded.disc_total,release_date=excluded.release_date,total_samples=excluded.total_samples",tv,25))return 0;
	binding tk[]={I(media),T(track->path)};
	if(!scalar(h,"SELECT id FROM tracks WHERE media_id=? AND path=?",tk,2,&id))return 0;
	binding ti[]={I(id)};
	if(!execute(h,"DELETE FROM track_contributors WHERE track_id=?",ti,1)||!execute(h,"DELETE FROM track_genres WHERE track_id=?",ti,1))return 0;
	for(unsigned i=0;i<track->artist_count;i++) {
		sqlite3_int64 contributor;
		if(!name_id(h,"contributors",media,track->artists[i],&contributor))return 0;
		binding link[]={I(id),I(contributor),I(i)};
		if(!execute(h,"INSERT INTO track_contributors VALUES(?,?,1,?) ON CONFLICT DO NOTHING",link,3))return 0;
	}
	if(artist) {binding link[]={I(id),I(artist)};
	if(!execute(h,"INSERT INTO track_contributors VALUES(?,?,2,0) ON CONFLICT DO NOTHING",link,2))return 0;
	}
	for(unsigned i=0;i<track->genre_count;i++) {
		sqlite3_int64 genre;
		if(!name_id(h,"genres",media,track->genres[i],&genre))return 0;
		binding link[]={I(id),I(genre)};
		if(!execute(h,"INSERT INTO track_genres VALUES(?,?) ON CONFLICT DO NOTHING",link,2))return 0;
	}
	h->result.record_id=(uint64_t)id;
	h->dirty=1;
	return 1;
}
static int scalar(sb_music_library *h,const char *sql,const binding *values,unsigned count,sqlite3_int64 *out)
{
	sqlite3_stmt *s=prepare(h,sql,values,count);
	if(!s)return 0;
	int rc=sqlite3_step(s);
	int ok=rc==SQLITE_ROW;
	if(ok)*out=sqlite3_column_int64(s,0);
	else if(rc!=SQLITE_DONE)sql_error(h,rc);
 else failure(h,SB_ML_NOT_FOUND,"Expected database record is absent");
	sqlite3_finalize(s);
	return ok;
}
static int text_valid(const char *s,size_t capacity)
{
	const unsigned char *p=(const unsigned char *)s;
	const char *end=memchr(s,0,capacity);
	if(!end)return 0;
	while(p<(const unsigned char *)end) {
		unsigned c=*p++;
		if(c<128){if(c<32)return 0;
		continue;
		}
		unsigned n=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:0;
		if(!n||(size_t)((const unsigned char *)end-p)<n)return 0;
		if((c==0xe0&&p[0]<0xa0)||(c==0xed&&p[0]>=0xa0)||(c==0xf0&&p[0]<0x90)||(c==0xf4&&p[0]>=0x90))return 0;
		while(n--)if((*p++&0xc0)!=0x80)return 0;
	}
	return 1;
}
static int path_valid(const char *path)
{
	if(!text_valid(path,SB_ML_PATH_MAX)||!path[0]||path[0]=='/'||strchr(path,'\\'))return 0;
	unsigned depth=0;
	const char *p=path;
	while(*p) {
		const char *slash=strchr(p,'/');
		size_t n=slash?(size_t)(slash-p):strlen(p);
		if(!n||(n==1&&p[0]=='.')||(n==2&&!memcmp(p,"..",2))||++depth>64)return 0;
		if(!slash)return 1;
		p=slash+1;
		if(!*p)return 0;
	}
	return 0;
}
int sb_music_library_track_valid(const sb_ml_track *t)
{
	if(!t)return 0;
	if(!path_valid(t->path)||!text_valid(t->release_date,sizeof t->release_date)||!text_valid(t->title,sizeof t->title)||!text_valid(t->album,sizeof t->album)||
	   !text_valid(t->album_key,sizeof t->album_key)||!text_valid(t->album_artist,sizeof t->album_artist)||
	   !text_valid(t->codec,sizeof t->codec)||!text_valid(t->fingerprint,sizeof t->fingerprint)||
	   !text_valid(t->artwork_ref,sizeof t->artwork_ref)||t->artist_count>SB_ML_NAMES_MAX||t->genre_count>SB_ML_NAMES_MAX||
	   t->identity.track_id>INT64_MAX||t->total_samples>INT64_MAX||t->file_size>INT64_MAX||t->duration_ms>INT64_MAX||t->audio_offset>INT64_MAX||
	   t->audio_length>INT64_MAX||t->scan_epoch>INT64_MAX||t->audio_offset>t->file_size||t->audio_length>t->file_size-t->audio_offset)return 0;
	for(unsigned i=0;i<t->artist_count;i++)if(!t->artists[i][0]||!text_valid(t->artists[i],sizeof t->artists[i]))return 0;
	for(unsigned i=0;i<t->genre_count;i++)if(!t->genres[i][0]||!text_valid(t->genres[i],sizeof t->genres[i]))return 0;
	return 1;
}
static media_slot *media_find(sb_music_library *h,const sb_music_source_id *id,uint64_t generation)
{
	for(unsigned i=0;i<h->media_count;i++)if(sb_music_source_equal(id,&h->media[i].state.source)) {
		if(!h->media[i].state.online){failure(h,SB_ML_OFFLINE,"Source is offline");
		return NULL;
		}
		if(!generation||h->media[i].state.generation!=generation){failure(h,SB_ML_STALE,"Stale media generation");
		return NULL;
		}
		return &h->media[i];
	}
	failure(h,SB_ML_OFFLINE,"Source has not been registered in this incarnation");
	return NULL;
}
static int progress(void *context)
{
	sb_music_library *h=context;
	int cancel=0;
	if(!pthread_mutex_trylock(&h->lock)){cancel=h->cancel;
	pthread_mutex_unlock(&h->lock);
	}
	return cancel||(h->deadline&&milliseconds()>=h->deadline);
}
static int trace_statement(unsigned event,void *context,void *statement,void *text)
{
 (void)event;(void)text;sb_music_library *h=context;
 const char *sql=sqlite3_sql(statement);if(!sql)return 0;
 uint64_t *counter=!strncmp(sql,"INSERT ",7)?&h->sql_inserts:!strncmp(sql,"UPDATE ",7)?&h->sql_updates:!strncmp(sql,"DELETE ",7)?&h->sql_deletes:NULL;
 const char *destination=NULL;
 if(!strncmp(sql,"INSERT ",7)){const char *into=strstr(sql," INTO ");if(into)destination=into+6;}
 else if(!strncmp(sql,"UPDATE ",7))destination=sql+7;
 else if(!strncmp(sql,"DELETE ",7)){const char *from=strstr(sql," FROM ");if(from)destination=from+6;}
 /* A main-table DELETE may read the temporary membership table. Classify
  * the write destination, not every table referenced by the statement. */
 if(counter&&destination&&!strncmp(destination,"temp.",5))counter=&h->temporary_writes;
 if(counter&&*counter<UINT64_MAX)(*counter)++;
 return 0;
}
static void changed_row(void *context,int operation,const char *database,const char *table,sqlite3_int64 row)
{
 (void)row;sb_music_library *h=context;
 if(strcmp(database,"main"))return;
 uint64_t *counter=operation==SQLITE_INSERT?&h->row_inserts:operation==SQLITE_UPDATE?&h->row_updates:&h->row_deletes;
 if(*counter<UINT64_MAX)(*counter)++;
 if(operation==SQLITE_UPDATE&&!strcmp(table,"tracks")&&h->track_updates<UINT64_MAX)h->track_updates++;
}
static int open_database(sb_music_library *h)
{
	pthread_once(&memory_policy_once,memory_policy);
	int flags=h->options.read_only?SQLITE_OPEN_READONLY:SQLITE_OPEN_READWRITE|(h->options.create?SQLITE_OPEN_CREATE:0);
	int rc=sqlite3_open_v2(h->options.path,&h->db,flags|SQLITE_OPEN_NOMUTEX,NULL);
	if(rc!=SQLITE_OK)return sql_error(h,rc);
	sqlite3_update_hook(h->db,changed_row,h);
	sqlite3_trace_v2(h->db,SQLITE_TRACE_STMT,trace_statement,h);
	sqlite3_extended_result_codes(h->db,1);
	sqlite3_busy_timeout(h->db,(int)h->options.busy_ms);
	sqlite3_limit(h->db,SQLITE_LIMIT_LENGTH,65536);
	sqlite3_limit(h->db,SQLITE_LIMIT_SQL_LENGTH,32768);
	sqlite3_limit(h->db,SQLITE_LIMIT_ATTACHED,0);
	if(!execute(h,"PRAGMA foreign_keys=ON",NULL,0)||!execute(h,"PRAGMA trusted_schema=OFF",NULL,0)||!script(h,"PRAGMA cache_size=-512;PRAGMA mmap_size=0;PRAGMA temp_store=MEMORY;"))return 0;
	if(h->options.maximum_bytes) {
		sqlite3_int64 page_size=0,pages=0;
		if(!scalar(h,"PRAGMA page_size",NULL,0,&page_size)||!scalar(h,"PRAGMA page_count",NULL,0,&pages)||page_size<=0)return 0;
		if((uint64_t)pages>h->options.maximum_bytes/(uint64_t)page_size){failure(h,SB_ML_LIMIT,"Database exceeds configured working-copy budget");return 0;}
		char pragma[96];snprintf(pragma,sizeof pragma,"PRAGMA max_page_count=%llu",(unsigned long long)(h->options.maximum_bytes/(uint64_t)page_size));
		if(!script(h,pragma))return 0;
	}
	sqlite3_int64 version=0,application=0,tables=0;
	if(!scalar(h,"PRAGMA user_version",NULL,0,&version)||!scalar(h,"PRAGMA application_id",NULL,0,&application))return 0;
	h->result.record_id=(uint64_t)version; /* OPEN reports pre-migration schema. */
	if(version>SB_ML_SCHEMA_VERSION){failure(h,SB_ML_NEW_SCHEMA,"Newer schema preserved; downgrade refused");
	return 0;
	}
	if(version && application!=SB_ML_APPLICATION_ID){failure(h,SB_ML_INVALID,"Not a Standalone Music database");
	return 0;
	}
	if(!version) {
		if(!scalar(h,"SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'",NULL,0,&tables))return 0;
		if(tables||application||h->options.read_only||!h->options.create){failure(h,SB_ML_INVALID,"Creation is explicit; foreign/empty database preserved");
		return 0;
		}
		if(!execute(h,"BEGIN IMMEDIATE",NULL,0))return 0;
		if(!script(h,sb_ml_schema)||!script(h,"PRAGMA application_id=1396854101;PRAGMA user_version=4;COMMIT;"))return 0;
	} else if(version==1) {
		if(h->options.read_only){failure(h,SB_ML_MIGRATION_REQUIRED,"Read-only schema needs writable migration");
		return 0;
		}
		if(!script(h,"BEGIN IMMEDIATE;ALTER TABLE tracks ADD COLUMN fingerprint TEXT NOT NULL DEFAULT '';ALTER TABLE tracks ADD COLUMN scan_epoch INTEGER NOT NULL DEFAULT 0;PRAGMA user_version=2;"))return 0;
 version=2;
	}
 if(version&&version<3){
  if(h->options.read_only){failure(h,SB_ML_MIGRATION_REQUIRED,"Read-only schema needs WP2 migration");return 0;}
  if(sqlite3_get_autocommit(h->db)&&!execute(h,"BEGIN IMMEDIATE",NULL,0))return 0;
  if(!script(h,"ALTER TABLE tracks ADD COLUMN bit_depth INTEGER NOT NULL DEFAULT 0;ALTER TABLE tracks ADD COLUMN track_total INTEGER NOT NULL DEFAULT 0;ALTER TABLE tracks ADD COLUMN disc_total INTEGER NOT NULL DEFAULT 0;ALTER TABLE tracks ADD COLUMN release_date TEXT NOT NULL DEFAULT '';ALTER TABLE tracks ADD COLUMN total_samples INTEGER NOT NULL DEFAULT 0;ALTER TABLE library_meta ADD COLUMN scan_counter INTEGER NOT NULL DEFAULT 0;ALTER TABLE media ADD COLUMN scan_root TEXT;CREATE INDEX track_fingerprint ON tracks(media_id,fingerprint);PRAGMA user_version=3;"))return 0;
  version=3;
 }
 /* Schema 3 remains a supported read-only projection for baseline rollback.
  * Writable scans migrate their private working copy transactionally. */
 if(version==3&&!h->options.read_only){
  if(sqlite3_get_autocommit(h->db)&&!execute(h,"BEGIN IMMEDIATE",NULL,0))return 0;
  if(!script(h,SB_ML_REJECTIONS_SCHEMA)||!script(h,"PRAGMA user_version=4;"))return 0;
 }

 sqlite3_int64 required=0;
 if((version>=4||!h->options.read_only)&&!execute(h,"SELECT media_id,path,fingerprint,size,mtime,code,message,previous_track_id FROM rejected_files LIMIT 0",NULL,0))return 0;
 if(!scalar(h,"SELECT count(*) FROM sqlite_schema WHERE type='index' AND name IN ('folder_parent','album_name','track_fingerprint','track_name','track_album','track_folder','track_recent','contributor_tracks','genre_tracks')",NULL,0,&required)||required!=9){
  failure(h,SB_ML_CORRUPT,"Required library indexes missing; database preserved");return 0;
 }
 sqlite3_int64 valid_revision=0;
 if(!scalar(h,"SELECT revision FROM library_meta WHERE id=1 AND scan_counter>=0",NULL,0,&valid_revision)||valid_revision<1||valid_revision>=INT64_MAX){
  failure(h,SB_ML_CORRUPT,"Missing/invalid library metadata; migration not committed");return 0;
 }

	sqlite3_stmt *check=prepare(h,"PRAGMA quick_check",NULL,0);
	if(!check)return 0;
	rc=sqlite3_step(check);
	const unsigned char *check_text=rc==SQLITE_ROW?sqlite3_column_text(check,0):NULL;
 int good=check_text&&!strcmp((const char *)check_text,"ok");
	sqlite3_finalize(check);
	if(!good){if(rc!=SQLITE_ROW)return sql_error(h,rc);
	failure(h,SB_ML_CORRUPT,"Database quick_check failed; original preserved");
	return 0;
	}
	if(!sqlite3_get_autocommit(h->db)&&!execute(h,"COMMIT",NULL,0))return 0;
	if(!h->options.read_only) {
		const char *mode=h->options.journal==SB_ML_WAL_JOURNAL?"PRAGMA journal_mode=WAL":"PRAGMA journal_mode=DELETE";
		check=prepare(h,mode,NULL,0);
		if(!check)return 0;
		rc=sqlite3_step(check);
		const unsigned char *journal_text=rc==SQLITE_ROW?sqlite3_column_text(check,0):NULL;
 good=journal_text&&!sqlite3_stricmp((const char *)journal_text,h->options.journal==SB_ML_WAL_JOURNAL?"wal":"delete");
		sqlite3_finalize(check);
		if(!good){failure(h,SB_ML_IO,"Requested journal policy unavailable");
		return 0;
		}
		if(!script(h,"PRAGMA synchronous=FULL;PRAGMA wal_autocheckpoint=64;"))return 0;
	}
	if(!script(h,"PRAGMA cache_size=-512;PRAGMA mmap_size=0;PRAGMA temp_store=MEMORY;"))return 0;
	if(!scalar(h,"SELECT revision FROM library_meta WHERE id=1",NULL,0,&version)||version<1||version==INT64_MAX){failure(h,SB_ML_CORRUPT,"Invalid library revision");
	return 0;
	}
	h->revision=(uint64_t)version;
 h->data_revision=h->revision;
	sqlite3_progress_handler(h->db,1000,progress,h);
	return 1;
}

/* Only trusted fragments enter SQL construction; values are always bound. */
static void add_filter(char *sql,binding *v,unsigned *n,const sb_music_query *q,const char *alias)
{
 char part[384];
 if(q->album_id){snprintf(part,sizeof part," AND %s.album_id=?",alias);
 strcat(sql,part);
 v[(*n)++]=I(q->album_id);
 }
 if(q->folder_id){snprintf(part,sizeof part," AND %s.folder_id=?",alias);
 strcat(sql,part);
 v[(*n)++]=I(q->folder_id);
 }
 if(q->artist_id){snprintf(part,sizeof part," AND EXISTS(SELECT 1 FROM track_contributors x WHERE x.track_id=%s.id AND x.contributor_id=?)",alias);
 strcat(sql,part);
 v[(*n)++]=I(q->artist_id);
 }
 if(q->genre_id){snprintf(part,sizeof part," AND EXISTS(SELECT 1 FROM track_genres x WHERE x.track_id=%s.id AND x.genre_id=?)",alias);
 strcat(sql,part);
 v[(*n)++]=I(q->genre_id);
 }
}
static void prefix_pattern(char out[514],const char *input)
{
 size_t n=0;
 for(;*input;input++){if(*input=='%'||*input=='_'||*input=='\\')out[n++]='\\';
 out[n++]=*input;
 }out[n++]='%';
 out[n]=0;
}
static void search_filter(char *sql,binding *v,unsigned *n,const sb_music_query *q,const char *pattern)
{
 if(!q->search[0])return;
 const char *title="t.title LIKE ? ESCAPE '\\'";
 const char *album="EXISTS(SELECT 1 FROM albums z WHERE z.id=t.album_id AND z.title LIKE ? ESCAPE '\\')";
 const char *artist="EXISTS(SELECT 1 FROM track_contributors x JOIN contributors z ON z.id=x.contributor_id WHERE x.track_id=t.id AND z.name LIKE ? ESCAPE '\\')";
 strcat(sql," AND (");
 if(q->search_field==SB_SEARCH_ANY){strcat(sql,title);
 strcat(sql," OR ");
 strcat(sql,album);
 strcat(sql," OR ");
 strcat(sql,artist);
 v[(*n)++]=T(pattern);
 v[(*n)++]=T(pattern);
 v[(*n)++]=T(pattern);
 }
 else {strcat(sql,q->search_field==SB_SEARCH_TITLE?title:q->search_field==SB_SEARCH_ARTIST?artist:album);
 v[(*n)++]=T(pattern);
 }
 strcat(sql,")");
}
static int query_sql(sb_music_library *h,const sb_music_query *q,sqlite3_int64 media,char sql[8192],binding v[32],unsigned *count,char pattern[514])
{
 *count=0;
 v[(*count)++]=I(media);
 prefix_pattern(pattern,q->search);
 if(q->kind==SB_QUERY_TRACKS){
  strcpy(sql,"SELECT t.id AS id,t.title AS name,a.title AS secondary,t.duration AS duration,COALESCE(t.artwork_id,0) AS artwork,2 AS kind,t.disc AS disc,t.track AS number,t.mtime AS recent FROM tracks t JOIN albums a ON a.id=t.album_id WHERE t.media_id=?");
  add_filter(sql,v,count,q,"t");
  search_filter(sql,v,count,q,pattern);
 } else if(q->kind==SB_QUERY_ARTISTS||q->kind==SB_QUERY_ALBUMS||q->kind==SB_QUERY_GENRES){
  if(q->kind==SB_QUERY_ARTISTS)strcpy(sql,"SELECT c.id AS id,c.name AS name,'' AS secondary,0 AS duration,0 AS artwork,0 AS kind,0 AS disc,0 AS number,0 AS recent FROM contributors c WHERE c.media_id=? AND EXISTS(SELECT 1 FROM track_contributors tc JOIN tracks t ON t.id=tc.track_id WHERE tc.contributor_id=c.id");
  else if(q->kind==SB_QUERY_ALBUMS)strcpy(sql,"SELECT a.id AS id,a.title AS name,COALESCE((SELECT name FROM contributors WHERE id=a.artist_id),'') AS secondary,0 AS duration,COALESCE((SELECT artwork_id FROM tracks WHERE media_id=a.media_id AND album_id=a.id AND artwork_id IS NOT NULL ORDER BY disc,track,id LIMIT 1),0) AS artwork,1 AS kind,0 AS disc,0 AS number,0 AS recent FROM albums a WHERE a.media_id=? AND EXISTS(SELECT 1 FROM tracks t WHERE t.album_id=a.id AND t.media_id=a.media_id");
  else strcpy(sql,"SELECT g.id AS id,g.name AS name,'' AS secondary,0 AS duration,0 AS artwork,3 AS kind,0 AS disc,0 AS number,0 AS recent FROM genres g WHERE g.media_id=? AND EXISTS(SELECT 1 FROM track_genres tg JOIN tracks t ON t.id=tg.track_id WHERE tg.genre_id=g.id");
  add_filter(sql,v,count,q,"t");
  if(q->search[0]) {
   if(q->search_field==SB_SEARCH_ANY){strcat(sql,") AND ");
   strcat(sql,q->kind==SB_QUERY_ARTISTS?"c.name LIKE ? ESCAPE '\\'":q->kind==SB_QUERY_ALBUMS?"a.title LIKE ? ESCAPE '\\'":"g.name LIKE ? ESCAPE '\\'");
   v[(*count)++]=T(pattern);
   }
   else {search_filter(sql,v,count,q,pattern);
   strcat(sql,")");
   }
  } else strcat(sql,")");
 } else if(q->kind==SB_QUERY_FOLDERS){
  if(q->album_id||q->artist_id||q->genre_id||q->search_field>SB_SEARCH_TITLE){failure(h,SB_ML_UNSUPPORTED,"Mixed folder pages support name filtering only");
  return 0;
  }
  sqlite3_int64 parent=(sqlite3_int64)q->folder_id;
  if(!parent){binding b[]={I(media)};
  if(!scalar(h,"SELECT id FROM folders WHERE media_id=? AND path=''",b,1,&parent)){
   /* An empty freshly registered source has no directories yet. */
   if(h->result.status==SB_ML_NOT_FOUND){h->result.status=SB_ML_OK;h->result.error[0]=0;}
   strcpy(sql,"SELECT 0 AS id,'' AS name,'' AS secondary,0 AS duration,0 AS artwork,4 AS kind,0 AS disc,0 AS number,0 AS recent WHERE 0");
   *count=0;
   return h->result.status==SB_ML_OK;
   }}
  strcpy(sql,"SELECT f.id AS id,f.name AS name,'' AS secondary,0 AS duration,0 AS artwork,4 AS kind,0 AS disc,0 AS number,0 AS recent FROM folders f WHERE f.media_id=? AND f.parent_id=?");
  v[(*count)++]=I(parent);
  if(q->search[0]){strcat(sql," AND f.name LIKE ? ESCAPE '\\'");
  v[(*count)++]=T(pattern);
  }
  strcat(sql," UNION ALL SELECT t.id,t.title,a.title,t.duration,COALESCE(t.artwork_id,0),2,t.disc,t.track,t.mtime FROM tracks t JOIN albums a ON a.id=t.album_id WHERE t.media_id=? AND t.folder_id=?");
  v[(*count)++]=I(media);
  v[(*count)++]=I(parent);
  if(q->search[0]){strcat(sql," AND t.title LIKE ? ESCAPE '\\'");
  v[(*count)++]=T(pattern);
  }
 } else {failure(h,SB_ML_UNSUPPORTED,"Saved playlists are not implemented in WP1");
 return 0;
 }
 return 1;
}
static void column_text(sqlite3_stmt *s,int index,char *out,size_t capacity)
{
 const unsigned char *text=sqlite3_column_text(s,index);
 if(!text){out[0]=0;
 return;
 }
 size_t n=(size_t)sqlite3_column_bytes(s,index);
 if(n>=capacity){n=capacity-1;
 while(n&&(text[n]&0xc0)==0x80)n--;
 }
 memcpy(out,text,n);
 out[n]=0;
}
static int query_library(sb_music_library *h,const sb_music_query *q,int explain)
{
 if(!sb_music_query_valid(q)||!text_valid(q->search,sizeof q->search)||q->album_id>INT64_MAX||q->artist_id>INT64_MAX||q->genre_id>INT64_MAX||q->folder_id>INT64_MAX){failure(h,SB_ML_INVALID,"Invalid bounded query");
 return 0;
 }
 if(q->library_revision&&q->library_revision!=h->revision){failure(h,SB_ML_STALE,"Library revision changed");
 return 0;
 }
 if(q->sort!=SB_SORT_NAME&&q->kind!=SB_QUERY_TRACKS){failure(h,SB_ML_UNSUPPORTED,"Requested sort applies to tracks only");
 return 0;
 }
 media_slot *media=media_find(h,&q->media.source,q->media.generation);
 if(!media)return 0;
 char base[8192],sql[9216],pattern[514];
 binding values[32];
 unsigned n;
 if(!query_sql(h,q,media->id,base,values,&n,pattern))return 0;
 const char *order=q->sort==SB_SORT_ALBUM_TRACK?"disc,number,id":q->sort==SB_SORT_RECENT?"recent DESC,id":"name COLLATE NOCASE,id,kind";
 if(!explain){
  sqlite3_int64 total=0;
  snprintf(sql,sizeof sql,"SELECT count(*) FROM (%s)",base);
  if (!scalar(h, sql, values, n, &total))
	  return 0;
  if (total > UINT32_MAX) {
	  failure(h, SB_ML_INVALID, "Result count exceeds page contract");
	  return 0;
  }
  h->result.data.query.page = (sb_music_page){h->revision,
					      q->media.generation,
					      q->offset,
					      0,
					      (uint32_t)total,
					      0,
					      1};
 }
 if (h->request.operation == SB_ML_BROWSE)
	 snprintf(sql,
		  sizeof sql,
		  "SELECT b.*,CASE WHEN kind=2 THEN COALESCE((SELECT "
		  "group_concat(name,'; ') FROM (SELECT c.name AS name "
		  "FROM track_contributors tc JOIN contributors c ON "
		  "c.id=tc.contributor_id WHERE tc.track_id=b.id AND tc.role=1 "
		  "ORDER BY tc.ordinal,c.id)),'') ELSE '' END,CASE WHEN "
		  "kind=2 THEN (SELECT year FROM tracks WHERE id=b.id) WHEN "
		  "kind=1 THEN (SELECT year FROM albums WHERE id=b.id) ELSE 0 "
		  "END,CASE WHEN kind=2 THEN (SELECT CASE codec WHEN 'mp3' "
		  "THEN 1 WHEN 'flac' THEN 2 ELSE 4 END "
		  "FROM tracks WHERE id=b.id) WHEN kind=1 THEN "
		  "COALESCE((SELECT SUM(DISTINCT "
		  "CASE codec WHEN 'mp3' THEN 1 WHEN 'flac' THEN 2 ELSE 4 END) "
		  "FROM tracks t "
		  "WHERE t.album_id=b.id AND t.media_id=(SELECT media_id FROM "
		  "albums WHERE id=b.id)),0) ELSE 0 "
		  "END FROM (SELECT * FROM (%s) ORDER BY %s LIMIT ? OFFSET ?) "
		  "b ORDER BY %s",
		  base,
		  order,
		  order);
 else
	 snprintf(sql,
		  sizeof sql,
		  "%sSELECT * FROM (%s) ORDER BY %s LIMIT ? OFFSET ?",
		  explain ? "EXPLAIN QUERY PLAN " : "",
		  base,
		  order);
 values[n++] = I(q->limit);
 values[n++] = I(q->offset);
 sqlite3_stmt *s = prepare(h, sql, values, n);
 if (!s)
	 return 0;
 int rc;
 while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
	 if (explain) {
		 unsigned i = h->result.data.plan.count;
		 if (i < 16) {
			 column_text(s, 3, h->result.data.plan.lines[i], 192);
			 h->result.data.plan.count++;
		 }
	 } else {
		 unsigned i = h->result.data.query.page.count;
		 if (i == q->limit) {
			 sqlite3_finalize(s);
			 failure(h, SB_ML_INTERNAL, "Page bound violated");
			 return 0;
		 }
		 sb_music_row *row = &h->result.data.query.rows[i];
		 row->id = (uint64_t)sqlite3_column_int64(s, 0);
		 column_text(s, 1, row->name, sizeof row->name);
		 column_text(s, 2, row->secondary, sizeof row->secondary);
		 row->duration_ms = (uint64_t)sqlite3_column_int64(s, 3);
		 row->artwork_id = (uint64_t)sqlite3_column_int64(s, 4);
		 row->kind = (sb_music_query_kind)sqlite3_column_int(s, 5);
		 if (h->request.operation == SB_ML_BROWSE) {
			 sb_ml_browse_info *info =
				 &h->result.data.query.info[i];
			 info->disc = (unsigned)sqlite3_column_int(s, 6);
			 info->track = (unsigned)sqlite3_column_int(s, 7);
			 column_text(s, 9, info->artist, sizeof info->artist);
			 info->year = (unsigned)sqlite3_column_int(s, 10);
			 info->codec_mask = (unsigned)sqlite3_column_int(s, 11);
		 }
		 h->result.data.query.page.count++;
	 }
 }
 if (!explain)
	 h->result.data.query.page.bytes =
		 h->result.data.query.page.count * sizeof(sb_music_row) +
		 sizeof(sb_music_page);
 sqlite3_finalize(s);
 return rc == SQLITE_DONE ? 1 : sql_error(h, rc);
}

static int browse_summary(sb_music_library *h, const sb_music_query *q)
{
	media_slot *media =
		media_find(h, &q->media.source, q->media.generation);
	if (!media)
		return 0;
	const char *sql[] = {
		"SELECT count(*) FROM tracks WHERE media_id=?",
		"SELECT count(*) FROM albums a WHERE a.media_id=? AND "
		"EXISTS(SELECT 1 FROM tracks t WHERE t.album_id=a.id AND t.media_id=a.media_id)",
		"SELECT count(*) FROM contributors c WHERE c.media_id=? AND "
		"EXISTS(SELECT 1 FROM track_contributors tc WHERE "
		"tc.contributor_id=c.id)"};
	uint32_t counts[3];
	binding v[] = {I(media->id)};
	for (unsigned i = 0; i < 3; i++) {
		sqlite3_int64 n;
		if (!scalar(h, sql[i], v, 1, &n))
			return 0;
		if (n < 0 || n > UINT32_MAX) {
			failure(h, SB_ML_LIMIT, "Summary count limit");
			return 0;
		}
		counts[i] = (uint32_t)n;
	}
	h->result.data.summary.tracks = counts[0];
	h->result.data.summary.albums = counts[1];
	h->result.data.summary.artists = counts[2];
	return 1;
}

static int track_detail(sb_music_library *h,
			const sb_music_track_id *id,
			uint64_t generation)
{
	media_slot *media = media_find(h, &id->source, generation);
	if (!media)
		return 0;
	if (!id->track_id || id->track_id > INT64_MAX) {
		failure(h, SB_ML_INVALID, "Invalid track ID");
		return 0;
	}
	binding values[] = {I(media->id), I(id->track_id)};
	sqlite3_stmt *s = prepare(
		h,
		"SELECT "
		"t.path,t.title,a.title,a.album_key,COALESCE(c.name,''),t."
		"codec,t.fingerprint,COALESCE(w.reference,''),t.disc,t.track,t."
		"year,t.sample_rate,t.channels,t.bitrate,a.compilation,t."
		"duration,t.size,t.audio_offset,t.audio_length,t.scan_epoch,t."
		"mtime,t.bit_depth,t.track_total,t.disc_total,t.release_date,t."
		"total_samples FROM tracks t JOIN albums a ON a.id=t.album_id "
		"LEFT JOIN contributors c ON c.id=a.artist_id LEFT JOIN "
		"artwork w ON w.id=t.artwork_id WHERE t.media_id=? AND t.id=?",
		values,
		2);
	if (!s)
		return 0;
	int rc = sqlite3_step(s);
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(s);
		if (rc != SQLITE_DONE)
			return sql_error(h, rc);
		failure(h, SB_ML_NOT_FOUND, "Track does not exist");
		return 0;
	}
	sb_ml_track *t = &h->result.data.track;
	t->identity = *id;
	t->media_generation = generation;
	column_text(s, 0, t->path, sizeof t->path);
	column_text(s, 1, t->title, sizeof t->title);
	column_text(s, 2, t->album, sizeof t->album);
	const char *key = (const char *)sqlite3_column_text(s, 3);
	if (key && !strncmp(key, "key:", 4))
		snprintf(t->album_key, sizeof t->album_key, "%s", key + 4);
	column_text(s, 4, t->album_artist, sizeof t->album_artist);
	column_text(s, 5, t->codec, sizeof t->codec);
	column_text(s, 6, t->fingerprint, sizeof t->fingerprint);
	column_text(s, 7, t->artwork_ref, sizeof t->artwork_ref);
	t->disc = (unsigned)sqlite3_column_int64(s, 8);
	t->track = (unsigned)sqlite3_column_int64(s, 9);
	t->year = (unsigned)sqlite3_column_int64(s, 10);
	t->sample_rate = (unsigned)sqlite3_column_int64(s, 11);
	t->channels = (unsigned)sqlite3_column_int64(s, 12);
	t->bitrate = (unsigned)sqlite3_column_int64(s, 13);
	t->compilation = sqlite3_column_int(s, 14);
	t->duration_ms = (uint64_t)sqlite3_column_int64(s, 15);
	t->file_size = (uint64_t)sqlite3_column_int64(s, 16);
	t->audio_offset = (uint64_t)sqlite3_column_int64(s, 17);
	t->audio_length = (uint64_t)sqlite3_column_int64(s, 18);
	t->scan_epoch = (uint64_t)sqlite3_column_int64(s, 19);
	t->mtime = sqlite3_column_int64(s, 20);
	t->bit_depth = (unsigned)sqlite3_column_int64(s, 21);
	t->track_total = (unsigned)sqlite3_column_int64(s, 22);
	t->disc_total = (unsigned)sqlite3_column_int64(s, 23);
	column_text(s, 24, t->release_date, sizeof t->release_date);
	t->total_samples = (uint64_t)sqlite3_column_int64(s, 25);
	sqlite3_finalize(s);
	binding ti[] = {I(id->track_id)};
	s = prepare(h,
		    "SELECT c.name FROM track_contributors x JOIN contributors "
		    "c ON c.id=x.contributor_id WHERE x.track_id=? AND "
		    "x.role=1 ORDER BY x.ordinal,c.id",
		    ti,
		    1);
	if (!s)
		return 0;
	while ((rc = sqlite3_step(s)) == SQLITE_ROW &&
	       t->artist_count < SB_ML_NAMES_MAX)
		column_text(s,
			    0,
			    t->artists[t->artist_count++],
			    sizeof t->artists[0]);
	sqlite3_finalize(s);
	if (rc != SQLITE_DONE)
		return sql_error(h, rc);
	s = prepare(h,
		    "SELECT g.name FROM track_genres x JOIN genres g ON "
		    "g.id=x.genre_id WHERE x.track_id=? ORDER BY g.name "
		    "COLLATE NOCASE,g.id",
		    ti,
		    1);
	if (!s)
		return 0;
	while ((rc = sqlite3_step(s)) == SQLITE_ROW &&
	       t->genre_count < SB_ML_NAMES_MAX)
		column_text(
			s, 0, t->genres[t->genre_count++], sizeof t->genres[0]);
	sqlite3_finalize(s);
	return rc == SQLITE_DONE ? 1 : sql_error(h, rc);
}

static int statistics(sb_music_library *h)
{
	sb_ml_stats *out = &h->result.data.stats;
	sqlite3_int64 n;
	if (!scalar(h, "PRAGMA page_count", NULL, 0, &n))
		return 0;
	out->page_count = (uint64_t)n;
	if (!scalar(h, "PRAGMA page_size", NULL, 0, &n))
		return 0;
	out->page_size = (unsigned)n;
	out->db_bytes = out->page_count * out->page_size;
	if (!scalar(h,
		    "SELECT COALESCE(sum(d.pgsize),0) FROM dbstat d JOIN "
		    "sqlite_schema s ON s.name=d.name WHERE s.type='index'",
		    NULL,
		    0,
		    &n))
		return 0;
	out->index_bytes = (uint64_t)n;
	sqlite3_stmt *s = prepare(h, "PRAGMA journal_mode", NULL, 0);
	if (!s)
		return 0;
	int rc = sqlite3_step(s);
	if (rc == SQLITE_ROW)
		column_text(s, 0, out->journal, sizeof out->journal);
	sqlite3_finalize(s);
	if (rc != SQLITE_ROW)
		return sql_error(h, rc);
	sqlite3_int64 used = 0, high = 0;
	sqlite3_status64(SQLITE_STATUS_MEMORY_USED, &used, &high, 0);
	out->sqlite_memory = (uint64_t)used;
	out->sqlite_memory_high = (uint64_t)high;
	out->revision = h->revision;
	sqlite3_int64 schema_version=0;if(!scalar(h,"PRAGMA user_version",NULL,0,&schema_version))return 0;
	out->schema_version = (unsigned)schema_version;
	out->row_inserts=h->row_inserts;out->row_updates=h->row_updates;
	out->row_deletes=h->row_deletes;out->track_updates=h->track_updates;
	out->sql_inserts=h->sql_inserts;out->sql_updates=h->sql_updates;
	out->sql_deletes=h->sql_deletes;out->temporary_writes=h->temporary_writes;
	return 1;
}
static void rollback_preserving_error(sb_music_library *h)
{
 sb_ml_status status=h->result.status;
 int code=h->result.sqlite_code;
 char error[160];
 memcpy(error,h->result.error,sizeof error);
 sqlite3_progress_handler(h->db,0,NULL,NULL);
 execute(h,"ROLLBACK",NULL,0);
 h->transaction=0;
 h->dirty=0;
 sqlite3_progress_handler(h->db,1000,progress,h);
 if(status!=SB_ML_OK){h->result.status=status;h->result.sqlite_code=code;memcpy(h->result.error,error,sizeof error);}
}

static void refresh_database(sb_music_library *h)
{
 if(!h->options.read_only||!h->options.refresh||h->transaction||h->scan_epoch){failure(h,SB_ML_UNSUPPORTED,"Snapshot refresh requires idle read-only provider");return;}
 if(h->revision>=INT64_MAX-1){failure(h,SB_ML_LIMIT,"Revision exhausted");return;}
 sb_ml_refresh refreshed={0};
 sb_ml_status status=h->options.refresh(h->options.refresh_context,h->options.path,&refreshed);
 if(status!=SB_ML_OK){
  if(status==SB_ML_BUSY||status==SB_ML_LIMIT||status==SB_ML_CANCELLED){failure(h,status,"Snapshot refresh deferred; previous view retained");return;}
  int changed=0;for(unsigned i=0;i<h->media_count;i++){changed|=h->media[i].state.online;h->media[i].state.online=0;}
  if(changed&&h->revision<INT64_MAX-1)h->revision++;
  failure(h,status,"Snapshot medium unavailable; catalogue retained offline");return;
 }
 h->result.data.media=refreshed.media;
 if(!refreshed.changed)return;
 if(refreshed.path[0]!='/'||!memchr(refreshed.path,0,sizeof refreshed.path)||!strcmp(refreshed.path,h->options.path)){
  failure(h,SB_ML_INVALID,"Refresh must supply an inactive validated working copy");return;
 }
 sb_ml_options previous=h->options;media_slot old_media[16];
 memcpy(old_media,h->media,sizeof old_media);unsigned old_count=h->media_count;
 uint64_t old_revision=h->revision;
 int rc=sqlite3_close(h->db);if(rc!=SQLITE_OK){sql_error(h,rc);return;}h->db=NULL;
 snprintf(h->options.path,sizeof h->options.path,"%s",refreshed.path);
 h->media_count=0;h->unavailable=0;
 int ok=open_database(h)&&source_update(h,&refreshed.media);
 if(ok){
  if(h->revision<=old_revision)h->revision=old_revision+1;
  h->result.record_id=1;h->result.data.media=refreshed.media;return;
 }
 sb_ml_result failed=h->result;
 if(h->db){sqlite3_close(h->db);h->db=NULL;}
 h->options=previous;h->unavailable=0;h->media_count=0;
 if(open_database(h)){
  memcpy(h->media,old_media,sizeof old_media);h->media_count=old_count;
  h->revision=old_revision;
 }
 h->result=failed;
}

#include "music_library_scan.inc"

static void process_request(sb_music_library *h)
{
 const sb_ml_request *r=&h->request;
 if(r->operation==SB_ML_OPEN){open_database(h);
 return;
 }
 if(r->operation==SB_ML_REFRESH){refresh_database(h);return;}
 if(r->operation==SB_ML_CLOSE){
  if(h->transaction)rollback_preserving_error(h);
  sqlite3_progress_handler(h->db,0,NULL,NULL);
  /* close performs normal SQLite recovery/checkpoint policy, never deletes DB. */
  int rc=sqlite3_close(h->db);
  if(rc!=SQLITE_OK){sql_error(h,rc);
  return;
  }h->db=NULL;
  return;
 }
 if(r->operation==SB_ML_SOURCE&&!r->data.media.online){
  if(h->transaction)rollback_preserving_error(h);
  source_update(h,&r->data.media);
  return;
 }
 if(r->operation>=SB_ML_SCAN_START&&r->operation<=SB_ML_SCAN_ABORT){scan_request(h);return;}
 if(r->scan_token&&!scan_current(h,r))return;
 if(h->transaction&&r->transaction!=h->transaction){failure(h,SB_ML_CONFLICT,"Transaction belongs to another request owner");
 return;
 }
 if(!h->transaction&&r->transaction&&r->operation!=SB_ML_BEGIN){failure(h,SB_ML_STALE,"Transaction no longer exists");
 return;
 }
 if(r->operation==SB_ML_INTEGRITY){
  if(h->transaction||h->unavailable){failure(h,SB_ML_CONFLICT,"Integrity requires an idle healthy library");return;}
  sqlite3_stmt *check=prepare(h,"PRAGMA integrity_check",NULL,0);
  if(!check)return;
  int rc=sqlite3_step(check);
  const unsigned char *text=rc==SQLITE_ROW?sqlite3_column_text(check,0):NULL;
  int ok=text&&!strcmp((const char *)text,"ok");
  if(ok)ok=sqlite3_step(check)==SQLITE_DONE;
  sqlite3_finalize(check);
  if(!ok)failure(h,SB_ML_CORRUPT,"SQLite integrity_check failed");
  return;
 }
 if(r->operation==SB_ML_SOURCE){source_update(h,&r->data.media);
 return;
 }
 if (r->operation == SB_ML_QUERY || r->operation == SB_ML_BROWSE ||
     r->operation == SB_ML_BROWSE_SUMMARY || r->operation == SB_ML_PLAN ||
     r->operation == SB_ML_DETAIL || r->operation == SB_ML_STATS) {
	 if (r->operation == SB_ML_QUERY || r->operation == SB_ML_BROWSE ||
	     r->operation == SB_ML_BROWSE_SUMMARY ||
	     r->operation == SB_ML_PLAN) {
		 if (!sb_music_query_valid(&r->data.query)) {
			 failure(h, SB_ML_INVALID, "Invalid bounded query");
			 return;
		 }
		 if (!media_find(h,
				 &r->data.query.media.source,
				 r->data.query.media.generation))
			 return;
	 } else if (r->operation == SB_ML_DETAIL &&
		    !media_find(h,
				&r->data.detail.identity.source,
				r->data.detail.generation))
		 return;
	 if (h->unavailable) {
		 failure(h,
			 SB_ML_IO,
			 "Storage requires explicit reopen after I/O failure");
		 return;
	 }
	 int snapshot = !h->transaction;
	 if (snapshot && !execute(h, "BEGIN", NULL, 0))
		 return;
	 int ok = refresh_revision(h);
	 if (ok) {
		 if (r->operation == SB_ML_BROWSE_SUMMARY)
			 ok = browse_summary(h, &r->data.query);
		 else if (r->operation == SB_ML_QUERY ||
			  r->operation == SB_ML_BROWSE ||
			  r->operation == SB_ML_PLAN)
			 ok = query_library(
				 h, &r->data.query, r->operation == SB_ML_PLAN);
		 else if (r->operation == SB_ML_DETAIL)
			 ok = track_detail(h,
					   &r->data.detail.identity,
					   r->data.detail.generation);
		 else
			 ok = statistics(h);
	 }
	 if (snapshot) {
		 if (ok)
			 execute(h, "COMMIT", NULL, 0);
		 else
			 rollback_preserving_error(h);
	 }
	 return;
 }
 if (h->options.read_only) {
	 failure(h, SB_ML_READ_ONLY, "Library opened read-only");
	 return;
 }
 if (r->operation == SB_ML_BEGIN) {
	 if (!r->transaction || h->transaction) {
		 failure(h, SB_ML_CONFLICT, "Invalid or nested transaction");
		 return;
	 }
	 if (execute(h, "BEGIN IMMEDIATE", NULL, 0)) {
		 h->transaction = r->transaction;
		 if (!refresh_revision(h)) {
			 rollback_preserving_error(h);
			 return;
		 }
		 h->dirty = 0;
	 }
	 return;
 }
 if(r->operation==SB_ML_COMMIT){
  if(!h->transaction){failure(h,SB_ML_STALE,"No transaction to commit");
  return;
  }
  if((!h->dirty||bump_revision(h))&&execute(h,"COMMIT",NULL,0)){if(h->dirty)revision_committed(h);
  h->transaction=0;
  h->dirty=0;
  }
  else rollback_preserving_error(h);
  return;
 }
 if(r->operation==SB_ML_ROLLBACK){if(!h->transaction){failure(h,SB_ML_STALE,"No transaction to roll back");
 return;
 }if(execute(h,"ROLLBACK",NULL,0)){h->transaction=0;
 h->dirty=0;
 }return;
 }
 if(r->operation!=SB_ML_UPSERT&&r->operation!=SB_ML_DELETE_TRACK){failure(h,SB_ML_INVALID,"Invalid library operation");
 return;
 }
 const sb_music_track_id *identity=r->operation==SB_ML_UPSERT?&r->data.track.identity:&r->data.detail.identity;
 uint64_t generation=r->operation==SB_ML_UPSERT?r->data.track.media_generation:r->data.detail.generation;
 media_slot *media=media_find(h,&identity->source,generation);
 if(!media)return;
 if(r->operation==SB_ML_UPSERT&&!sb_music_library_track_valid(&r->data.track)){failure(h,SB_ML_INVALID,"Invalid track metadata/path/bounds");
 return;
 }
 if(r->operation==SB_ML_DELETE_TRACK&&(!identity->track_id||identity->track_id>INT64_MAX)){failure(h,SB_ML_INVALID,"Invalid delete ID");
 return;
 }
 int automatic=!h->transaction;
 if(automatic&&!execute(h,"BEGIN IMMEDIATE",NULL,0))return;
 if(automatic&&!refresh_revision(h)){rollback_preserving_error(h);return;}
 int ok;
 if(r->operation==SB_ML_UPSERT)ok=upsert_track(h,&r->data.track,media->id);
 else {binding v[]={I(media->id),I(identity->track_id)};
 ok=execute(h,"DELETE FROM tracks WHERE media_id=? AND id=?",v,2);
 if(ok&&sqlite3_changes(h->db))h->dirty=1;
 }
 if(ok && r->operation==SB_ML_UPSERT && r->scan_token){
  binding rejected[]={I(media->id),T(r->data.track.path)};
  ok=execute(h,"DELETE FROM rejected_files WHERE media_id=? AND path=?",rejected,2);
  binding seen[]={I(h->result.record_id)};
  if(ok)ok=execute(h,"INSERT OR IGNORE INTO temp.scan_seen VALUES(?)",seen,1);
 }
 if(ok&&automatic){ok=(!h->dirty||bump_revision(h))&&execute(h,"COMMIT",NULL,0);
 if(ok){if(h->dirty)revision_committed(h);
 h->dirty=0;
 }}
 if(!ok)rollback_preserving_error(h);
}
static void *database_worker(void *context)
{
 sb_music_library *h=context;
 for(;;){
  pthread_mutex_lock(&h->lock);
  while(!h->pending)pthread_cond_wait(&h->condition,&h->lock);

  pthread_mutex_unlock(&h->lock);
  memset(&h->result,0,sizeof h->result);
  h->result.request_id=h->request.request_id;
  h->result.operation=h->request.operation;
  h->deadline=h->request.operation==SB_ML_OPEN?0:milliseconds()+(h->options.query_ms?h->options.query_ms:2000);
  process_request(h);
  if(h->result.status==SB_ML_CANCELLED&&h->transaction)rollback_preserving_error(h);
  h->result.revision=h->revision;
  int exit=h->request.operation==SB_ML_CLOSE||(h->request.operation==SB_ML_OPEN&&h->result.status!=SB_ML_OK);
  if(exit&&h->db){sqlite3_close(h->db);
  h->db=NULL;
  }
  pthread_mutex_lock(&h->lock);
  h->pending=0;
  h->ready=1;
  h->exited=exit;
  unsigned char byte = 1;
  ssize_t ignored;
  do {
	  ignored = write(h->pipe[1], &byte, 1);
  } while (ignored < 0 && errno == EINTR);
  (void)ignored;
  pthread_mutex_unlock(&h->lock);
  if (exit)
	  return NULL;
 }
}

const char *sb_music_library_status(sb_ml_status status)
{
	static const char *names[] = {"ok",
				      "busy",
				      "invalid",
				      "not_found",
				      "offline",
				      "stale",
				      "read_only",
				      "io",
				      "corrupt",
				      "new_schema",
				      "migration_required",
				      "conflict",
				      "cancelled",
				      "unsupported",
				      "internal",
				      "limit"};
	return (unsigned)status < sizeof names / sizeof *names
		       ? names[status]
		       : "invalid_status";
}

sb_music_library *sb_music_library_start(const sb_ml_options *options)
{
	if (!options || options->path[0] != '/' ||
	    !text_valid(options->path, sizeof options->path) ||
	    options->busy_ms > 200 || options->query_ms > 5000 ||
	    (options->maximum_bytes && (options->maximum_bytes<4096 || options->maximum_bytes>UINT64_C(536870912))) ||
	    (unsigned)options->journal > SB_ML_WAL_JOURNAL ||
	    (options->journal == SB_ML_WAL_JOURNAL && !options->wal_safe) ||
	    (options->read_only && options->create))
		return NULL;
	sb_music_library *h = calloc(1, sizeof *h);
	if (!h)
		return NULL;
	h->options = *options;
	if (pthread_mutex_init(&h->lock, NULL)) {
		free(h);
		return NULL;
	}
	if (pthread_mutex_init(&h->broker, NULL)) {
		pthread_mutex_destroy(&h->lock);
		free(h);
		return NULL;
	}
	if (pthread_cond_init(&h->condition, NULL)) {
		pthread_mutex_destroy(&h->lock);
		pthread_mutex_destroy(&h->broker);
		free(h);
		return NULL;
	}
	if (pipe(h->pipe)) {
		pthread_cond_destroy(&h->condition);
		pthread_mutex_destroy(&h->lock);
		pthread_mutex_destroy(&h->broker);
		free(h);
		return NULL;
	}
	for (unsigned i = 0; i < 2; i++) {
		if (fcntl(h->pipe[i], F_SETFL, O_NONBLOCK) ||
		    fcntl(h->pipe[i], F_SETFD, FD_CLOEXEC)) {
			close(h->pipe[0]);
			close(h->pipe[1]);
			pthread_cond_destroy(&h->condition);
			pthread_mutex_destroy(&h->lock);
			pthread_mutex_destroy(&h->broker);
			free(h);
			return NULL;
		}
	}
	h->pending = 1;
	h->request.operation = SB_ML_OPEN;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 256u * 1024u);
	int rc = pthread_create(&h->thread, &attr, database_worker, h);
	pthread_attr_destroy(&attr);
	if (rc) {
		close(h->pipe[0]);
		close(h->pipe[1]);
		pthread_cond_destroy(&h->condition);
		pthread_mutex_destroy(&h->lock);
		pthread_mutex_destroy(&h->broker);
		free(h);
		return NULL;
	}
	return h;
}

int sb_music_library_fd(const sb_music_library *h)
{
	return h ? h->pipe[0] : -1;
}

sb_ml_status sb_music_library_submit(sb_music_library *h,
				     const sb_ml_request *request)
{
	if (!h || !request || !request->request_id ||
	    (request->request_id == UINT64_MAX &&
	     request->operation != SB_ML_CLOSE) ||
	    request->operation == SB_ML_OPEN ||
	    (unsigned)request->operation > SB_ML_CLOSE)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&h->lock))
		return SB_ML_BUSY;
	sb_ml_status status = h->pending || h->ready ? SB_ML_BUSY
			      : h->exited	     ? SB_ML_INVALID
			      : request->request_id <= h->last_request
				      ? SB_ML_STALE
				      : SB_ML_OK;
	if (status == SB_ML_OK) {
		h->last_request = request->request_id;
		h->request = *request;
		h->pending = 1;
		h->cancel = 0;
		pthread_cond_signal(&h->condition);
	}
	pthread_mutex_unlock(&h->lock);
	return status;
}

sb_ml_status sb_music_library_receive(sb_music_library *h, sb_ml_result *result)
{
	if (!h || !result)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&h->lock))
		return SB_ML_BUSY;
	sb_ml_status status = h->ready ? SB_ML_OK : SB_ML_BUSY;
	if (h->ready) {
		*result = h->result;
		h->ready = 0;
		unsigned char byte;
		ssize_t ignored;
		do {
			ignored = read(h->pipe[0], &byte, 1);
		} while (ignored < 0 && errno == EINTR);
		(void)ignored;
	}
	pthread_mutex_unlock(&h->lock);
	return status;
}

sb_ml_status sb_music_library_cancel(sb_music_library *h, uint64_t request_id)
{
	if (!h)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&h->lock))
		return SB_ML_BUSY;
	sb_ml_status status = h->pending && h->request.request_id == request_id
				      ? SB_ML_OK
				      : SB_ML_STALE;
	if (status == SB_ML_OK)
		h->cancel = 1;
	pthread_mutex_unlock(&h->lock);
	return status;
}
sb_ml_status sb_music_library_cancel_scan(sb_music_library *h,uint64_t token)
{
 if(!h||!token)return SB_ML_INVALID;
 if(pthread_mutex_trylock(&h->lock))return SB_ML_BUSY;
 sb_ml_status status=h->pending&&h->request.scan_token==token?SB_ML_OK:SB_ML_STALE;
 if(status==SB_ML_OK)h->cancel=1;
 pthread_mutex_unlock(&h->lock);return status;
}
sb_ml_status sb_music_library_destroy(sb_music_library *h)
{
 if(!h)return SB_ML_INVALID;
 if(pthread_mutex_trylock(&h->lock))return SB_ML_BUSY;
 int safe=h->exited&&!h->ready&&!h->pending;
 pthread_mutex_unlock(&h->lock);
 if(!safe)return SB_ML_BUSY;
 pthread_join(h->thread,NULL);
 close(h->pipe[0]);
 close(h->pipe[1]);
 pthread_cond_destroy(&h->condition);
 pthread_mutex_destroy(&h->lock);
 pthread_mutex_destroy(&h->broker);
 free(h);
 return SB_ML_OK;
}

static int provider_submit(void *context,uint64_t request_id,const sb_music_query *query)
{
 if(!query)return SB_ML_INVALID;
 sb_ml_request request={0};
 request.request_id=request_id;request.operation=SB_ML_QUERY;request.data.query=*query;
 return sb_music_library_submit(context,&request);
}
static void provider_cancel(void *context,uint64_t request_id)
{
 (void)sb_music_library_cancel(context,request_id);
}
sb_music_library_provider sb_music_library_queries(sb_music_library *h)
{
 return (sb_music_library_provider){h,provider_submit,provider_cancel};
}

/* Serial broker for blocking worker clients only. Existing async API is retained
 * unchanged; a handle must use one consumption mode at a time. */
sb_ml_status static_exchange(sb_music_library *h,sb_ml_request *request,sb_ml_result *result)
{
 if(!h||!request||!result)return SB_ML_INVALID;
 pthread_mutex_lock(&h->lock);
 if(h->pending||h->ready||h->last_request>=UINT64_MAX-1){pthread_mutex_unlock(&h->lock);pthread_mutex_unlock(&h->broker);return SB_ML_BUSY;}
 request->request_id=h->last_request+1;
 pthread_mutex_unlock(&h->lock);
 sb_ml_status status;
 struct timespec pause={0,1000000};
 while((status=sb_music_library_submit(h,request))==SB_ML_BUSY)nanosleep(&pause,NULL);
 if(status==SB_ML_OK)for(;;){
  status=sb_music_library_receive(h,result);if(status!=SB_ML_BUSY)break;
  struct pollfd p={h->pipe[0],POLLIN,0};int rc;do{rc=poll(&p,1,-1);}while(rc<0&&errno==EINTR);
  if(rc<0){status=SB_ML_IO;break;}
 }
 pthread_mutex_unlock(&h->broker);return status==SB_ML_OK?result->status:status;
}

int sb_music_library_browse_attach(sb_music_library *h)
{
	if (!h || pthread_mutex_trylock(&h->lock))
		return 0;
	int ok = !h->browse_attached && !h->exited;
	if (ok)
		h->browse_attached = 1;
	pthread_mutex_unlock(&h->lock);
	return ok;
}

void sb_music_library_browse_detach(sb_music_library *h)
{
	if (h) {
		pthread_mutex_lock(&h->lock);
		h->browse_attached = 0;
		h->browse_until = 0;
		pthread_mutex_unlock(&h->lock);
	}
}

sb_ml_status sb_music_library_cancel_browse(sb_music_library *h, uint64_t token)
{
	if (!h || !token)
		return SB_ML_INVALID;
	if (pthread_mutex_trylock(&h->lock))
		return SB_ML_BUSY;
	sb_ml_status s = h->pending && h->request.browse_token == token
				 ? SB_ML_OK
				 : SB_ML_STALE;
	if (s == SB_ML_OK)
		h->cancel = 1;
	pthread_mutex_unlock(&h->lock);
	return s;
}

sb_ml_status sb_music_library_exchange_browse(sb_music_library *h,
					      sb_ml_request *r,
					      sb_ml_result *out)
{
	if (!h || !r || !out || !r->browse_token ||
	    (r->operation != SB_ML_BROWSE &&
	     r->operation != SB_ML_DETAIL &&
	     r->operation != SB_ML_BROWSE_SUMMARY &&
	     r->operation != SB_ML_REFRESH &&
	     r->operation != SB_ML_SOURCE))
		return SB_ML_INVALID;
	pthread_mutex_lock(&h->lock);
	if (!h->browse_attached) {
		pthread_mutex_unlock(&h->lock);
		return SB_ML_INVALID;
	}
	h->browse_until = milliseconds() + 20;
	pthread_mutex_unlock(&h->lock);
	if (pthread_mutex_trylock(&h->broker))
		return SB_ML_BUSY;
	sb_ml_status s = static_exchange(h, r, out);
	pthread_mutex_lock(&h->lock);
	h->browse_until = 0;
	pthread_mutex_unlock(&h->lock);
	return s;
}

sb_ml_status sb_music_library_exchange(sb_music_library *h,
				       sb_ml_request *r,
				       sb_ml_result *out)
{
	if (!h || !r || !out)
		return SB_ML_INVALID;
	const struct timespec pause = {0, 1000000};
	for (;;) {
		pthread_mutex_lock(&h->broker);
		pthread_mutex_lock(&h->lock);
		int yield = h->browse_until > milliseconds();
		pthread_mutex_unlock(&h->lock);
		if (!yield)
			break;
		pthread_mutex_unlock(&h->broker);
		nanosleep(&pause, NULL);
	}
	return static_exchange(h, r, out);
}
