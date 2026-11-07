#ifndef SB_MUSIC_LIBRARY_H
#define SB_MUSIC_LIBRARY_H
#include "../common/music_contracts.h"
#define SB_ML_SCHEMA_VERSION 4
#define SB_ML_PATH_MAX 4096
#define SB_ML_TEXT_MAX 512
#define SB_ML_NAMES_MAX 8

typedef enum { SB_ML_OK, SB_ML_BUSY, SB_ML_INVALID, SB_ML_NOT_FOUND,
	SB_ML_OFFLINE, SB_ML_STALE, SB_ML_READ_ONLY, SB_ML_IO, SB_ML_CORRUPT,
	SB_ML_NEW_SCHEMA, SB_ML_MIGRATION_REQUIRED, SB_ML_CONFLICT,
	SB_ML_CANCELLED, SB_ML_UNSUPPORTED, SB_ML_INTERNAL, SB_ML_LIMIT } sb_ml_status;
typedef enum { SB_ML_DELETE_JOURNAL, SB_ML_WAL_JOURNAL } sb_ml_journal;
typedef struct { char path[SB_ML_PATH_MAX]; sb_music_media media; int changed; } sb_ml_refresh;
/* Optional validated snapshot provider, invoked only on the database worker.
 * Returns an inactive private-RAM candidate; never overwrites the active file. */
typedef sb_ml_status (*sb_ml_refresh_fn)(void *,const char *,sb_ml_refresh *);
typedef struct {
	char path[SB_ML_PATH_MAX]; /* explicit absolute DB filename; no default root */
	int create, read_only, wal_safe;
	sb_ml_journal journal;
	unsigned busy_ms, query_ms; /* bounds: <=200 ms busy, <=5000 ms CPU/progress deadline */
	uint64_t maximum_bytes; /* 0 = existing default; optional bounded persistent working copy */
	sb_ml_refresh_fn refresh; void *refresh_context;
	int single_source; /* selected-volume store: never adopt another medium's rows */
} sb_ml_options;
typedef struct {
	sb_music_track_id identity;
	uint64_t media_generation;
	char path[SB_ML_PATH_MAX], title[SB_ML_TEXT_MAX], album[SB_ML_TEXT_MAX];
	char release_date[64];
	char album_key[256], album_artist[256], codec[32], fingerprint[128], artwork_ref[512];
	char artists[SB_ML_NAMES_MAX][256], genres[SB_ML_NAMES_MAX][128];
	unsigned artist_count, genre_count, disc, track, year, sample_rate, channels, bitrate;
	unsigned bit_depth, track_total, disc_total;
	int compilation;
	uint64_t duration_ms, file_size, audio_offset, audio_length, scan_epoch, total_samples;
	int64_t mtime;
} sb_ml_track;
typedef struct {
 sb_music_media media;
 char path[SB_ML_PATH_MAX], fingerprint[128], message[128];
 uint64_t file_size, previous_track_id;
 int64_t mtime;
 unsigned code; /* stable metadata classification: unsupported=1, invalid=2, limit=3 */
} sb_ml_rejection;

typedef enum { SB_ML_OPEN, SB_ML_SOURCE, SB_ML_BEGIN, SB_ML_COMMIT, SB_ML_ROLLBACK,
	SB_ML_UPSERT, SB_ML_DELETE_TRACK, SB_ML_QUERY, SB_ML_DETAIL,
	SB_ML_STATS, SB_ML_PLAN, SB_ML_SCAN_START, SB_ML_SCAN_LOOKUP,
	SB_ML_SCAN_TOUCH, SB_ML_SCAN_REJECT_LOOKUP, SB_ML_SCAN_REJECT, SB_ML_SCAN_FINISH, SB_ML_SCAN_ABORT, SB_ML_BROWSE,
	SB_ML_BROWSE_SUMMARY, SB_ML_INTEGRITY, SB_ML_REFRESH, SB_ML_CLOSE } sb_ml_operation;
typedef struct {
	uint64_t request_id, transaction, scan_token, browse_token;
	sb_ml_operation operation;
	union { sb_music_media media; sb_ml_track track; sb_music_query query; sb_ml_rejection rejection;
		struct { sb_music_track_id identity; uint64_t generation; } detail; } data;
} sb_ml_request;
typedef struct {
	uint64_t db_bytes, index_bytes, page_count, revision, sqlite_memory, sqlite_memory_high;
	unsigned page_size, schema_version;
	uint64_t row_inserts, row_updates, row_deletes, track_updates;
	uint64_t sql_inserts, sql_updates, sql_deletes, temporary_writes;
	char journal[16];
} sb_ml_stats;
typedef struct {
	char artist[128];
	unsigned disc, track, year, codec_mask;
} sb_ml_browse_info;
typedef struct {
	uint64_t request_id, revision, record_id;
	sb_ml_operation operation;
	sb_ml_status status;
	int sqlite_code;
	char error[160];
	union {
		sb_music_media media;
		struct { sb_music_page page; sb_music_row rows[SB_MUSIC_PAGE_MAX];
			sb_ml_browse_info info[SB_MUSIC_PAGE_MAX]; } query;
		struct { uint32_t tracks, albums, artists; } summary;
		sb_ml_track track;
		sb_ml_rejection rejection;
		sb_ml_stats stats;
		struct { unsigned count; char lines[16][192]; } plan;
	} data;
} sb_ml_result;
typedef struct sb_music_library sb_music_library;

/* Explicit construction only. SQLite open happens on the new worker and emits
 * request_id=0 / SB_ML_OPEN. Normal sbbase never calls this in WP1. */
sb_music_library *sb_music_library_start(const sb_ml_options *);
int sb_music_library_fd(const sb_music_library *);
/* Single-flight bounded mailbox; neither function waits for database work.
 * Copies all caller-owned data. BUSY means retry later without spinning. */
sb_ml_status sb_music_library_submit(sb_music_library *, const sb_ml_request *);
sb_ml_status sb_music_library_receive(sb_music_library *, sb_ml_result *);
sb_ml_status sb_music_library_cancel(sb_music_library *, uint64_t request_id); /* cooperative, identity checked */
sb_music_library_provider sb_music_library_queries(sb_music_library *);
/* Only after OPEN failure or completed CLOSE has been received. No SQL/join
 * of an active database operation is permitted on the caller's control loop. */
sb_ml_status sb_music_library_destroy(sb_music_library *);
const char *sb_music_library_status(sb_ml_status);
/* Worker-only synchronous broker over the SAME existing mailbox/connection.
 * While used, every producer/consumer of this handle must use exchange; never
 * mix with raw submit/receive. Not permitted on protocol/Jive control loops.
 * Serializes jobs, allocates request IDs and sleeps on the existing wakeup FD. */
sb_ml_status sb_music_library_exchange(sb_music_library *,sb_ml_request *,sb_ml_result *);
sb_ml_status sb_music_library_cancel_scan(sb_music_library *,uint64_t scan_token);
int sb_music_library_track_valid(const sb_ml_track *);
/* One browse dispatcher per handle; worker-only broker try-acquire, with a
 * short read-turn reservation. Never consume raw completions concurrently. */
int sb_music_library_browse_attach(sb_music_library *);
void sb_music_library_browse_detach(sb_music_library *);
sb_ml_status sb_music_library_exchange_browse(sb_music_library *,sb_ml_request *,sb_ml_result *);
sb_ml_status sb_music_library_cancel_browse(sb_music_library *,uint64_t token);
#endif
