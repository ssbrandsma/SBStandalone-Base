#ifndef SB_OUTPUT_QUEUE_H
#define SB_OUTPUT_QUEUE_H
#include <stddef.h>
#include <stdint.h>
/* Lazy allocation only when bytes cannot be sent immediately. Single loop. */
#define SB_OUTPUT_CAPACITY 65536u
#define SB_OUTPUT_BUDGET 16384u
#define SB_OUTPUT_TIMEOUT_MS 15000u

typedef struct {
	unsigned char *data;
	size_t offset, length;
	uint64_t since_ms;
	uint64_t connection, owner_generation, stream_epoch;
} sb_output;

/* Queue success means accepted in order, not delivered/acknowledged. */
int sb_output_enqueue(
	sb_output *, int fd, const void *, size_t, uint64_t now_ms);
/* Returns -1 close required, 0 pending, 1 drained. <=4 sends / 16 KiB. */
int sb_output_flush(sb_output *, int fd, uint64_t now_ms);
int sb_output_expired(const sb_output *, uint64_t now_ms);
void sb_output_clear(sb_output *);
/* Scope a future controller connection. Revoking pending bytes requires closing
 * its socket, never splicing/truncating an already partly written wire frame.
 */
int sb_output_scope(sb_output *,
		    uint64_t connection,
		    uint64_t owner,
		    uint64_t epoch);
int sb_output_scoped(sb_output *,
		     int fd,
		     uint64_t connection,
		     uint64_t owner,
		     uint64_t epoch,
		     const void *,
		     size_t,
		     uint64_t now_ms);
#endif
