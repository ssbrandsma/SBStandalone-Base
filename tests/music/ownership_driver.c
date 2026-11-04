#define _POSIX_C_SOURCE 200809L
#include "ownership_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static sb_physical_owner owner;
static int resources_ready = 0;

static int music_proof(void *ctx,
		       uint64_t adapter,
		       uint64_t connection,
		       uint64_t generation,
		       uint64_t now)
{
	(void)ctx;
	if (!resources_ready)
		return 0;
	if (owner.value.current == SB_OWNER_NONE)
		return 1;
	if (owner.value.yield == SB_YIELD_REQUESTED)
		return sb_owner_ack(&owner,
				    SB_OWNER_MUSIC,
				    adapter,
				    owner.value.request_id,
				    generation,
				    connection,
				    now,
				    1,
				    1,
				    1,
				    1);
	return sb_owner_release(&owner,
				SB_OWNER_MUSIC,
				adapter,
				generation,
				connection,
				1,
				1,
				1);
}

int main(void)
{
	char dir[] = "/tmp/sb-owner-protocol-XXXXXX", path[256], line[512],
	     reply[SB_OB_REPLY];
	assert(mkdtemp(dir));
	snprintf(path, sizeof path, "%s/token", dir);
	sb_ownership_bridge bridge;
	assert(sb_owner_init(&owner, 1));
	assert(sb_ob_open(&bridge, &owner, path));
	assert(sb_ob_auth(&bridge, bridge.token, 64));
	assert(!sb_ob_auth(&bridge, "invalid", 7));
	sb_ob_bind_music(&bridge, music_proof, NULL, NULL, NULL);
	sb_ob_connection(&bridge, 1, 1);
	uint64_t now = 1;
	while (fgets(line, sizeof line, stdin)) {
		line[strcspn(line, "\n")] = 0;
		unsigned long long value;
		if (sscanf(line, "!time %llu", &value) == 1) {
			now = (uint64_t)value;
			sb_ob_tick(&bridge, now);
			puts("{\"status\":200}");
		} else if (sscanf(line, "!connect %llu", &value) == 1) {
			sb_ob_connection(&bridge, (uint64_t)value, 1);
			puts("{\"status\":200}");
		} else if (sscanf(line, "!ready %llu", &value) == 1) {
			resources_ready = !!value;
			puts("{\"status\":200}");
		} else if (sscanf(line, "!hold %llu", &value) == 1) {
			sb_owner_suspend(&owner, !!value);
			puts("{\"status\":200}");
		} else {
			int status = sb_ob_command(&bridge,
						   line,
						   strlen(line),
						   now,
						   reply,
						   sizeof reply);
			if (status == 200 || status == 202)
				printf("{\"status\":%d,\"reply\":%s}\n",
				       status,
				       reply);
			else
				printf("{\"status\":%d}\n", status);
		}
		fflush(stdout);
	}
	sb_ob_close(&bridge);
	assert(access(path, F_OK));
	assert(!rmdir(dir));
	return 0;
}
