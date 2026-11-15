#ifndef SB_MUSIC_CONTRACTS_H
#define SB_MUSIC_CONTRACTS_H
#include <stddef.h>
#include <stdint.h>

/* WP0 ABI only. No runtime activation path exists in this package. */
#define SB_MUSIC_ENABLED 0
#define SB_MUSIC_PAGE_MAX 50u
#define SB_MUSIC_QUEUE_MAX 10000u
#define SB_MUSIC_RESOURCE_MAX 8u
#define SB_MUSIC_QUERY_BYTES_MAX 16384u
#define SB_MUSIC_TOKEN_BYTES 32u /* entropy bytes, not an encoded string */

typedef struct { unsigned char bytes[16]; } sb_music_source_id;
typedef struct {
	sb_music_source_id source;
	uint64_t generation; /* monotonic within controller incarnation; zero invalid */
	int online;
} sb_music_media;
enum { SB_TRACK_LOCAL=0,SB_TRACK_NETWORK=1 };
typedef struct { sb_music_source_id source; uint64_t track_id;
 unsigned kind;uint64_t network_id; } sb_music_track_id;
int sb_music_track_valid(const sb_music_track_id *);
int sb_music_track_equal(const sb_music_track_id *,const sb_music_track_id *);
typedef struct {
 char server[256],object[512],uri[2048];
 uint64_t generation;
} sb_music_network_track;
typedef enum { SB_OWNER_NONE, SB_OWNER_RADIO, SB_OWNER_SPOTIFY,
	SB_OWNER_QOBUZ, SB_OWNER_MUSIC } sb_music_owner;
typedef enum { SB_YIELD_IDLE, SB_YIELD_REQUESTED, SB_YIELD_ACKNOWLEDGED,
	SB_YIELD_REFUSED, SB_YIELD_TIMEOUT, SB_YIELD_REVOKED } sb_music_yield_state;
typedef struct {
	sb_music_owner current, requested;
	uint64_t generation, request_id, deadline_ms, connection_generation;
	sb_music_yield_state yield;
	int physical_release_confirmed; /* a metadata lease can NEVER set this */
} sb_music_ownership;
typedef enum { SB_STREAM_NEW, SB_STREAM_CONNECTED, SB_STREAM_DECODER_READY,
	SB_STREAM_AUDIBLE, SB_STREAM_INPUT_EOF, SB_STREAM_DRAINED,
	SB_STREAM_STOPPED, SB_STREAM_FAILED } sb_music_stream_state;
typedef struct {
	uint64_t epoch, owner_generation, media_generation, connection_generation;
	sb_music_track_id track;
} sb_music_stream_id;
typedef struct {
	sb_music_stream_id id;
	sb_music_stream_state state;
	uint32_t seen_events; /* independent flags: ready and input EOF can overlap audible */
	int terminal_seen;
} sb_music_stream_guard;
typedef enum { SB_EVENT_CONNECTED, SB_EVENT_DECODER_READY, SB_EVENT_AUDIBLE,
	SB_EVENT_INPUT_EOF, SB_EVENT_DRAINED, SB_EVENT_STOPPED, SB_EVENT_FAILED } sb_music_event;
typedef struct {
	uint64_t entry_id;
	sb_music_track_id track;
	uint64_t media_generation;
} sb_music_queue_entry;
typedef struct {
	uint64_t revision, owner_generation, media_generation, current_entry_id;
	uint32_t count, current_index; /* UINT32_MAX iff no current item */
	sb_music_track_id current_track;
} sb_music_queue_snapshot;
typedef struct {
	uint64_t expected_revision, owner_generation, media_generation;
	uint32_t result_count;
} sb_music_queue_update; /* validate then atomically commit once; no queue storage here */
typedef enum { SB_QUERY_ARTISTS, SB_QUERY_ALBUMS, SB_QUERY_TRACKS,
	SB_QUERY_GENRES, SB_QUERY_FOLDERS, SB_QUERY_PLAYLISTS } sb_music_query_kind;
