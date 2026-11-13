#ifndef SB_SLIM_CONTROLLER_H
#define SB_SLIM_CONTROLLER_H
#include "../../common/music_contracts.h"
#include <stddef.h>
#include <stdint.h>

/* Single event-loop owner; no allocations, threads, sockets or activation. */
#define SB_SLIM_CAPS_MAX 1024
#define SB_SLIM_HTTP_MAX 4096
#define SB_SLIM_CODEC_MP3 1u
#define SB_SLIM_CODEC_FLAC 2u
#define SB_SLIM_CODEC_VORBIS 4u

typedef struct {
	unsigned char device, revision, mac[6], uuid[16];
	uint16_t flags;
	uint64_t bytes_received;
	unsigned codecs;
	int reconnect, has_uuid;
	char locale[3], model[32], firmware[64],
		capabilities[SB_SLIM_CAPS_MAX + 1];
} sb_slim_hello;

typedef enum {
	SB_SLIM_UNKNOWN,
	SB_SLIM_HELO,
	SB_SLIM_STAT,
	SB_SLIM_RESP,
	SB_SLIM_DSCO
} sb_slim_event_kind;

typedef struct {
	sb_slim_event_kind kind;
	sb_slim_hello hello;
	char code[5];
	uint32_t decode_size, decode_full, output_size, output_full;
	uint64_t bytes_received;
	uint32_t jiffies, elapsed_ms, server_timestamp;
	uint16_t signal, voltage, error_code, http_status;
	unsigned reason;
} sb_slim_event;

/* 1 valid, 0 unsupported opcode, -1 malformed. Output changes only on success.
 */
int sb_slim_parse(const char opcode[4],
		  const unsigned char *,
		  size_t,
		  sb_slim_event *);

/* Only local opaque resource paths; no caller-supplied host, headers or URL. */
typedef struct {
	char codec;
	unsigned port;
	char token[65];
	unsigned threshold_kib, output_threshold;
} sb_slim_start;

size_t sb_slim_strm(char command,
		    uint32_t interval_or_timestamp,
		    const sb_slim_start *,
		    unsigned char *,
		    size_t);
size_t sb_slim_audg(uint32_t left,
		    uint32_t right,
		    uint32_t sequence,
		    unsigned char *,
		    size_t);
size_t sb_slim_aude(unsigned enabled, unsigned char *, size_t);
size_t sb_slim_cont(unsigned char *, size_t); /* no ICY/loop/GUID */

typedef enum {
	SB_T_DISCONNECTED,
	SB_T_CONNECTED,
	SB_T_IDLE,
	SB_T_PREPARING,
	SB_T_CONNECTING_STREAM,
	SB_T_BUFFERING,
	SB_T_PLAYING,
	SB_T_PAUSED,
	SB_T_STOPPING,
	SB_T_DRAINING,
	SB_T_STOPPED,
	SB_T_ERROR
} sb_transport_state;

typedef enum {
	SB_T_OK,
	SB_T_HTTP_ERROR,
	SB_T_CODEC_ERROR,
	SB_T_INPUT_ERROR,
	SB_T_TIMEOUT,
	SB_T_CONNECTION_LOST,
	SB_T_PROTOCOL_ERROR
} sb_transport_error;

typedef struct {
	int enabled, attached, quiescent, used_connection, have_jiffies;
	sb_slim_hello hello;
	uint64_t connection, last_connection, owner_generation, last_epoch;
	sb_music_stream_guard stream;
	sb_transport_state state;
	sb_transport_error error;
	uint64_t deadline_ms, elapsed_ms, revision;
	uint32_t last_jiffies;
	uint16_t seen_progress;
	char last_status[4];
	unsigned http_status, input_reason, terminal_notifications;
	char pending; /* pause/resume/stop awaiting confirmation */
} sb_transport;

int sb_transport_init(sb_transport *, int enabled);
int sb_transport_attach(sb_transport *,
			uint64_t connection,
			const sb_slim_hello *);
void sb_transport_detach(sb_transport *, uint64_t connection);
int sb_transport_quiescent(sb_transport *,
			   uint64_t connection,
			   uint64_t owner_generation);
int sb_transport_prepare(sb_transport *,
			 const sb_music_stream_id *,
			 char codec,
			 uint64_t now);
int sb_transport_command(sb_transport *,
			 const sb_music_stream_id *,
			 char command,
			 uint64_t now);
int sb_transport_stopped(sb_transport *,
			 const sb_music_stream_id *,
			 int confirmed_quiescent);
int sb_transport_release_stream(sb_transport *);
int sb_transport_event(sb_transport *,
		       uint64_t connection,
		       const sb_music_stream_id *,
		       const sb_slim_event *,
		       uint64_t now);
int sb_transport_tick(sb_transport *, uint64_t now);

/* Adapters are explicit registrations/incarnations and a pull-based pending
 * yield. No metadata-manager API can acknowledge these three physical facts. */
typedef struct {
	int enabled, connected, baseline_idle, suspended;
	sb_music_ownership value;
	uint64_t registrations[5], last_request, revision;
} sb_physical_owner;

int sb_owner_init(sb_physical_owner *, int enabled);
int sb_owner_register(sb_physical_owner *,
		      sb_music_owner,
		      uint64_t adapter_incarnation);
int sb_owner_connect(sb_physical_owner *,
		     uint64_t connection,
		     int confirmed_idle);
void sb_owner_disconnect(sb_physical_owner *, uint64_t connection);
/* 1 committed, 2 pending yield, 0 refused/stale/busy. timeout in 1..10000 ms.
 */
int sb_owner_request(sb_physical_owner *,
		     sb_music_owner,
		     uint64_t request,
		     uint64_t now,
		     unsigned timeout_ms);
int sb_owner_ack(sb_physical_owner *,
		 sb_music_owner previous,
		 uint64_t adapter,
		 uint64_t request,
		 uint64_t generation,
		 uint64_t connection,
		 uint64_t now,
		 int accepted,
		 int producer_stopped,
		 int hooks_released,
		 int decoder_quiescent);
int sb_owner_release(sb_physical_owner *,
		     sb_music_owner,
		     uint64_t adapter,
		     uint64_t generation,
		     uint64_t connection,
		     int producer_stopped,
		     int hooks_released,
		     int decoder_quiescent);
int sb_owner_crashed(sb_physical_owner *, sb_music_owner, uint64_t adapter);
int sb_owner_tick(sb_physical_owner *, uint64_t now);
void sb_owner_suspend(sb_physical_owner *,
		      int suspended); /* alarm/sleep admission hold */
int sb_owner_controls(const sb_physical_owner *,
		      sb_music_owner,
		      uint64_t generation,
		      uint64_t connection);
/* Future Music entry points: do not bypass arbitration with the low-level
 * state-machine methods. Connection quiescence must be separately confirmed. */
int sb_transport_owned_prepare(sb_transport *,
			       const sb_physical_owner *,
			       const sb_music_stream_id *,
			       char codec,
			       uint64_t now);
int sb_transport_owned_command(sb_transport *,
			       const sb_physical_owner *,
			       const sb_music_stream_id *,
			       char command,
			       uint64_t now);
#endif
