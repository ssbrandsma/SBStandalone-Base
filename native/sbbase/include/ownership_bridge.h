#ifndef SB_OWNERSHIP_BRIDGE_H
#define SB_OWNERSHIP_BRIDGE_H
#include "slim_controller.h"
#define SB_OB_MESSAGE 384
#define SB_OB_REPLY 1024

typedef struct {
	uint64_t incarnation, sequence, seen_ms;
	char command[SB_OB_MESSAGE + 1], reply[SB_OB_REPLY];
	int status;
} sb_ob_adapter;

typedef int (*sb_ob_music_proof)(void *,
				 uint64_t adapter,
				 uint64_t connection,
				 uint64_t generation,
				 uint64_t now);

typedef struct {
	sb_ob_music_proof music_proof;
	int (*music_begin)(void *, uint64_t);
	int (*music_ready)(void *);
	int last_music_ready;
	void *music_context;
	sb_physical_owner *owner;
	sb_ob_adapter adapters[5];
	uint64_t next_incarnation, observed_connection;
	int reconnect_pending;
	unsigned unavailable_mask, startup_native_absent_mask;
	unsigned idle_mask, admission_holds;
	int alarm_hold;
	int enabled, token_dir;
	char token[65], token_name[128];
	uint64_t token_dev, token_inode;
} sb_ownership_bridge;

/* Explicit provisioning only, before event-loop entry. Parent must be owned
 * by effective uid, mode0700; token absent. Creates mode0600, random per start.
 */
int sb_ob_open(sb_ownership_bridge *,
	       sb_physical_owner *,
	       const char *token_path);
void sb_ob_bind_music(sb_ownership_bridge *,
		      sb_ob_music_proof,
		      int (*)(void *, uint64_t),
		      int (*)(void *),
		      void *);
void sb_ob_close(sb_ownership_bridge *);
int sb_ob_auth(const sb_ownership_bridge *, const char *, size_t);
/* Called on the same Base loop as the owner/controller. No I/O or allocation.
 */
int sb_ob_command(sb_ownership_bridge *,
		  const char *,
		  size_t,
		  uint64_t now,
		  char *,
		  size_t);
int sb_ob_snapshot(
	sb_ownership_bridge *, unsigned adapter, uint64_t now, char *, size_t);
void sb_ob_tick(sb_ownership_bridge *, uint64_t now);
void sb_ob_alarm(sb_ownership_bridge *, int hold);
void sb_ob_connection(sb_ownership_bridge *, uint64_t connection, int attached);
#endif
