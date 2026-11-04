#ifndef SB_WEB_MUSIC_STREAM_H
#define SB_WEB_MUSIC_STREAM_H
#include "../../common/music_resource_ipc.h"
#define SB_STREAM_JOBS 8u
#define SB_STREAM_BUFFER 16384u
typedef struct sb_stream_registry sb_stream_registry;
/* Explicit private IPC only. Disabled does not allocate or adopt fd. */
sb_stream_registry *sb_stream_registry_start(int enabled, int ipc);
/* Called by existing web event loop; at most eight bounded messages per tick.
 * Peer loss revokes everything; no music listener or filesystem paths. */
int sb_stream_registry_tick(sb_stream_registry *, uint64_t now);
/* 200 admitted, 404 unknown, 410 revoked/expired, 503 busy. */
int sb_stream_info(sb_stream_registry *,
		   const unsigned char token[32],
		   unsigned method,
		   uint64_t now,
		   sb_music_resource *);
/* Admission copies one descriptor capability; returns nonzero job ID. */
uint64_t sb_stream_read(sb_stream_registry *,
			const unsigned char token[32],
			uint64_t offset,
			unsigned length,
			uint64_t now);
/* 0 pending, 1 ready; status200 or410/503/500. Data copied to caller. */
int sb_stream_result(sb_stream_registry *,
		     uint64_t job,
		     uint64_t now,
		     unsigned char data[SB_STREAM_BUFFER],
		     unsigned *length,
		     int *status);
int sb_stream_cancel(sb_stream_registry *,
		     uint64_t job); /* 0 BUSY: retain ID and retry */
int sb_stream_registry_stop(sb_stream_registry *);    /* 0 BUSY: retry */
int sb_stream_registry_destroy(sb_stream_registry *); /* 0 while worker alive */
#endif
