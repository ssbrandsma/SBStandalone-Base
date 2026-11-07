# Standalone Music WP1 Evidence Index

Completed 2026-11-02. Base HEAD remains `0a4b50e836446bdaf03804fe1a5477a139742243`; WP0/Phase 0 pre-existing uncommitted work was retained. WP1 changes are the isolated library/build/tests plus small WP0 query/provider type extensions. Historical Phase 0/WP0 source indexes were not rewritten.

[Source ledger](STANDALONE_MUSIC_WP1_SOURCES.json) records current exact hashes and anchors. [Measurement artifact](STANDALONE_MUSIC_WP1_MEASUREMENTS.json) records benchmarks, build sizes/hashes and log fingerprints. The input research set was read and hashed in ignored `.local/wp1/inputs.json`.

## CMakeLists.txt

SHA256: `11256202f20453fbd3ae6db4cb7238b89a372975d31293e9bd88da811ff164e3`


## native/common/music_contracts.h

SHA256: `045ce2b3a266872662f3609253a3c2cb026e43d70736c3848b0eeb797d21a58d`

- Line 119: `int sb_music_source_equal(const sb_music_source_id *, const sb_music_source_id *);`
- Line 120: `int sb_music_media_current(const sb_music_media *, const sb_music_media *);`
- Line 121: `int sb_music_yield_ack(sb_music_ownership *, uint64_t request_id,`
- Line 123: `int sb_music_can_commit_owner(const sb_music_ownership *, uint64_t now_ms);`
- Line 124: `int sb_music_stream_event(sb_music_stream_guard *, const sb_music_stream_id *, sb_music_event);`
- Line 125: `int sb_music_queue_update_valid(const sb_music_queue_snapshot *, const sb_music_queue_update *);`
- Line 126: `int sb_music_query_valid(const sb_music_query *);`
- Line 127: `int sb_music_resource_current(const sb_music_resource *, const sb_music_media *,`
- Line 129: `int sb_music_range_valid(uint64_t size, const sb_music_byte_range *);`

## native/common/music_contracts.c

SHA256: `07854ba2cec9b0b846e39b2c8bedd7c5be3438aa39a3a272957c5d1875cfd8a6`

- Line 4: `int sb_music_source_equal(const sb_music_source_id *a, const sb_music_source_id *b)`
- Line 9: `int sb_music_media_current(const sb_music_media *a, const sb_music_media *b)`
- Line 14: `int sb_music_yield_ack(sb_music_ownership *o, uint64_t request, uint64_t generation,`
- Line 25: `int sb_music_can_commit_owner(const sb_music_ownership *o, uint64_t now)`
- Line 31: `static int stream_equal(const sb_music_stream_id *a, const sb_music_stream_id *b)`
- Line 38: `int sb_music_stream_event(sb_music_stream_guard *s, const sb_music_stream_id *id, sb_music_event event)`
- Line 51: `int sb_music_queue_update_valid(const sb_music_queue_snapshot *s, const sb_music_queue_update *u)`
- Line 58: `int sb_music_query_valid(const sb_music_query *q)`
- Line 66: `int sb_music_resource_current(const sb_music_resource *r, const sb_music_media *m,`
- Line 78: `int sb_music_range_valid(uint64_t size, const sb_music_byte_range *r)`

## native/sbmusic/music_library.h

SHA256: `62f40bc7df75bc9f199260f1b64ac9438b115410f3cc90260a14605283dfdf1e`

- Line 4: `#define SB_ML_SCHEMA_VERSION 2`
- Line 62: `sb_music_library *sb_music_library_start(const sb_ml_options *);`
- Line 63: `int sb_music_library_fd(const sb_music_library *);`
- Line 66: `sb_ml_status sb_music_library_submit(sb_music_library *, const sb_ml_request *);`
- Line 67: `sb_ml_status sb_music_library_receive(sb_music_library *, sb_ml_result *);`
- Line 68: `sb_ml_status sb_music_library_cancel(sb_music_library *, uint64_t request_id); /* cooperative, identity checked */`
- Line 69: `sb_music_library_provider sb_music_library_queries(sb_music_library *);`
- Line 72: `sb_ml_status sb_music_library_destroy(sb_music_library *);`

## native/sbmusic/music_library.c

SHA256: `a99b8269bf66f276256db8c5867cfbb9e3d09df3f1a2ea7b3dba89d20050fd54`

- Line 22: `static pthread_once_t memory_policy_once=PTHREAD_ONCE_INIT;`
- Line 23: `static void memory_policy(void)`
- Line 53: `static sb_ml_status failure(sb_music_library *h,sb_ml_status status,const char *message)`
- Line 59: `static int sql_error(sb_music_library *h,int rc)`
- Line 93: `static int execute(sb_music_library *h,const char *sql,const binding *values,unsigned count)`
- Line 104: `static int script(sb_music_library *h,const char *sql)`
- Line 119: `static int scalar(sb_music_library *,const char *,const binding *,unsigned,sqlite3_int64 *);`
- Line 120: `static int refresh_revision(sb_music_library *h)`
- Line 132: `static int bump_revision(sb_music_library *h)`
- Line 139: `static void revision_committed(sb_music_library *h)`
- Line 143: `static int name_id(sb_music_library *h,const char *table,sqlite3_int64 media,const char *name,sqlite3_int64 *id)`
- Line 152: `static int folder_id(sb_music_library *h,sqlite3_int64 media,const char *path,sqlite3_int64 *id)`
- Line 178: `static int source_update(sb_music_library *h,const sb_music_media *media)`
- Line 221: `if(!execute(h,"BEGIN IMMEDIATE",NULL,0))return 0;`

