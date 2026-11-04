#ifndef SB_MUSIC_STORAGE_H
#define SB_MUSIC_STORAGE_H
#include "../common/music_contracts.h"
#include <sys/types.h>
#define SB_STORAGE_MAX 16
#define SB_STORAGE_PATH 1024
#define SB_STORAGE_MOUNTS_MAX 128
typedef enum { SB_STORAGE_OK, SB_STORAGE_DISABLED, SB_STORAGE_BUSY,
 SB_STORAGE_INVALID, SB_STORAGE_LIMIT, SB_STORAGE_STALE, SB_STORAGE_IO,
 SB_STORAGE_UNSUPPORTED, SB_STORAGE_NOT_MOUNTED, SB_STORAGE_AMBIGUOUS } sb_storage_status;
typedef enum { SB_STORAGE_UNKNOWN, SB_STORAGE_USB, SB_STORAGE_SD } sb_storage_type;
typedef enum { SB_STORAGE_ABSENT, SB_STORAGE_DISCOVERED, SB_STORAGE_MOUNT_PENDING,
 SB_STORAGE_AVAILABLE, SB_STORAGE_UNAVAILABLE, SB_STORAGE_QUIESCING,
 SB_STORAGE_UNMOUNTED, SB_STORAGE_OFFLINE, SB_STORAGE_ERROR } sb_storage_state;
typedef struct {
 uint64_t mount_id;
 unsigned major, minor;
 char root[SB_STORAGE_PATH], path[SB_STORAGE_PATH], device[256], fs[32];
 int read_only;
} sb_storage_mount;
typedef struct { unsigned count; int has_mount_ids; sb_storage_mount rows[SB_STORAGE_MOUNTS_MAX]; } sb_storage_mounts;
/* Only a trusted OS identity resolver may fill these, never an untrusted UI.
 * Both UUID and hardware+partition identity required for persistent association. */
typedef struct { char uuid[128], hardware_partition[256]; } sb_storage_evidence;
typedef struct { sb_music_source_id source; sb_storage_evidence evidence; uint64_t library_id; int tainted; } sb_storage_identity;
typedef struct {
 sb_music_media media;
 sb_storage_state state;
 sb_storage_status error;
 sb_storage_type type;
 sb_storage_mount mount;
 sb_storage_evidence evidence;
 uint64_t event_sequence, validated_ms, library_id;
 int mounted, writable, persistent_identity;
} sb_storage_medium;
typedef struct {
 sb_music_media previous, current;
 int revoked, changed;
} sb_storage_change;
typedef struct {
 int enabled, has_storage;
 uint64_t next_generation, next_identity, incarnation;
 unsigned count, identity_count;
 sb_storage_medium media[SB_STORAGE_MAX];
 sb_storage_identity identities[SB_STORAGE_MAX];
} sb_storage_model;
typedef enum { SB_STORAGE_INSERT, SB_STORAGE_VALIDATED, SB_STORAGE_REMOVE,
 SB_STORAGE_EJECT_REQUEST, SB_STORAGE_UNMOUNT_OBSERVED, SB_STORAGE_FAILURE } sb_storage_event_kind;
typedef struct {
 sb_storage_event_kind kind;
 unsigned slot;
 uint64_t sequence, expected_generation, now_ms;
 sb_storage_mount mount;
 sb_storage_evidence evidence;
 sb_storage_type type;
 sb_storage_status error;
} sb_storage_event;
/* Single controller owner; no allocation, I/O or callbacks. Incarnation must be
 * nonzero random per process. Identity registry can be imported/exported by the
 * caller, but this module NEVER writes markers/settings or guesses a DB path. */
void sb_storage_init(sb_storage_model *, int enabled, int fab4, uint64_t incarnation);
sb_storage_status sb_storage_apply(sb_storage_model *, const sb_storage_event *, sb_storage_change *);
int sb_storage_current(const sb_storage_model *, unsigned slot, const sb_music_media *);
sb_storage_status sb_storage_import_identity(sb_storage_model *, const sb_storage_identity *);
sb_storage_status sb_storage_associate_library(sb_storage_model *,unsigned slot,const sb_music_media *,uint64_t library_id);
/* Strict bounded parsers. mountinfo is authorization source; mounts-only data
 * is discovery-only and cannot authorize a root on unsupported old kernels. */
sb_storage_status sb_storage_parse(const char *, size_t, int mountinfo, sb_storage_mounts *);
sb_storage_status sb_storage_read_mounts(void *unused, sb_storage_mounts *);
typedef sb_storage_status (*sb_storage_mount_reader)(void *, sb_storage_mounts *);
typedef struct {
 int fd;
 sb_music_media media;
 sb_storage_mount mount;
 dev_t device;
 ino_t inode;
 char relative[SB_STORAGE_PATH]; /* validated scope within mount; empty = full mount */
} sb_storage_root;
/* Blocking worker APIs, NEVER call on Jive/protocol loops. No writes. */
sb_storage_status sb_storage_validate(const char *approved_parent, const sb_storage_mount *,
 const sb_music_media *, sb_storage_mount_reader, void *, sb_storage_root *);
sb_storage_status sb_storage_root_check(const sb_storage_root *, const sb_music_media *,
 sb_storage_mount_reader, void *);
sb_storage_status sb_storage_open_relative(const sb_storage_root *, const sb_music_media *,
 const char *relative, int directory, sb_storage_mount_reader, void *, int *fd);
void sb_storage_root_close(sb_storage_root *);
/* Opt-in no-atime reads, including descendants; no mount changes. */
sb_storage_status sb_storage_root_noatime(sb_storage_root *);
sb_storage_status sb_storage_root_child(const sb_storage_root *,const sb_music_media *,const char *relative,
 sb_storage_mount_reader,void *,sb_storage_root *);
sb_storage_status sb_storage_identity_linux(const sb_storage_mount *, sb_storage_evidence *);
/* Feature-gated Fab4 single-flight validation bridge. No daemon, timer, polling,
 * new listener or stock MediaManager patch. Caller schedules bounded retries
 * for delayed mounts; notifications are hints, fresh OS state is authoritative. */
typedef struct sb_storage_worker sb_storage_worker;
typedef struct {
 int enabled, fab4;
 char approved_parent[SB_STORAGE_PATH];
 sb_storage_mount_reader reader; void *reader_context; /* NULL = real /proc */
} sb_storage_worker_options;
typedef struct {
 uint64_t request_id;
 sb_storage_status status;
 sb_storage_mounts mounts;
 sb_storage_status validation[SB_STORAGE_MOUNTS_MAX];
 sb_storage_evidence evidence[SB_STORAGE_MOUNTS_MAX];
} sb_storage_discovery;
sb_storage_worker *sb_storage_worker_start(const sb_storage_worker_options *);
int sb_storage_worker_fd(const sb_storage_worker *);
sb_storage_status sb_storage_worker_submit(sb_storage_worker *, uint64_t request_id);
sb_storage_status sb_storage_worker_receive(sb_storage_worker *, sb_storage_discovery *);
sb_storage_status sb_storage_worker_stop(sb_storage_worker *); /* async */
sb_storage_status sb_storage_worker_destroy(sb_storage_worker *); /* only exited */
#endif