typedef enum { SB_SORT_NAME, SB_SORT_ALBUM_TRACK, SB_SORT_RECENT } sb_music_sort;
typedef enum { SB_SEARCH_ANY, SB_SEARCH_TITLE, SB_SEARCH_ARTIST, SB_SEARCH_ALBUM } sb_music_search_field;
typedef struct {
	sb_music_query_kind kind;
	sb_music_sort sort;
	sb_music_media media;
	uint64_t library_revision, artist_id, album_id, genre_id, folder_id, playlist_id;
	uint32_t offset, limit;
	char search[256];
	sb_music_search_field search_field;
} sb_music_query;
typedef struct {
	uint64_t library_revision, media_generation;
	uint32_t offset, count, total, bytes;
	int media_online;
} sb_music_page; /* database DTO; deliberately no Jive action or JSON fields */
typedef struct {
	uint64_t id, artwork_id, duration_ms;
	char name[128], secondary[128];
	sb_music_query_kind kind; /* mixed folder pages distinguish folders/tracks */
} sb_music_row; /* bounded display projection, never the full persisted model */
typedef struct {
	void *context;
	int (*submit_query)(void *context, uint64_t request_id, const sb_music_query *);
	void (*cancel_query)(void *context, uint64_t request_id);
} sb_music_library_provider;

typedef enum { SB_RESOURCE_AUDIO, SB_RESOURCE_ARTWORK } sb_music_resource_kind;
enum { SB_RESOURCE_GET = 1, SB_RESOURCE_HEAD = 2 };
typedef struct {
	unsigned char token[SB_MUSIC_TOKEN_BYTES];
	sb_music_resource_kind kind;
	sb_music_media media;
	uint64_t owner_generation, stream_epoch, expires_ms, size;
	unsigned methods;
	char mime[64];
	int revoked;
	int streaming; /* explicit resource wire v3 socket stream; never pread/seek */
} sb_music_resource;
typedef struct { uint64_t first, length; } sb_music_byte_range;
/* API-only future provider: successful open transfers one owned descriptor.
 * No paths cross the HTTP boundary. Received SCM_RIGHTS descriptors share an
 * open-file offset: consumers must pread, never seek a shared descriptor. */
typedef struct {
	sb_music_resource resource;
	int fd;
	uint64_t request_id;
} sb_music_resource_handle;
typedef enum { SB_RESOURCE_OK, SB_RESOURCE_NOT_FOUND, SB_RESOURCE_EXPIRED,
	SB_RESOURCE_REVOKED, SB_RESOURCE_OFFLINE, SB_RESOURCE_BUSY,
	SB_RESOURCE_BAD_RANGE, SB_RESOURCE_BAD_METHOD, SB_RESOURCE_IO_ERROR } sb_music_resource_result;
typedef struct {
	/* Nonblocking submit/completion interface; implementations arrive later. */
	int (*submit_open)(uint64_t request_id, const unsigned char token[SB_MUSIC_TOKEN_BYTES], unsigned method);
	void (*cancel)(uint64_t request_id);
	void (*revoke_media)(const sb_music_source_id *, uint64_t generation);
} sb_music_resource_provider;

int sb_music_source_equal(const sb_music_source_id *, const sb_music_source_id *);
int sb_music_media_current(const sb_music_media *, const sb_music_media *);
int sb_music_yield_ack(sb_music_ownership *, uint64_t request_id,
	uint64_t generation, uint64_t connection_generation, uint64_t now_ms, int accepted);
int sb_music_can_commit_owner(const sb_music_ownership *, uint64_t now_ms);
int sb_music_stream_event(sb_music_stream_guard *, const sb_music_stream_id *, sb_music_event);
int sb_music_queue_update_valid(const sb_music_queue_snapshot *, const sb_music_queue_update *);
int sb_music_query_valid(const sb_music_query *);
int sb_music_resource_current(const sb_music_resource *, const sb_music_media *,
	uint64_t owner_generation, uint64_t stream_epoch, uint64_t now_ms, unsigned method);
int sb_music_range_valid(uint64_t size, const sb_music_byte_range *);
#endif