## native/sbmusic/schema.h

SHA256: `6580c54b49baef6f8742e7df7e1f30180c1c4afda76825266253ee3703130f29`


## native/sbmusic/CMakeLists.txt

SHA256: `833351fcef6b67b5719ca8fdfde4c2a4314fe65316e5838e36b8255e8fcbb936`

- Line 4: `target_compile_definitions(sbmusic_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_ENABLE_DBSTAT_VTAB SQLITE_DQS=0 _FILE_OFFSET_BITS=64)`

## native/sbmusic/third_party/sqlite/PROVENANCE.json

SHA256: `bf87f42dc33275057c28b999c1e23bf6e1d506455e47b8d9a48525f335f4abaa`


## scripts/build-arm.sh

SHA256: `b4734666932a91270fb3afe61d9024669858a42c08114e23b19b7ddd12c86a28`


## scripts/build-music-arm.sh

SHA256: `80d97a9ced08b605018e0b19c4a3cd878af71266903c2f175d8153b86c4d2668`

- Line 18: `sqlite_flags='-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_ENABLE_DBSTAT_VTAB -DSQLITE_DQS=0'`

## tests/CMakeLists.txt

SHA256: `10f4075f060f1e6132a07da1394c09beef7adda28fced0ec5824cfbb650ee3dc`


## tests/music/test_library.c

SHA256: `4d83bc3dc280ae8c03b7e73242b52c40da986adfa27b8fc6f10ea0422588697c`

- Line 51: `static sb_music_library *open_library(const char *path,int create,int readonly,int wal)`
- Line 70: `static void close_library(sb_music_library *h){sb_ml_request r={0};`
- Line 75: `static void source(sb_music_library *h){sb_ml_request r={0};`
- Line 80: `static void transaction(sb_music_library *h,sb_ml_operation operation,uint64_t id){sb_ml_request r={0};`
- Line 85: `static void fixture(sb_ml_track *t,unsigned i)`
- Line 143: `static int compare_names(const char *a,const char *b)`
- Line 159: `static void raw(sqlite3 *db,const char *sql){char *error=NULL;`
- Line 160: `int rc=sqlite3_exec(db,sql,NULL,NULL,&error);`
- Line 165: `static void correctness(const char *path,unsigned n,int wal)`
- Line 340: `raw(other,"BEGIN IMMEDIATE");`
- Line 380: `static int compare_double(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}`
- Line 381: `static void benchmark(const char *path,unsigned n)`
- Line 453: `int main(int argc,char **argv)`

## tests/music/test_library_storage.py

SHA256: `ea6fcd6b68436efd8b0f2988df1f954308b1116796424031c2374de2c142a2ff`

- Line 14: `def run(*args):`
- Line 21: `def digest(path):`

## tests/music/test_library_large.py

SHA256: `22efdad7dca7a48d71581a27552fb253924e1dba706fba1fd13eeac290cb3b68`


## tests/music/test_library_qemu.py

SHA256: `2b71cbf4dbe472049e0074a453359b849588546ca6fafd7e3112b9f2a6d53974`


## tests/music/benchmark_library.py

SHA256: `fc69ba5e4b9d0c1e8b71c66bbc7988f9ddc80dde1d8808cb229ee6cc4fd74382`


## native/sbmusic/third_party/sqlite/sqlite3.c

SHA256: `0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52`


## native/sbmusic/third_party/sqlite/sqlite3.h

SHA256: `d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c`


## Evidence classifications

- Confirmed by source and host tests: native schema/versioning, prepared operations, stable IDs, bounded query DTOs, asynchronous single-flight worker, transactions/snapshots, revision refresh, input validation and disabled product integration.
- Confirmed by controlled host faults/copies: read-only writes refused, lock busy response, process-crash hot-journal recovery, injected write EIO revocation, allocator limit, migration/newer/foreign/corrupt-copy preservation.
- Confirmed by cross-build and user-mode QEMU only: ARMv5/ARMv6 static compilation and library/contract functional execution. No actual target-kernel/filesystem/audio qualification is implied.
- Targets/unverified: device CPU/RAM/latency, actual Baby memory, removable filesystem locking/fsync/power loss, read-only WAL sidecar constraints on hardware, slow kernel I/O cancellation.

Raw ignored logs: baseline-tests.log, final-tests.log, library-final-tests.log, storage-tests.log, large-tests.log, sanitizers.log, build-arm.log, music-arm-final.log, cross-contracts.log, qemu-tests.log, proxy-probe.log and performance.json under `.local/wp1/`. No device credentials or live media were collected.

SQLite archive/source provenance is pinned in the vendored PROVENANCE.json. Primary API references: [thread ownership](https://www.sqlite.org/threadsafe.html), [journaling pragmas](https://www.sqlite.org/pragma.html#pragma_journal_mode), [WAL requirements](https://www.sqlite.org/wal.html), [allocator limits](https://www.sqlite.org/c3ref/hard_heap_limit64.html), [official download](https://www.sqlite.org/download.html). Runtime/library behavior is evidenced by the pinned source and executed tests, not inferred from current documentation alone.
