/*
 * Local LMS-compatible service and protocol event loop.
 *
 * Loads the applet catalog and coordinates discovery, SlimProto, HTTP and
 * CometD connections for Jive. Uses protocol.c for wire-format responses and
 * nowplaying.c for playback state and commands shared with audio integrations.
 */

#define _POSIX_C_SOURCE 200809L

#if defined(SB_MUSIC_BROWSE_TESTING) || defined(SB_MUSIC_RUNTIME)
#define SB_MUSIC_BROWSE_INTEGRATION 1
#endif
#if defined(SB_MUSIC_PLAYBACK_TESTING) || defined(SB_MUSIC_RUNTIME)
#define SB_MUSIC_PLAYBACK_INTEGRATION 1
#endif

#include "sbbase.h"
#include "nowplaying.h"
#include "lms_command.h"
#include "slim_controller.h"
#include "ownership_bridge.h"
#include "../../sblifecycle/tinysc.h"
#include "output_queue.h"
#ifdef SB_MUSIC_BROWSE_INTEGRATION
#include "music_browse.h"
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
#include "music_playback.h"
#endif
#include <stdarg.h>
#endif
#include "../../common/music_contracts.h"
#include "../../common/upnp_ipc.h"
#include "alarms.h"
#include "sleep.h"
#include "../../common/firmware_state.h"
#include "../../common/applet_compat.h"

#include <arpa/inet.h>
#include <ctype.h>

#include <errno.h>

#include <fcntl.h>

#include <netdb.h>

#include <poll.h>

#include <signal.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>
#include <strings.h>

#include <sys/socket.h>

#include <sys/stat.h>

#include <sys/time.h>

#include <time.h>

#include <unistd.h>

#define CLIENTS 32

#define HTTP_IDLE_SECONDS 300

#define STREAM_HEARTBEAT_SECONDS 2

typedef struct {
	int fd, kind, helo, close_after_drain, http_resume;
	int upnp_fd;
	uint64_t upnp_request,upnp_deadline;
	char *upnp_reply;
	size_t upnp_length,upnp_sent;
	sb_upnp_request *upnp_selection;
	int upnp_edit;
	void *upnp_items;
	unsigned upnp_item_count,upnp_update_id,upnp_page_count;
	uint64_t upnp_intent;
	sb_output output;
#ifdef SB_MUSIC_BROWSE_INTEGRATION
	struct music_http *music_http;
	sb_lms_command *music_subscription;
	sb_browse_call *music_sub_call;
	char music_menu_path[160], music_path[160];
	uint64_t music_menu_version, music_view_version, music_output_epoch;
	int music_menu_visible;
#endif
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
	uint64_t music_connection, music_control_context;
#endif
	int ownership_wait;
	uint64_t ownership_revision, ownership_deadline, ownership_connection;
	int playerstatus_subscribed;
	time_t keepalive;
	/* Last strm-t poll sent. Incoming traffic must not move this:
	 * a chattering peer (decoder STMt) would otherwise suppress the
	 * idle poll and trip Jive's 35 s READ_TIMEOUT. */
	time_t slim_poll;
	size_t used;
	char client_id[32];
	char subscribed_player[18];
	char slim_player[18];
	unsigned char buf[SB_MAX_HTTP_HEADER + SB_MAX_HTTP_BODY + 1];
} client;

#ifdef SB_MUSIC_BROWSE_INTEGRATION
static sb_browse *music_browse;
static sb_music_library *music_library;
static sb_music_media music_media_snapshot = {{{1}}, 1, 1};
static void music_dispose(client *);
static int music_tail(client *, const char *, size_t);
static void music_inherit(client *, client *);
#endif
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
static sb_music_playback *local_music;
#ifdef SB_MUSIC_RUNTIME
#define local_owner production_owner
#else
static sb_physical_owner local_owner;
#endif
static uint64_t local_connection;
#ifndef SB_MUSIC_RUNTIME
static int local_mock_adapters;
#endif
static int playback_command(client *, struct music_http *);
static void playback_service(void);
#ifndef SB_MUSIC_RUNTIME
static int playback_test_route(client *, const char *);
#endif
#endif
static int player_connected;
static int test_nowplaying;
static np_manager *nowplaying;
static sb_alarms *alarms;

static char player_id[18];
#ifdef SB_TARGET_FAB4
static const char *player_model = "fab4",
		  *player_modelname = "Squeezebox Touch";
#else
static const char *player_model = "baby",
		  *player_modelname = "Squeezebox Radio";
#endif

static client *clients;

typedef struct {
	char client_id[32], channel[160], version[64], machine[16], last[512];
	time_t seen;
} firmware_subscription;

static firmware_subscription firmware_subscriptions[CLIENTS];

static char catalog_data[65536] = "{\"count\":0,\"item_loop\":[]}";
typedef struct {
    size_t offset, length;
    char target[64], name[65], version[33], minimum[64], maximum[64];
    int blocked;
} catalog_entry;
static catalog_entry catalog_entries[128];
static size_t catalog_entry_count;

static sb_config *active_config;
static uint64_t upnp_intent_generation;

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t exit_signal = 0;
static sb_tinysc *tinysc_lifecycle;
#ifdef SB_MUSIC_RUNTIME
static void music_runtime_tick(void);
static int music_safety_allowed(void);
static int music_browse_only;
static int music_idle_only;
#endif
static volatile sig_atomic_t reload_catalog = 0;

static int
json_string(const char *body, const char *key, char *out, size_t cap);

/**
 * @brief Handle stop.
 * @param sig Numeric value for sig.
 */
static void stop(int sig)
{
	exit_signal = sig;
	running = 0;
}

/* Death-rattle diagnostics: a silent process exit is unacceptable in a
 * supervised production service. Fatal signals announce themselves with
 * async-signal-safe writes, then re-raise for the canonical exit status.
 * Clean shutdowns report the terminating signal (0 = internal exit). */
static void fatal(int sig)
{
	const char *m = "MUSIC_FATAL signal\n";
	if (sig == SIGSEGV)
		m = "MUSIC_FATAL SIGSEGV\n";
	else if (sig == SIGABRT)
		m = "MUSIC_FATAL SIGABRT\n";
	else if (sig == SIGBUS)
		m = "MUSIC_FATAL SIGBUS\n";
	else if (sig == SIGILL)
		m = "MUSIC_FATAL SIGILL\n";
	else if (sig == SIGFPE)
		m = "MUSIC_FATAL SIGFPE\n";
	(void)write(STDERR_FILENO, m, strlen(m));
	signal(sig, SIG_DFL);
	raise(sig);
}

static void death_notice(void)
{
	fprintf(stderr, "MUSIC_EXIT terminating_signal=%d\n",
		(int)exit_signal);
}

/**
 * @brief Request a configuration reload after a signal.
 * @param sig Numeric value for sig.
 */
static void reload(int sig)
{
	(void)sig;
	reload_catalog = 1;
}

/**
 * @brief Put a file descriptor into nonblocking mode.
 * @param fd Open file descriptor.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int nonblock(int fd)
{
	int f = fcntl(fd, F_GETFL, 0);
	return f < 0 || fcntl(fd,F_SETFD,FD_CLOEXEC)<0 ? -1 : fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

/**
 * @brief Create a listening loopback socket.
 * @param type Numeric value for type.
 * @param port Network port number.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int listener(int type, unsigned port)
{

	int fd = socket(AF_INET, type, 0), yes = 1;
	struct sockaddr_in a;

	if (fd < 0)
		return -1;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = htons((unsigned short)port);

	if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 ||
	    (type == SOCK_STREAM && listen(fd, 8) < 0)) {
		close(fd);
		return -1;
	}
	if(nonblock(fd)<0){close(fd);return -1;}
	return fd;
}

/**
 * @brief Close client.
 * @param c Client connection state.
 */
static sb_ownership_bridge ownership_bridge;
static sb_physical_owner production_owner;
#ifndef SB_MUSIC_RUNTIME
static uint64_t ownership_connection_sequence;
#endif

static void close_client(client *c)
{
 if(c->upnp_fd>=0)close(c->upnp_fd);
 c->upnp_fd=-1;free(c->upnp_reply);c->upnp_reply=NULL;free(c->upnp_selection);c->upnp_selection=NULL;free(c->upnp_items);c->upnp_items=NULL;
 if(c->ownership_connection)sb_ob_connection(&ownership_bridge,c->ownership_connection,0);
 c->ownership_connection=0;c->ownership_wait=0;
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
	if (local_music && c->music_connection)
		sb_playback_detach(local_music, c->music_connection);
	c->music_connection = c->music_control_context = 0;
#endif
#ifdef SB_MUSIC_BROWSE_INTEGRATION
	music_dispose(c);
#endif
	if (c->fd >= 0)
		close(c->fd);
	sb_output_clear(&c->output);
	c->close_after_drain = 0;
	c->http_resume = 0;
	c->fd = -1;
	c->used = 0;
	c->client_id[0] = 0;
	c->playerstatus_subscribed = 0;
	c->subscribed_player[0] = 0;
}

/**
 * @brief Send data.
 * @param c Client connection state.
 * @param data Data supplied to the operation.
 * @param size Size of the supplied buffer or object.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int send_data(client *c, const char *data, size_t size)
{
#ifdef SB_MUSIC_BROWSE_INTEGRATION
	if (c->music_http) {
		int r = music_tail(c, data, size);
		if (r >= 0) {
			if (!r)
				close_client(c);
			return r;
		}
	}
#endif
	if (c->fd < 0 || c->close_after_drain ||
	    !sb_output_enqueue(
		    &c->output, c->fd, data, size, np_monotonic_ms())) {
		close_client(c);
		return 0;
	}
	return 1;
}

/**
 * @brief Handle http reply.
 * @param c Client connection state.
 * @param status Input text for status.
 * @param body Input text for body.
 * @param keep Whether keep is enabled.
 */
static void
http_reply(client *c, const char *status, const char *body, int keep)
{
	char h[256];
	size_t n = strlen(body);
	int l = snprintf(
		h,
		sizeof h,
		"HTTP/1.1 %s\r\nContent-Type: "
		"application/json\r\nCache-Control: "
		"no-cache\r\nContent-Length: %lu\r\nConnection: %s\r\n\r\n",
		status,
		(unsigned long)n,
		keep ? "keep-alive" : "close");
	if (!send_data(c, h, (size_t)l) || !send_data(c, body, n))
		return;
	if (!keep) {
		if (c->output.length)
			c->close_after_drain = 1;
		else
			close_client(c);
	} else
		c->keepalive = time(NULL);
}

/**
 * @brief Handle stream start.
 * @param c Client connection state.
 * @param body Input text for body.
 */
static void stream_start(client *c, const char *body)
{
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
	c->music_control_context =
		sb_queue_snapshot(sb_playback_queue(local_music)).context;
#endif
	char h[256], chunk[9216];
	size_t n = strlen(body);
	int l = snprintf(h,
			 sizeof h,
			 "HTTP/1.1 200 OK\r\nContent-Type: "
			 "application/json\r\nCache-Control: "
			 "no-cache\r\nTransfer-Encoding: "
			 "chunked\r\nConnection: keep-alive\r\n\r\n");
	if (!send_data(c, h, (size_t)l))
		return;
	l = snprintf(
		chunk, sizeof chunk, "%lx\r\n%s\r\n", (unsigned long)n, body);
	if (!send_data(c, chunk, (size_t)l))
		return;
	c->kind = 3;
	c->used = 0;
	c->keepalive = time(NULL);
}

/**
 * @brief Handle stream heartbeat.
 * @param c Client connection state.
 */
static void stream_heartbeat(client *c)
{
	if (send_data(c, "2\r\n[]\r\n", 7))
		c->keepalive = time(NULL);
}

/**
 * @brief Handle stream event.
 * @param c Client connection state.
 * @param body Input text for body.
 */
static void stream_event(client *c, const char *body)
{
	char chunk[9216];
	size_t n = strlen(body);
	int l = snprintf(
		chunk, sizeof chunk, "%lx\r\n%s\r\n", (unsigned long)n, body);
	if (send_data(c, chunk, (size_t)l))
		c->keepalive = time(NULL);
}

/**
 * @brief Handle player status json.
 * @param out Destination buffer for the result.
 * @param cap Size or count for cap.
 */
static void player_status_json(char *out, size_t cap)
{
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
	if (local_music && local_owner.value.current == SB_OWNER_MUSIC)
		sb_playback_status(local_music, out, cap);
	else
#endif
		np_playerstatus_json(nowplaying, out, cap);
	sb_alarms_status(alarms, out, cap, time(NULL));
	sb_sleep_status(out, cap, np_monotonic_ms());
}

/**
 * @brief Handle split playerstatus path.
 * @param path Filesystem path to process.
 * @param client_id Mutable text buffer for client id.
 * @param client_cap Length or capacity associated with the preceding buffer.
 * @param requested_player Mutable text buffer for requested player.
 * @param player_cap Length or capacity associated with the preceding buffer.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int split_playerstatus_path(const char *path,
				   char *client_id,
				   size_t client_cap,
				   char *requested_player,
				   size_t player_cap)
{
	const char *marker = strstr(path, "/slim/playerstatus/");
	size_t client_len, player_len;
	if (!marker || path[0] != '/')
		return 0;
	client_len = (size_t)(marker - path - 1);
	player_len = strlen(marker + strlen("/slim/playerstatus/"));
	if (!client_len || client_len >= client_cap || player_len != 17 ||
	    player_len >= player_cap)
		return 0;
	memcpy(client_id, path + 1, client_len);
	client_id[client_len] = 0;
	memcpy(requested_player,
	       marker + strlen("/slim/playerstatus/"),
	       player_len + 1);
	return 1;
}

/**
 * @brief Handle stream for id.
 * @param id Identifier of the requested item.
 * @return Pointer to the requested object, or `NULL` when unavailable.
 */
static client *stream_for_id(const char *id)
{
	int i;
	for (i = 0; i < CLIENTS; i++)
		if (clients[i].fd >= 0 && clients[i].kind == 3 &&
		    !strcmp(clients[i].client_id, id))
			return &clients[i];
	return NULL;
}

/* WP4a.6: Jive uses one CID for short requests and a different CID for
 * its long-poll stream, so a per-stream subscription flag can never
 * cover it: sets keyed by stream CID never match the request CID, and
 * pushes addressed to the stream CID land on a channel Jive ignores.
 * Track subscribed request-CIDs globally and broadcast each addressed
 * snapshot to every live stream (loopback-only listener: the only
 * consumers are Jive and local probes, which route by channel). */
#define SB_SUBSCRIBED_CIDS 8
static char subscribed_cids[SB_SUBSCRIBED_CIDS][32];

static void subscribed_cid_add(const char *cid)
{
	int i;
	if (!cid || !cid[0])
		return;
	for (i = 0; i < SB_SUBSCRIBED_CIDS; i++)
		if (!strcmp(subscribed_cids[i], cid))
			return;
	for (i = 0; i < SB_SUBSCRIBED_CIDS; i++)
		if (!subscribed_cids[i][0]) {
			snprintf(subscribed_cids[i],
				 sizeof subscribed_cids[i],
				 "%s",
				 cid);
			return;
		}
	/* Old request identities can outlive their disconnected Jive stream.
	 * Keep this cache bounded without permanently refusing new subscribers. */
	memmove(subscribed_cids,subscribed_cids+1,
		(SB_SUBSCRIBED_CIDS-1)*sizeof subscribed_cids[0]);
	snprintf(subscribed_cids[SB_SUBSCRIBED_CIDS-1],
		sizeof subscribed_cids[0],"%s",cid);
}

static void subscribed_cid_clear(const char *cid)
{
	int i;
	if (!cid || !cid[0])
		return;
	for (i = 0; i < SB_SUBSCRIBED_CIDS; i++)
		if (!strcmp(subscribed_cids[i], cid))
			subscribed_cids[i][0] = 0;
}

static int subscribed_cid_any(void)
{
	int i;
	for (i = 0; i < SB_SUBSCRIBED_CIDS; i++)
		if (subscribed_cids[i][0])
			return 1;
	return 0;
}

/**
 * @brief Handle publish playerstatus.
 */
static void publish_playerstatus(void)
{
	char data[6144], event[8192], channel[128];
	int i, s, subscribers = 0, delivered = 0;
	if (!player_id[0] || !subscribed_cid_any())
		return;
	player_status_json(data, sizeof data);
	for (s = 0; s < SB_SUBSCRIBED_CIDS; s++) {
		if (!subscribed_cids[s][0])
			continue;
		subscribers++;
		snprintf(channel,
			 sizeof channel,
			 "/%.31s/slim/playerstatus/%.17s",
			 subscribed_cids[s],
			 player_id);
		snprintf(event,
			 sizeof event,
			 "[{\"channel\":\"%s\",\"data\":%s}]",
			 channel,
			 data);
		for (i = 0; i < CLIENTS; i++) {
			client *c = &clients[i];
			if (c->fd < 0 || c->kind != 3 || !c->client_id[0])
				continue;
			if (test_nowplaying)
				fprintf(stderr,
					"NOWPLAYING PUSH client=%s\n",
					c->client_id);
			stream_event(c, event);
			if (c->fd >= 0)
				delivered++;
		}
	}
	if (test_nowplaying)
		fprintf(stderr,
			"NOWPLAYING DELIVERY subscribers=%d delivered=%d "
			"revision=%llu\n",
			subscribers,
			delivered,
			(unsigned long long)np_current(nowplaying)
				->playlist_revision);
}

/**
 * @brief Handle register playerstatus.
 * @param path Filesystem path to process.
 */
static void register_playerstatus(const char *path)
{
	char cid[32], requested[18];
	if (!split_playerstatus_path(
		    path, cid, sizeof cid, requested, sizeof requested))
		return;
	if (!player_id[0] || strcmp(requested, player_id))
		return;
	subscribed_cid_add(cid);
	if (test_nowplaying)
		fprintf(stderr,
			"NOWPLAYING SUBSCRIBE player=%s client=%s\n",
			requested,
			cid);
}

/** Construct the exact optional firmware result, gated by private device state.
 */
static void firmware_payload(const char *version,
			     const char *machine,
			     char *out,
			     size_t capacity)
{
	char url[256], installed[64];
	if (!strcmp(machine, "baby") && sb_firmware_state(url, installed) &&
	    !strcmp(version, installed))
		snprintf(out,
			 capacity,
			 "{\"firmwareUrl\":\"%s\",\"firmwareUpgrade\":0}",
			 url);
	else
		snprintf(out, capacity, "{\"firmwareUpgrade\":0}");
}

/** Extract one literal command parameter; escaped/oversized values fail closed.
 */
static void firmware_parameter(const char *message,
			       const char *end,
			       const char *name,
			       char *out,
			       size_t capacity)
{
	out[0] = 0;
	const char *p = strstr(message, name);
	if (!p || p >= end)
		return;
	p += strlen(name);
	size_t length = strcspn(p, "\"\\\r\n");
	if (!length || length >= capacity || p + length >= end ||
	    p[length] != '"')
		return;
	memcpy(out, p, length);
	out[length] = 0;
}

/** Register one bounded logical firmware subscription, independent of TCP fd.
 */
static void firmware_subscribe(const char *path,
			       const char *version,
			       const char *machine,
			       const char *payload)
{
	if (path[0] != '/' || strlen(path) >= 160)
		return;
	const char *slash = strchr(path + 1, '/');
	if (!slash || strcmp(slash, "/slim/firmwarestatus"))
		return;
	size_t length = (size_t)(slash - path - 1);
	if (!length || length >= 32)
		return;
	for (const char *p = path + 1; p < slash; p++)
		if (!isalnum((unsigned char)*p) && *p != '-')
			return;
	firmware_subscription *slot = NULL;
	for (unsigned i = 0; i < CLIENTS; i++) {
		firmware_subscription *s = &firmware_subscriptions[i];
		if (strlen(s->client_id) == length &&
		    !memcmp(s->client_id, path + 1, length)) {
			slot = s;
			break;
		}
		if (!s->client_id[0] ||
		    time(NULL) - s->seen > HTTP_IDLE_SECONDS)
			slot = s;
	}
	if (!slot)
		return;
	memcpy(slot->client_id, path + 1, length);
	slot->client_id[length] = 0;
	snprintf(slot->channel, sizeof slot->channel, "%s", path);
	snprintf(slot->version, sizeof slot->version, "%s", version);
	snprintf(slot->machine, sizeof slot->machine, "%s", machine);
	snprintf(slot->last, sizeof slot->last, "%.511s", payload);
	slot->seen = time(NULL);
}

/** Deliver changed firmware status only, preserving ordinary Comet traffic. */
static void firmware_publish(const char *reconnected)
{
	for (unsigned i = 0; i < CLIENTS; i++) {
		firmware_subscription *s = &firmware_subscriptions[i];
		if (!s->client_id[0])
			continue;
		client *stream = stream_for_id(s->client_id);
		if (!stream) {
			if (time(NULL) - s->seen > HTTP_IDLE_SECONDS)
				memset(s, 0, sizeof *s);
			continue;
		}
		s->seen = time(NULL);
		char payload[512], event[768];
		firmware_payload(
			s->version, s->machine, payload, sizeof payload);
		if ((!reconnected || strcmp(reconnected, s->client_id)) &&
		    !strcmp(payload, s->last))
			continue;
		snprintf(event,
			 sizeof event,
			 "[{\"channel\":\"%s\",\"data\":%s}]",
			 s->channel,
			 payload);
		stream_event(stream, event);
		strcpy(s->last, payload);
	}
}

/**
 * @brief Handle unregister playerstatus.
 * @param body Input text for body.
 */
static void unregister_playerstatus(const char *body)
{
	char path[160], cid[32], requested[18];
	client *stream;
	if (!json_string(body, "\"unsubscribe\"", path, sizeof path))
		return;
	for (unsigned i = 0; i < CLIENTS; i++)
		if (!strcmp(firmware_subscriptions[i].channel, path))
			memset(&firmware_subscriptions[i],
			       0,
			       sizeof firmware_subscriptions[i]);
	if (!split_playerstatus_path(
		    path, cid, sizeof cid, requested, sizeof requested))
		return;
	stream = stream_for_id(cid);
	if (stream) {
		stream->playerstatus_subscribed = 0;
		stream->subscribed_player[0] = 0;
	}
	if (!strcmp(requested, player_id))
		subscribed_cid_clear(cid);
}

/**
 * @brief Handle publish player.
 */
static void publish_player(void)
{
	char event[2048];
	sb_config *cfg = active_config;
	int i;
	if (!cfg || !player_id[0])
		return;
	for (i = 0; i < CLIENTS; i++)
		if (clients[i].fd >= 0 && clients[i].kind == 3 &&
		    clients[i].client_id[0]) {
			snprintf(event,
				 sizeof event,
				 "[{\"channel\":\"/%s/slim/"
				 "serverstatus\",\"data\":{\"httpport\":\"%u\","
				 "\"ip\":\"%s\",\"version\":\"%s\",\"uuid\":\"%"
				 "s\",\"player "
				 "count\":1,\"players_loop\":[{\"playerindex\":"
				 "\"0\",\"playerid\":\"%s\",\"name\":"
				 "\"Standalone\",\"model\":\"%s\","
				 "\"modelname\":\"%s\",\"isplayer\":1,"
				 "\"connected\":1,"
				 "\"power\":%d,\"firmware\":\"7.7.3\",\"ip\":"
				 "\"127.0.0.1\",\"seq_no\":%u,\"displaytype\":"
				 "\"none\",\"isplaying\":%d,\"canpoweroff\":1}]"
				 "}}]",
				 clients[i].client_id,
				 cfg->http_port,
				 cfg->advertise_ip[0] ? cfg->advertise_ip
						      : "127.0.0.1",
				 cfg->lms_version,
				 cfg->uuid,
				 player_id,
				 player_model,
				 player_modelname,
				 np_player_power(nowplaying),np_player_sequence(nowplaying),
				 np_current(nowplaying)->state == NP_PLAYING);
			fprintf(stderr,
				"COMET PUSH client=%s\n",
				clients[i].client_id);
			stream_event(&clients[i], event);
		}
}

/**
 * @brief Handle token.
 * @param out Destination buffer for the result.
 */
static void token(char out[9])
{
	static unsigned long sequence;
	unsigned long x = (unsigned long)time(NULL) ^ (unsigned long)getpid() ^
			  (++sequence * 2654435761ul);
	snprintf(out, 9, "%08lx", x & 0xfffffffful);
}

/**
 * @brief Handle json unescape slashes.
 * @param s Mutable text buffer for s.
 */
static void json_unescape_slashes(char *s)
{
	char *r = s, *w = s;
	while (*r) {
		if (r[0] == '\\' && r[1] == '/')
			r++;
		*w++ = *r++;
	}
	*w = 0;
}

/**
 * @brief Validate player id.
 * @param id Identifier of the requested item.
 * @return Nonzero when the condition is true; otherwise zero.
 */
static int valid_player_id(const char *id)
{
	int i;
	if (strlen(id) != 17)
		return 0;
	for (i = 0; i < 17; i++) {
		if ((i + 1) % 3 == 0) {
			if (id[i] != ':')
				return 0;
		} else if (!((id[i] >= '0' && id[i] <= '9') ||
			     (id[i] >= 'a' && id[i] <= 'f') ||
			     (id[i] >= 'A' && id[i] <= 'F')))
			return 0;
	}
	return 1;
}

/**
 * @brief Handle json string.
 * @param body Input text for body.
 * @param key Input text for key.
 * @param out Destination buffer for the result.
 * @param cap Size or count for cap.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int json_string(const char *body, const char *key, char *out, size_t cap)
{
	const char *p = strstr(body, key);
	size_t n = 0;
	if (!p || cap < 2)
		return 0;
	p += strlen(key);
	while (*p == ' ' || *p == '\t' || *p == ':')
		p++;
	if (*p != '\"')
		return 0;
	for (p++; *p && *p != '\"'; p++) {
		if (n + 1 < cap)
			out[n++] = *p;
	}
	if (*p != '\"')
		return 0;
	out[n] = 0;
	return 1;
}

/**
 * @brief Handle json raw.
 * @param body Input text for body.
 * @param key Input text for key.
 * @param out Destination buffer for the result.
 * @param cap Size or count for cap.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int json_raw(const char *body, const char *key, char *out, size_t cap)
{
	const char *p = strstr(body, key), *start;
	size_t n;
	if (!p || cap < 2)
		return 0;
	p += strlen(key);
	while (*p == ' ' || *p == '\t' || *p == ':')
		p++;
	start = p;
	if (*p == '\"') {
		for (p++; *p && *p != '\"'; p++)
			if (*p == '\\' && p[1])
				p++;
		if (*p == '\"')
			p++;
	} else
		while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' &&
		       *p != '\t' && *p != '\r' && *p != '\n')
			p++;
	n = (size_t)(p - start);
	if (!n || n >= cap)
		return 0;
	memcpy(out, start, n);
	out[n] = 0;
	return 1;
}

/**
 * @brief Handle before.
 * @param start Input text for start.
 * @param needle Input text for needle.
 * @param end Input text for end.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int before(const char *start, const char *needle, const char *end)
{
	const char *p = strstr(start, needle);
	return p && p < end;
}

/**
 * @brief Add json.
 * @param out Destination buffer for the result.
 * @param cap Size or count for cap.
 * @param used Object referenced by used.
 * @param json Input text for json.
 */
static void add_json(char *out, size_t cap, size_t *used, const char *json)
{
	size_t n = strlen(json);
	if (*used + n + 2 >= cap)
		return;
	if (*used > 1)
		out[(*used)++] = ',';
	memcpy(out + *used, json, n);
	*used += n;
	out[*used] = 0;
}

/**
 * @brief Load catalog.
 * @param path Filesystem path to process.
 * @return Zero on success; otherwise a nonzero status or error code.
 */
static int load_catalog_file(const char *path, long maximum)
{
	FILE *f;
	char *b, *p, *start;
	long size;
	size_t used = 0;
	int count = 0, depth, in_string, escape;
    struct catalog_stage { char data[sizeof catalog_data]; catalog_entry entries[128]; } *staged = NULL;
	if (!path || !(f = fopen(path, "rb")))
		return -1;
	if (fseek(f, 0, SEEK_END) || ((size = ftell(f)) < 2) || size > maximum ||
	    fseek(f, 0, SEEK_SET)) {
		fclose(f);
		return -1;
	}
	b = malloc((size_t)size + 1);
	if (!b) {
		fclose(f);
		return -1;
	}
	if (fread(b, 1, (size_t)size, f) != (size_t)size) {
		free(staged); free(b);
		fclose(f);
		return -1;
	}
	fclose(f);
	b[size] = 0;
    if (!sb_lms_json_valid_catalog(b, (size_t)size) || !(staged = calloc(1,sizeof *staged))) { free(staged); free(b); return -1; }
	p = strstr(b, "\"applets\"");
	if (!p || (p = strchr(p, '[')) == NULL) {
		free(staged); free(b);
		return -1;
	}
	strcpy(staged->data, "{\"count\":0,\"item_loop\":[");

	used = strlen(staged->data);
	for (p++; *p && *p != ']';) {
		while (*p && *p != '{' && *p != ']')
			p++;
		if (*p != '{')
			break;
		start = p;
		depth = 0;
		in_string = escape = 0;
		do {
			char ch = *p++;
			if (in_string) {
				if (escape)
					escape = 0;
				else if (ch == '\\')
					escape = 1;
				else if (ch == '\"')
					in_string = 0;
			} else if (ch == '\"')
				in_string = 1;
			else if (ch == '{')
				depth++;
			else if (ch == '}')
				depth--;
		} while (*p && depth);
		if (depth ||
		    used + (size_t)(p - start) + 4 >= sizeof staged->data) {
			free(staged); free(b);
			return -1;
		}
		if ((size_t)count >= sizeof staged->entries / sizeof staged->entries[0]) {
			free(staged); free(b);
			return -1;
		}
		if (count)
			staged->data[used++] = ',';
		staged->entries[count].offset = used;
		staged->entries[count].length = (size_t)(p - start);
		staged->entries[count].target[0] = 0;
		{
			char saved = *p;
			*p = 0;
			json_string(start, "\"target\"", staged->entries[count].target,
				    sizeof staged->entries[count].target);
            json_string(start,"\"name\"",staged->entries[count].name,sizeof staged->entries[count].name);
            json_string(start,"\"version\"",staged->entries[count].version,sizeof staged->entries[count].version);
            json_string(start,"\"min_target_version\"",staged->entries[count].minimum,sizeof staged->entries[count].minimum);
            json_string(start,"\"max_target_version\"",staged->entries[count].maximum,sizeof staged->entries[count].maximum);
            char blocked[8]="", conflict[8]="";
            json_raw(start,"\"blocked\"",blocked,sizeof blocked);
            json_raw(start,"\"conflict\"",conflict,sizeof conflict);
            staged->entries[count].blocked=!strcmp(blocked,"true")||!strcmp(conflict,"true");
            *p = saved;
		}
		memcpy(staged->data + used, start, (size_t)(p - start));
		used += (size_t)(p - start);
		staged->data[used] = 0;
		count++;
	}
	if (used + 3 >= sizeof staged->data) {
		free(staged); free(b);
		return -1;
	}
	staged->data[used++] = ']';
	staged->data[used++] = '}';
	staged->data[used] = 0;
	{
		char prefix[32];
		size_t old = strlen("{\"count\":0");
		snprintf(prefix, sizeof prefix, "{\"count\":%d", count);
        size_t added=strlen(prefix)-old;
        if (used+added+1>=sizeof staged->data) { free(staged); free(b); return -1; }
        memmove(staged->data+strlen(prefix),staged->data+old,used-old+1);
        for(int i=0;i<count;i++)staged->entries[i].offset+=added;
        memcpy(staged->data,prefix,strlen(prefix));
	}
	memcpy(catalog_data,staged->data,sizeof catalog_data);
    memcpy(catalog_entries,staged->entries,sizeof catalog_entries);
    catalog_entry_count = (size_t)count;
	free(staged); free(b);
	return count;
}

static int load_catalog(const char *path)
{
    char snapshot[1024], expected[128], recorded[128]="", header[512];
    if (!sb_applet_config_stamp(path,expected,sizeof expected)) {
        sb_applet_snapshot_path(path,snapshot,sizeof snapshot);
        FILE *f=fopen(snapshot,"rb");
        if(f){size_t n=fread(header,1,sizeof header-1,f);header[n]=0;fclose(f);
            if(json_string(header,"\"base_revision\"",recorded,sizeof recorded)&&
               !strcmp(recorded,expected)) {
                int count=load_catalog_file(snapshot,512*1024);
                if(count>=0)return count;
            }
        }
    }
    return load_catalog_file(path,65536);
}

/**
 * Return only applets compatible with the requesting device.
 * @param target Requested model; missing target defaults to this Radio's baby.
 * @param out Response JSON buffer.
 * @param capacity Buffer size, including the terminator.
 * Untargeted and wildcard entries are shared by all models. Capacity failures
 * return an empty catalog rather than a partial or malformed JSON response.
 */
static void catalog_response(const char *target, const char *firmware, char *out, size_t capacity)
{
	char header[64];
	size_t i, used = 0, count = 0, length;

	if (!target[0])
		target = "baby";
	for (i = 0; i < catalog_entry_count; i++) {
        catalog_entry *entry=&catalog_entries[i];
        if(entry->blocked||!sb_applet_compatible(entry->target,target,entry->minimum,entry->maximum,firmware))continue;
        int superseded=0;
        for(size_t j=0;j<catalog_entry_count;j++){
            catalog_entry *other=&catalog_entries[j];
            if(i==j||strcmp(entry->name,other->name)||other->blocked||
               !sb_applet_compatible(other->target,target,other->minimum,other->maximum,firmware))continue;
            int newer=sb_applet_version_compare(other->version,entry->version);
            if(newer>0||(newer==0&&j<i)){superseded=1;break;}
        }
        if(superseded)continue;
		length = catalog_entries[i].length;
		if (used + length + 67 >= capacity)
			goto empty;
		if (count++)
			out[used++] = ',';
		memcpy(out + used, catalog_data + catalog_entries[i].offset, length);
		used += length;
	}
	length = (size_t)snprintf(header, sizeof header,
				 "{\"count\":%zu,\"item_loop\":[", count);
	if (used + length + 3 > capacity)
		goto empty;
	memmove(out + length, out, used);
	memcpy(out, header, length);
	memcpy(out + length + used, "]}", 3);
	return;
empty:
	snprintf(out, capacity, "{\"count\":0,\"item_loop\":[]}");
}

/**
 * @brief Read one bounded, unescaped command string from a JSON cursor.
 * @param cursor Current input position; advanced after a successful read.
 * @param end Exclusive request boundary.
 * @param out Destination string buffer.
 * @param cap Destination capacity including the terminator.
 * @return One on success, zero for malformed or oversized input.
 */
static int
command_string(const char **cursor, const char *end, char *out, size_t cap)
{
	const char *p = *cursor;
	size_t n = 0;

	while (p < end && isspace((unsigned char)*p))
		p++;
	if (p == end || *p++ != '"')
		return 0;
	while (p < end && *p != '"') {
		if ((unsigned char)*p < 32 || *p == '\\' || n + 1 >= cap)
			return 0;
		out[n++] = *p++;
	}
	if (p == end)
		return 0;
	out[n] = 0;
	*cursor = p + 1;
	return 1;
}

/**
 * @brief Consume a JSON delimiter and surrounding leading whitespace.
 * @param cursor Input cursor, advanced on success.
 * @param end Exclusive request boundary.
 * @param ch Required delimiter.
 * @return One when the delimiter is present, otherwise zero.
 */
static int command_delimiter(const char **cursor, const char *end, char ch)
{
	while (*cursor < end && isspace((unsigned char)**cursor))
		(*cursor)++;
	if (*cursor == end || **cursor != ch)
		return 0;
	(*cursor)++;
	return 1;
}

/**
 * @brief Send a Jive connect command to its matching SlimProto connection.
 * @param start Beginning of the current CometD request.
 * @param end Exclusive request boundary; never inspect the next batch item.
 * @return Zero for another command, one for success, minus one on failure.
 *
 * Jive sends connect to the old server, which must emit serv to the player.
 * Only literal unicast IPv4 addresses are accepted: no DNS, shell, network
 * probe, or special SqueezeNetwork destination is needed in this callback.
 */
static int switch_server(const char *start, const char *end)
{
	const char *p = strstr(start, "\"request\"");
	char player[18], command[32], address[16];
	struct in_addr ip;
	unsigned char frame[10];
	uint32_t host;
	size_t size;
	int i;

	if (!p || p >= end)
		return 0;
	p += strlen("\"request\"");
	if (!command_delimiter(&p, end, ':') ||
	    !command_delimiter(&p, end, '[') ||
	    !command_string(&p, end, player, sizeof player) ||
	    !command_delimiter(&p, end, ',') ||
	    !command_delimiter(&p, end, '[') ||
	    !command_string(&p, end, command, sizeof command) ||
	    strcmp(command, "connect"))
		return 0;
	if (!command_delimiter(&p, end, ',') ||
	    !command_string(&p, end, address, sizeof address) ||
	    !command_delimiter(&p, end, ']') ||
	    !command_delimiter(&p, end, ']') || !valid_player_id(player) ||
	    inet_pton(AF_INET, address, &ip) != 1)
		return -1;
	host = ntohl(ip.s_addr);
	if ((host >> 24) == 0 || (host >> 24) >= 224)
		return -1;
	size = sb_server_frame(
		"serv", &ip.s_addr, sizeof ip.s_addr, frame, sizeof frame);
	for (i = 0; i < CLIENTS; i++) {
		client *c = &clients[i];

		if (c->fd < 0 || c->kind != 1 || !c->helo ||
		    strcasecmp(c->slim_player, player))
			continue;
		if (!send_data(c, (const char *)frame, size))
			return -1;
		fprintf(stderr,
			"Server switch player=%s destination=%s\n",
			player,
			address);
		return 1;
	}
	return -1;
}

/**
 * @brief Find the end of one JSON message without relying on optional IDs.
 * @param p Opening brace of a message.
 * @return Position after its closing brace, or NULL for incomplete input.
 */
static const char *message_end(const char *p)
{
	int depth = 0, quoted = 0;

	for (; *p; p++) {
		if (quoted) {
			if (*p == '\\' && p[1])
				p++;
			else if (*p == '"')
				quoted = 0;
		} else if (*p == '"') {
			quoted = 1;
		} else if (*p == '{') {
			depth++;
		} else if (*p == '}' && --depth == 0) {
			return p + 1;
		}
	}
	return NULL;
}

/**
 * @brief Dispatch CometD commands and construct their response batch.
 * @param body Received JSON batch.
 * @param cfg Local server configuration.
 * @param out Response JSON buffer.
 * @param cap Capacity of the response buffer.
 */
static void
request_responses(const char *body, const sb_config *cfg, char *out, size_t cap)
{

	const char *p = body, *next;
	size_t used = 1;
	char id[32], id_field[48], reply[160], msg[65536], data[60000];
	out[0] = '[';
	out[1] = 0;

	while ((p = strchr(p, '{')) != NULL) {
		int switched;
		sb_lms_command command;
		sb_command_result parsed;
		next = message_end(p);
		if (!next)
			break;
		id[0] = 0;
		reply[0] = 0;
		if (before(p, "\"id\"", next))
			json_raw(p, "\"id\"", id, sizeof id);
		snprintf(id_field,
			 sizeof id_field,
			 "%s%s%s",
			 id[0] ? "\"id\":" : "",
			 id,
			 id[0] ? "," : "");
		if (before(p, "\"response\"", next))
			json_string(p, "\"response\"", reply, sizeof reply);
		if (!reply[0]) {
			p = next;
			continue;
		}


		parsed = sb_lms_command_parse(p, (size_t)(next - p), &command);
		if(parsed==SB_COMMAND_OK && (!strcmp(command.arguments[0],"stop") ||
		 (!strcmp(command.arguments[0],"power")&&command.count>1&&!strcmp(command.arguments[1],"0")) ||
		 (!strcmp(command.arguments[0],"pause")&&(command.count==1||strcmp(command.arguments[1],"0")))))upnp_intent_generation=0;
		if (parsed != SB_COMMAND_OK) {
			snprintf(msg, sizeof msg,
				 "{%s\"channel\":\"/slim/request\",\"successful\":false,\"error\":\"%s\"}",
				 id_field, sb_lms_command_error(parsed));
			add_json(out, cap, &used, msg);
			snprintf(msg, sizeof msg, "{%s\"channel\":\"%s\",\"data\":{\"error\":\"%s\"}}",
				 id_field, reply, sb_lms_command_error(parsed));
			add_json(out, cap, &used, msg);
			p = next;
			continue;
		}

		switched = switch_server(p, next);
		if (switched < 0) {
			snprintf(msg,
				 sizeof msg,
				 "{%s\"channel\":\"/slim/request\","
				 "\"successful\":false,\"error\":\"Server "
				 "switch failed\"}",
				 id_field);
			add_json(out, cap, &used, msg);
			p = next;
			continue;
		}
		if (switched)
			strcpy(data, "{}");
		else if (sb_sleep_request(p,
					  (size_t)(next - p),
					  player_id,
					  data,
					  sizeof data,
					  np_monotonic_ms())) {
			publish_playerstatus();
		} else if (sb_alarms_request(alarms,
					     p,
					     (size_t)(next - p),
					     player_id,
					     data,
					     sizeof data,
					     time(NULL))) {
			publish_playerstatus();
		} else if (np_lms_query_json(nowplaying,
					     p,
					     (size_t)(next - p),
					     data,
					     sizeof data)) {
			if (!strcmp(command.arguments[0], "status")) {
				sb_alarms_status(
					alarms, data, sizeof data, time(NULL));
				sb_sleep_status(
					data, sizeof data, np_monotonic_ms());
				register_playerstatus(reply);
			}
		} else if (!strcmp(command.arguments[0], "serverstatus"))
			snprintf(data,
				 sizeof data,
				 "{\"httpport\":\"%u\",\"ip\":\"%s\","
				 "\"version\":\"%s\",\"uuid\":\"%s\",\"player "
				 "count\":%d,\"players_loop\":%s}",
				 cfg->http_port,
				 cfg->advertise_ip,
				 cfg->lms_version,
				 cfg->uuid,
				 player_id[0] ? 1 : 0,
				 player_id[0] ? "PLAYER" : "[]");

		else if (!strcmp(command.arguments[0], "firmwareupgrade")) {
			char version[64], machine[16];
			firmware_parameter(p,
					   next,
					   "firmwareVersion:",
					   version,
					   sizeof version);
			firmware_parameter(
				p, next, "machine:", machine, sizeof machine);
			firmware_payload(version, machine, data, sizeof data);
			if ((sb_lms_tag(&command, "subscribe") && !strcmp(sb_lms_tag(&command, "subscribe"), "0")))
				firmware_subscribe(
					reply, version, machine, data);
		}

		else if (!strcmp(command.arguments[0], "date"))
			snprintf(data,
				 sizeof data,
				 "{\"date_epoch\":%lu,\"date\":\"0000-00-00T00:"
				 "00:00+00:00\"}",
				 (unsigned long)time(NULL));

		else if (!strcmp(command.arguments[0], "jiveapplets")) {
			char target[64], firmware[64];
			firmware_parameter(p, next, "target:", target, sizeof target);
			firmware_parameter(p,next,"version:",firmware,sizeof firmware);
            catalog_response(target, firmware, data, sizeof data);
		}

		/* SlimMenus requires LMS' notification-array shape before it
		 * considers the server menu loaded. Alarm Clock is
		 * server-owned; the stock device applet supplies its
		 * wake-up/popup behavior. */
		else if (!strcmp(command.arguments[0], "menustatus"))
			sb_alarms_menu(data,
				       sizeof data,
				       player_id[0] ? player_id : "all",
				       1);

		else if (!strcmp(command.arguments[0], "menu"))
			sb_alarms_menu(data, sizeof data, player_id, 0);

		else if (!strcmp(command.arguments[0], "status")) {
			player_status_json(data, sizeof data);
			register_playerstatus(reply);
			if (test_nowplaying &&
			    strstr(reply, "/slim/playerstatus/"))
				fprintf(stderr,
					"NOWPLAYING STATUS requested "
					"player=%s\n",
					player_id[0] ? player_id : "unknown");
		}

		else if (!strcmp(command.arguments[0], "power")) {
			const char *seq=sb_lms_tag(&command,"seq_no");
			uint32_t sequence=seq?(uint32_t)strtoull(seq,NULL,10):0;
			if(command.count>1&&strcmp(command.arguments[1],"?")){
				int on=!strcmp(command.arguments[1],"1");
				if(np_set_player_power(nowplaying,on,sequence)){
					if(!on)(void)np_queue_command(nowplaying,"stop",0);
					publish_playerstatus();publish_player();
				}
			}
			snprintf(data,sizeof data,"{\"_power\":%d}",np_player_power(nowplaying));
		}
		else if (!strcmp(command.arguments[0], "mixer")) {
			const char *seq=sb_lms_tag(&command,"seq_no");
			if(command.count>2&&strcmp(command.arguments[2],"?")){
				(void)np_set_player_volume(nowplaying,atoi(command.arguments[2]),seq?(uint32_t)strtoull(seq,NULL,10):0);
				publish_playerstatus();
			}
			snprintf(data,sizeof data,"{\"_volume\":%d}",np_player_volume(nowplaying));
		}
		else if (!strcmp(command.arguments[0], "pause")) {
			np_queue_command(nowplaying,
					 (command.count > 1 && !strcmp(command.arguments[1], "0")) ? "play"
								  : "pause",
					 0);
			strcpy(data,
			       "{\"count\":0,\"offset\":0,\"item_loop\":[]}");
		}

		else if (!strcmp(command.arguments[0], "play")) {
			np_queue_command(nowplaying, "play", 0);
			strcpy(data,
			       "{\"count\":0,\"offset\":0,\"item_loop\":[]}");
		}

		else if (!strcmp(command.arguments[0], "stop")) {
			np_queue_command(nowplaying, "stop", 0);
			strcpy(data,
			       "{\"count\":0,\"offset\":0,\"item_loop\":[]}");
		}

		else if (!strcmp(command.arguments[0], "playlist") &&
			 !strcmp(command.arguments[1], "index")) {
			np_queue_command(nowplaying,
					 !strcmp(command.arguments[2], "-1") ? "previous"
								   : "next",
					 0);
			strcpy(data,
			       "{\"count\":0,\"offset\":0,\"item_loop\":[]}");
		}

		else
			strcpy(data,
			       "{\"count\":0,\"offset\":0,\"item_loop\":[]}");

		if (strstr(data, "PLAYER")) {
			char player[900], *mark = strstr(data, "PLAYER");
			/* This status bootstraps SqueezePlay's server switch.
			 * Reporting zero until the later SlimProto HELO makes
			 * the client detach before it can establish that
			 * connection, resulting in a reconnect loop. */
			snprintf(
				player,
				sizeof player,
				"[{\"playerindex\":\"0\",\"playerid\":\"%s\","
				"\"name\":\"Standalone\",\"model\":\"%s\","
				"\"modelname\":\"%s\",\"isplayer\":1,"
				"\"connected\":1,"
				"\"power\":%d,\"firmware\":\"7.7.3\",\"ip\":"
				"\"127.0.0.1\",\"seq_no\":%u,\"displaytype\":"
				"\"none\",\"isplaying\":%d,\"canpoweroff\":1}]",
				player_id,
				player_model,
				player_modelname,
				np_player_power(nowplaying),np_player_sequence(nowplaying),
				np_current(nowplaying)->state == NP_PLAYING);
			memmove(mark + strlen(player),
				mark + 6,
				strlen(mark + 6) + 1);
			memcpy(mark, player, strlen(player));
		}

		snprintf(msg,
			 sizeof msg,
			 "{%s\"channel\":\"/slim/"
			 "request\",\"successful\":true}",
			 id_field);
		add_json(out, cap, &used, msg);
		snprintf(msg,
			 sizeof msg,
			 "{%s\"channel\":\"%s\",\"data\":%s}",
			 id_field,
			 reply,
			 data);
		add_json(out, cap, &used, msg);
		p = next;
	}
	if (used + 2 < cap) {
		out[used++] = ']';
		out[used] = 0;
	}
}

#ifdef SB_MUSIC_BROWSE_INTEGRATION
#include "music_browse_http.inc"
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
#include "music_playback_glue.inc"
#ifndef SB_MUSIC_RUNTIME
#include "music_playback_test.inc"
#else
#include "music_runtime.inc"
#endif
#endif
#endif

/**
 * @brief Handle http.
 * @param c Client connection state.
 * @param cfg Object referenced by cfg.
 */
#include "ownership_http.inc"

/* Bounded reject diagnostic: Jive replays its whole backlog after any
 * error, so a silent 400 turns one poison batch into an undiagnosable
 * reconnect storm. Log length, whether the body is valid JSON under a
 * generous budget, and a sanitized head. */
static void comet_malformed_log(const client *c,
				const char *body,
				size_t need,
				int big_valid)
{
	static char dump[16385];
	size_t n = need < 16384 ? need : 16384, i;
	const char *s = (const char *)c->buf;
	size_t off = (size_t)(body - s);
	for (i = 0; i < n; i++) {
		unsigned char ch = (unsigned char)body[i];
		dump[i] = (ch == '\r') ? 'R' : (ch == '\n') ? 'N' :
			  (ch >= 32 && ch < 127) ? (char)ch : '?';
	}
	dump[n] = 0;
	fprintf(stderr,
		"COMET MALFORMED len=%lu used=%lu off=%lu big=%d body=%s\n",
		(unsigned long)need,
		(unsigned long)c->used,
		(unsigned long)off,
		big_valid,
		dump);
}

#include "upnp_http.inc"
static void handle_http_once(client *c,
				 const sb_config *cfg,
				 char *body,
				 long need,
				 int chunked);

static void handle_http(client *c, const sb_config *cfg)
{
	/* Consume-then-shift: Jive pipelines back-to-back POSTs on its
	 * reusable sockets, and loopback coalesces them into one recv.
	 * Discarding the tail (used=0) desyncs the socket permanently:
	 * the next recv overwrites buf+0 and every later request
	 * validates a fragment (COMET MALFORMED storm). Parse one
	 * request, dispatch it, shift the leftover, and re-enter while
	 * a complete request is buffered. Bounded per call; any rest
	 * waits for the next recv. Async music, streams, chunked
	 * uploads and draining sockets keep single-shot behavior. */
	for (int round = 0; round < 64; round++) {
		char *s = (char *)c->buf, *end = strstr(s, "\r\n\r\n"), *body,
		     *cl, *size_end, *size_parse;
		long need = 0;
		int chunked;
		size_t total, consumed;
		char save = 0;
		int guarded = 0;

		if (!end)
			return;

		char header_tail=*end;
		*end=0; /* Header searches must not inspect a pipelined request/body. */
		cl = strstr(s, "Content-Length:");
		if (!cl)
			cl = strstr(s, "content-length:");
		if (cl)
			need = strtol(cl + 15, NULL, 10);

		chunked = !cl && (strstr(s, "Transfer-Encoding: chunked") ||
				  strstr(s, "transfer-encoding: chunked"));
		*end=header_tail;
		body = end + 4;

		if (chunked) {
			size_end = strstr(body, "\r\n");
			if (!size_end)
				return;
			errno = 0;
			need = strtol(body, &size_parse, 16);
			if (errno || size_parse != size_end)
				need = -1;
			if (need >= 0 &&
			    (size_t)(size_end + 2 - s) + (size_t)need + 2 >
				    c->used)
				return;
			if (need >= 0) {
				memmove(body, size_end + 2, (size_t)need);
				body[need] = 0;
			}
		}

		if (need < 0 || need > SB_MAX_HTTP_BODY) {
			http_reply(c,
				   "413 Payload Too Large",
				   "[{\"successful\":false}]",
				   0);
			return;
		}

		if (!chunked && (size_t)(body - s) + (size_t)need > c->used)
			return;

		total = c->used;
		/* NUL-guard the slice: dispatch uses unbounded strstr and
		 * must not see pipelined bytes. Restored below. */
		if (!chunked &&
		    (size_t)(body - s) + (size_t)need < total) {
			save = body[need];
			body[need] = 0;
			guarded = 1;
		}
		if(test_nowplaying) {
			char bh[25];
			int m = need < 24 ? (need < 0 ? 0 : (int)need) : 24, q;
			for (q = 0; q < m; q++) {
				unsigned char ch = (unsigned char)body[q];
				bh[q] = (ch >= 32 && ch < 127) ? (char)ch : '?';
			}
			bh[m] = 0;
			fprintf(stderr,
				"COMET ROUND fd=%d off=%lu need=%ld "
				"used=%lu bodyhead=%s\n",
				c->fd,
				(unsigned long)(body - s),
				need,
				(unsigned long)total,
				bh);
		}
		handle_http_once(c, cfg, body, need, chunked);
		if (guarded)
			body[need] = save;
		if (chunked)
			return;
#ifdef SB_MUSIC_BROWSE_INTEGRATION
		if (c->music_http)
			return;
#endif
		if (c->fd < 0 || c->close_after_drain || c->kind != 2)
			return;
		if (c->http_resume) {
			/* A synchronously completed music request
			 * already shifted the tail; re-parse it. */
			c->http_resume = 0;
			continue;
		}
		consumed = (size_t)(body - s) + (size_t)need;
		if(test_nowplaying) fprintf(stderr,
			"COMET SHIFT fd=%d consumed=%lu total=%lu left=%lu\n",
			c->fd,
			(unsigned long)consumed,
			(unsigned long)total,
			(unsigned long)(consumed >= total ?
					0 : total - consumed));
		if (consumed >= total) {
			c->used = 0;
			return;
		}
		memmove(c->buf, c->buf + consumed, total - consumed);
		c->used = total - consumed;
		c->buf[c->used] = 0;
	}
	/* Yield after a bounded turn, but drain buffered requests on the next
	 * event-loop turn even if the peer has no more bytes to send. */
	if(c->used)c->http_resume=1;
}

static void handle_http_once(client *c,
			     const sb_config *cfg,
			     char *body,
			     long need,
			     int chunked)
{
	char *s = (char *)c->buf, *end = strstr(s, "\r\n\r\n"), *channel, *id;
	char response[24000], cid[32], reply[160] = "/response", mac[32],
				       rid[32] = "1";
	int i, keep_response = 0;
	char method[8], path[512];

	method[0] = path[0] = 0;
	if (sscanf(s, "%7s %511s", method, path) != 2) {
		http_reply(c,
			   "400 Bad Request",
			   "{\"success\":false,\"error\":\"invalid_request\"}",
			   0);
		return;
	}
	if(!strcmp(path,"/api/network-music")){upnp_http(c,method,s,(size_t)(end-s)+2,body,(size_t)need,chunked);return;}

if(!strcmp(path,"/api/music/ownership")){
  ownership_http(c,method,s,(size_t)(end-s)+2,body,(size_t)need,chunked);
  return;
 }
 if(!strcmp(path,"/api/tinysc/lifecycle")){
  struct sockaddr_in peer;socklen_t size=sizeof peer;char json[2048];
  if(strcmp(method,"GET")||getpeername(c->fd,(struct sockaddr *)&peer,&size)||
     peer.sin_family!=AF_INET||ntohl(peer.sin_addr.s_addr)!=INADDR_LOOPBACK){
   http_reply(c,"404 Not Found","{}",0);return;
  }
  if(!sb_ts_json(tinysc_lifecycle,json,sizeof json))strcpy(json,"{}");
  http_reply(c,"200 OK",json,0);return;
 }
#if defined(SB_MUSIC_PLAYBACK_INTEGRATION) && !defined(SB_MUSIC_RUNTIME)
	if (local_music && !strcmp(method, "GET") &&
	    playback_test_route(c, path))
		return;
#endif
	if (!strncmp(path, "/api/nowplaying", 15)) {
		int was_active = np_current(nowplaying)->active;
		int was_playing = np_current(nowplaying)->state == NP_PLAYING;
		int changed = 0, status = np_api(nowplaying,
						 method,
						 path,
						 body,
						 (size_t)need,
						 response,
						 sizeof response,
						 &changed);
		const char *phrase = status == 200   ? "200 OK"
				     : status == 201 ? "201 Created"
				     : status == 400 ? "400 Bad Request"
				     : status == 405 ? "405 Method Not Allowed"
				     : status == 409 ? "409 Conflict"
				     : status == 413 ? "413 Payload Too Large"
				     : status == 429 ? "429 Too Many Requests"
				     : status == 503 ? "503 Service Unavailable"
						     : "404 Not Found";
		if (changed) {
			/* Server status controls attachment, not playback mode.
			 * A station switch briefly buffers; re-announcing the
			 * player then races and resets the track update that
			 * follows. */
			int is_active = np_current(nowplaying)->active;
			(void)was_playing;
			if (was_active != is_active)
				publish_player();
			publish_playerstatus();
		}
		if (test_nowplaying && !strcmp(path, "/api/nowplaying/update"))
			fprintf(stderr,
				"NOWPLAYING UPDATE status=%d flags=%d "
				"generation=%llu revision=%llu id=%s "
				"station=%s artwork_changed=%d "
				"artwork_len=%lu\n",
				status,
				changed,
				(unsigned long long)np_current(nowplaying)
					->generation,
				(unsigned long long)np_current(nowplaying)
					->playlist_revision,
				np_current(nowplaying)->track.id,
				np_current(nowplaying)->track.station,
				!!(changed & NP_CHANGE_ARTWORK),
				(unsigned long)strlen(
					np_current(nowplaying)->track.artwork));
		http_reply(c, phrase, response, 0);
		return;
	}

#if defined(SB_MUSIC_BROWSE_INTEGRATION) && !defined(SB_MUSIC_RUNTIME)
	if (music_browse && !strcmp(method, "GET") &&
	    !strncmp(path, "/test/music/", 12)) {
		int ok = 1;
		if (!strcmp(path, "/test/music/metrics")) {
			char metrics[256];
			snprintf(metrics,
				 sizeof metrics,
				 "{\"async_peak\":%u,\"http_peak\":%u,\"http_"
				 "state_bytes\":%zu,\"wp1_result_bytes\":%zu}",
				 sb_browse_pending_peak(music_browse),
				 music_pending_peak,
				 sizeof(struct music_http),
				 sizeof(sb_ml_result));
			http_reply(c, "200 OK", metrics, 0);
			return;
		}
		if (!strcmp(path, "/test/music/offline")) {
			music_media_snapshot.online = 0;
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
			if (local_music)
				sb_playback_media_removed(
					local_music, &music_media_snapshot);
#endif
			ok = sb_browse_media(
				music_browse, &music_media_snapshot, 0);
		} else if (!strcmp(path, "/test/music/online")) {
			if (music_media_snapshot.generation >= INT64_MAX)
				ok = 0;
			else {
				music_media_snapshot.generation++;
				music_media_snapshot.online = 1;
				ok = sb_browse_media(
					music_browse, &music_media_snapshot, 0);
			}
		} else if (!strcmp(path, "/test/music/scan/start"))
			ok = sb_browse_media(
				music_browse, &music_media_snapshot, 1);
		else if (!strcmp(path, "/test/music/scan/end"))
			ok = sb_browse_media(
				music_browse, &music_media_snapshot, 0);
		else
			ok = 0;
		http_reply(c,
			   ok ? "200 OK" : "400 Bad Request",
			   ok ? "{\"success\":true}" : "{\"success\":false}",
			   0);
		return;
	}
#endif
	if (strncmp(s, "POST /cometd ", 13) && strncmp(s, "GET /health ", 12)
	    && strncmp(s, "GET /api/debug/comet ", 19)
#ifndef SB_MUSIC_RUNTIME
        && strncmp(s, "POST /test/next-track ", 22)
#endif
       ) {
		http_reply(c,
			   "404 Not Found",
			   "[{\"successful\":false,\"error\":\"not found\"}]",
			   0);
		return;
	}

	if (!strncmp(s, "GET /health ", 12)) {
		http_reply(c,
			   "200 OK",
			   "{\"status\":\"ok\",\"service\":\"sbbase\","
			   "\"version\":\"" SBBASE_VERSION
			   "\",\"lms_version\":\"" SB_LMS_COMPAT_VERSION "\"}",
			   0);
		return;
	}
	/* Loopback-only listener: safe to expose stream/subscription state
	 * for Now Playing diagnosis (WP4a.6 Dreams: is Jive subscribed?). */
	if (!strncmp(s, "GET /api/debug/comet ", 19)) {
		char dbg[4096], *w = dbg, *end = dbg + sizeof dbg;
		int i, first = 1;
		w += snprintf(w,
			      (size_t)(end - w),
			      "{\"player_id\":\"%s\",\"subscribed_cids\":[",
			      player_id);
		for (i = 0; i < SB_SUBSCRIBED_CIDS && w < end - 48; i++) {
			if (!subscribed_cids[i][0])
				continue;
			w += snprintf(w,
				      (size_t)(end - w),
				      "%s\"%s\"",
				      first ? "" : ",",
				      subscribed_cids[i]);
			first = 0;
		}
		first = 1;
		if (w < end - 16)
			w += snprintf(w, (size_t)(end - w), "],\"streams\":[");
		for (i = 0; i < CLIENTS && w < end - 128; i++) {
			client *sc = &clients[i];
			if (sc->fd < 0 || sc->kind != 3 ||
			    !sc->client_id[0])
				continue;
			w += snprintf(w,
				      (size_t)(end - w),
				      "%s{\"cid\":\"%s\"}",
				      first ? "" : ",",
				      sc->client_id);
			first = 0;
		}
		if (w < end - 8)
			w += snprintf(w, (size_t)(end - w), "]}");
		http_reply(c, "200 OK", dbg, 0);
		return;
	}

#ifndef SB_MUSIC_RUNTIME
	if (!strncmp(s, "POST /test/next-track ", 22)) {
		if (!test_nowplaying) {
			http_reply(c,
				   "404 Not Found",
				   "{\"error\":\"not found\"}",
				   0);
			return;
		}
		np_demo_next(nowplaying);
		fprintf(stderr, "NOWPLAYING TRACK changed\n");
		publish_playerstatus();
		http_reply(c, "200 OK", "{\"status\":\"ok\"}", 0);
		return;
	}

#endif

	/* Comet owns a reusable request socket (rhttp). Sinks can enqueue
	 * another request while processing this response, so closing the socket
	 * here races that re-entrant fetch and loses menu/playerstatus
	 * subscriptions. */
	keep_response = 1;
	if (!sb_lms_json_valid(body, (size_t)need)) {
#ifdef SB_MUSIC_BROWSE_INTEGRATION
		/* Drain over-budget backlogs and resync truncated heads
		 * instead of 400-looping; aligned-but-corrupt bodies
		 * keep the strict 400 (see music_prefix_drain). */
		int big_valid =
			sb_lms_json_valid_budget(body, (size_t)need, 65536);
		if (music_prefix_drain(c,
				       body,
				       (size_t)need,
				       big_valid))
			return;
		comet_malformed_log(c, body, (size_t)need, big_valid);
#else
		comet_malformed_log(c, body, (size_t)need, 0);
#endif
		http_reply(c, "400 Bad Request", "[{\"successful\":false,\"error\":\"malformed_json\"}]", 0);
		return;
	}
#ifdef SB_MUSIC_BROWSE_INTEGRATION
	if (music_http_begin(c, body, (size_t)need))
		return;
#endif
	json_unescape_slashes(body);
	channel = strstr(body, "\"channel\"");
	id = strstr(body, "\"id\"");
	(void)id;

	if (!channel) {
		http_reply(c,
			   "400 Bad Request",
			   "[{\"successful\":false,\"error\":\"invalid Bayeux "
			   "payload\"}]",
			   0);
		return;
	}

	cid[0] = 0;
	json_string(body, "\"response\"", reply, sizeof reply);
	json_string(body, "\"clientId\"", cid, sizeof cid);
	json_raw(body, "\"id\"", rid, sizeof rid);

	if (strstr(channel, "/meta/handshake")) {
		token(cid);
		/* WP4a.6: first handshake wins. A later probe with a
		 * different MAC must not hijack the established player
		 * identity (and its playback context) out from under
		 * Jive; same-MAC re-handshakes stay harmless. */
		if (json_string(body, "\"mac\"", mac, sizeof mac) &&
		    valid_player_id(mac) &&
		    (!player_id[0] || !strcmp(mac, player_id)))
			strcpy(player_id, mac);
		snprintf(response,
			 sizeof response,
			 "[{\"id\":\"1\",\"channel\":\"/meta/"
			 "handshake\",\"version\":\"1.0\","
			 "\"supportedConnectionTypes\":[\"long-polling\","
			 "\"streaming\"],\"clientId\":\"%s\",\"successful\":"
			 "true,\"advice\":{\"reconnect\":\"retry\","
			 "\"interval\":0,\"timeout\":60000}}]",
			 cid);
		/* SqueezePlay queues /meta/connect on this same chttp socket as
		 * soon as the handshake sink fires, so this one response must
		 * stay reusable. */
		keep_response = 1;
	} else if (strstr(channel, "/meta/connect") ||
		   strstr(channel, "/meta/reconnect")) {
		int reconnecting = strstr(channel, "/meta/reconnect") != NULL;
		if (!cid[0]) {
			http_reply(c,
				   "400 Bad Request",
				   "[{\"successful\":false,\"error\":\"missing "
				   "clientId\"}]",
				   0);
			return;
		}
		/* A Bayeux client has one event stream. Replace a stale
		 * duplicate so it cannot consume a client slot indefinitely. */
		/* After a Base restart the old client ID has no subscriptions.
		 * Accepting its reconnect would strand stock Jive with cached
		 * menus: only a handshake makes Comet replay its subscriptions.
		 * A live replacement stream still inherits its subscriptions. */
		if (reconnecting && !stream_for_id(cid)) {
			char escaped[192];
			np_json_escape(escaped, sizeof escaped, cid);
			snprintf(response, sizeof response,
				 "[{\"channel\":\"/meta/reconnect\","
				 "\"clientId\":\"%s\",\"successful\":false,"
				 "\"error\":\"402::Unknown client\","
				 "\"advice\":{\"reconnect\":\"handshake\","
				 "\"interval\":0}}]", escaped);
			http_reply(c, "200 OK", response, 0);
			return;
		}
		for (i = 0; i < CLIENTS; i++)
			if (&clients[i] != c && clients[i].fd >= 0 &&
			    clients[i].kind == 3 &&
			    !strcmp(clients[i].client_id, cid)) {
				fprintf(stderr,
					"COMET REPLACE client=%s oldfd=%d "
					"newfd=%d\n",
					cid,
					clients[i].fd,
					c->fd);
				c->playerstatus_subscribed =
					clients[i].playerstatus_subscribed;
				snprintf(c->subscribed_player,
					 sizeof c->subscribed_player,
					 "%s",
					 clients[i].subscribed_player);
#ifdef SB_MUSIC_BROWSE_INTEGRATION
				music_inherit(c, &clients[i]);
#endif
				close_client(&clients[i]);
			}
		/* Bayeux requires the subscribe ack to echo the requested
		 * subscription; without it SqueezePlay never completes
		 * its subscribe and reconnects forever. */
		char sub[192], subesc[384];
		sub[0] = 0;
		subesc[0] = 0;
		if (json_string(body, "\"subscription\"", sub, sizeof sub))
			np_json_escape(subesc, sizeof subesc, sub);
		snprintf(c->client_id, sizeof c->client_id, "%s", cid);
		snprintf(
			response,
			sizeof response,
			"[{\"channel\":\"/meta/"
			"%s\",\"clientId\":\"%s\",\"successful\":true,"
			"\"advice\":{\"reconnect\":\"retry\",\"interval\":0,"
			"\"timeout\":60000}},"
			"{\"channel\":\"/meta/"
			"subscribe\",\"clientId\":\"%s\"%s%s%s,\"successful\":"
			"true}]",
			reconnecting ? "reconnect" : "connect",
			cid,
			cid,
			subesc[0] ? ",\"subscription\":\"" : "",
			subesc,
			subesc[0] ? "\"" : "");
		fprintf(stderr,
			"COMET RX %.*s\nCOMET STREAM client=%s\n",
			(int)need,
			body,
			cid);
		stream_start(c, response);
		/* Subscriptions live in the global CID set (Jive's request
		 * CID differs from its stream CID), so every reconnect
		 * replays the current snapshot whenever anyone is
		 * subscribed. Updates that raced the reconnect cannot
		 * leave SqueezePlay displaying stale metadata. */
		if (c->fd >= 0 && subscribed_cid_any())
			publish_playerstatus();
		firmware_publish(cid);
		return;
	} else if (strstr(channel, "/meta/disconnect")) {
		for (unsigned n = 0; n < CLIENTS; n++)
			if (!strcmp(firmware_subscriptions[n].client_id, cid))
				memset(&firmware_subscriptions[n],
				       0,
				       sizeof firmware_subscriptions[n]);
		keep_response = 0;
		snprintf(response,
			 sizeof response,
			 "[{\"id\":\"1\",\"channel\":\"/meta/"
			 "disconnect\",\"successful\":true}]");
		for (i = 0; i < CLIENTS; i++)
			if (&clients[i] != c && clients[i].fd >= 0 &&
			    clients[i].kind == 3 && cid[0] &&
			    !strcmp(clients[i].client_id, cid))
				close_client(&clients[i]);
	} else {
		unregister_playerstatus(body);
		request_responses(body, cfg, response, sizeof response);
	}

	fprintf(stderr,
		"COMET RX %.*s\nCOMET TX %s\n",
		(int)need,
		body,
		response);

	/* Content-Length delimits each response while rhttp remains reusable.
	 */
	http_reply(c, "200 OK", response, keep_response);
	if (c->fd >= 0)
		c->used = 0;
}

/**
 * @brief Handle keepalive.
 * @param c Client connection state.
 */
static void keepalive(client *c)
{
	unsigned char payload[24] = {'t', '0', 'm', '?', '?', '?', '?', 0,
				     0,	  0,   '0', 0,	 0,   0,   0,	0,
				     0,	  0,   0,   0,	 0,   0,   0,	0},
		      frame[40];
	size_t z = sb_server_frame(
		"strm", payload, sizeof payload, frame, sizeof frame);
	if (z)
		send_data(c, (const char *)frame, z);
	c->keepalive = time(NULL);
	c->slim_poll = c->keepalive;
}

/**
 * @brief Handle slim.
 * @param c Client connection state.
 */
static void handle_slim(client *c)
{
	size_t off = 0, used;
	char op[5];
	const unsigned char *p;
	uint32_t n;
	fprintf(stderr,
		"SLIM RX fd=%d bytes=%lu\n",
		c->fd,
		(unsigned long)c->used);
	while (off < c->used) {
		int r = sb_parse_client_frame(
			c->buf + off, c->used - off, op, &p, &n, &used);
		if (r < 0) {
			fprintf(stderr, "SLIM INVALID fd=%d\n", c->fd);
			close_client(c);
			return;
		}
		if (!r) {
			fprintf(stderr,
				"SLIM PARTIAL fd=%d remaining=%lu\n",
				c->fd,
				(unsigned long)(c->used - off));
			break;
		}
		fprintf(stderr,
			"SLIM FRAME fd=%d op=%s len=%lu\n",
			c->fd,
			op,
			(unsigned long)n);
		if (!strcmp(op, "HELO")) {
			sb_slim_event event;
			if (c->helo || sb_slim_parse(op, p, n, &event) != 1) {
				close_client(c);
				return;
			}
			const unsigned char *mac = event.hello.mac;
			snprintf(player_id,
				 sizeof player_id,
				 "%02x:%02x:%02x:%02x:%02x:%02x",
				 mac[0],
				 mac[1],
				 mac[2],
				 mac[3],
				 mac[4],
				 mac[5]);
			player_model = !strcmp(event.hello.model, "fab4")
					       ? "fab4"
				       : !strcmp(event.hello.model, "baby")
					       ? "baby"
					       : "unknown";
			player_modelname = !strcmp(player_model, "fab4")
						   ? "Squeezebox Touch"
					   : !strcmp(player_model, "baby")
						   ? "Squeezebox Radio"
						   : "SqueezePlay";
			player_connected = 1;
			strcpy(c->slim_player, player_id);
			c->helo = 1;
#ifndef SB_MUSIC_RUNTIME
   if(ownership_bridge.enabled){
    if(ownership_connection_sequence==UINT64_MAX){close_client(c);return;}
    c->ownership_connection=++ownership_connection_sequence;
    sb_ob_connection(&ownership_bridge,c->ownership_connection,1);
   }
#endif
#ifdef SB_MUSIC_BROWSE_INTEGRATION
			sb_browse_player(music_browse, player_id);
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
			if (local_music) {
				c->music_connection = ++local_connection;
				if (!c->music_connection ||
				    !sb_playback_attach(local_music,
							c->music_connection,
							&event.hello)) {
					close_client(c);
					return;
				}
#ifdef SB_MUSIC_RUNTIME
                c->ownership_connection=c->music_connection;
                sb_ob_connection(&ownership_bridge,c->ownership_connection,1);
#endif
				sb_browse_playback(music_browse,

#ifdef SB_MUSIC_RUNTIME
                           music_idle_only ? 0 :
#endif
                           event.hello.codecs & 3u);
			}
#endif
#endif
			keepalive(c);
			if (c->fd < 0)
				return;
			publish_player();
		}
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
		if (local_music && c->helo && strcmp(op, "HELO")) {
			sb_slim_event event;
			if (sb_slim_parse(op, p, n, &event) == 1)
				sb_playback_event(
					local_music,
					c->music_connection,
					sb_playback_stream(local_music),
					&event,
					np_monotonic_ms());
		}
#endif
		off += used;
	}
	if (off) {
		memmove(c->buf, c->buf + off, c->used - off);
		c->used -= off;
	}
}

/**
 * @brief Handle defaults.
 * @param c Object referenced by c.
 */
static void defaults(sb_config *c)
{
	memset(c, 0, sizeof *c);
	strcpy(c->name, "StandaloneBase");
	strcpy(c->uuid, "9d989f40-499a-4b85-b92f-8dc415af2a04");
	strcpy(c->lms_version, SB_LMS_COMPAT_VERSION);
	strcpy(c->advertise_ip, "127.0.0.1");
	c->http_port = 9000;
	c->slim_port = c->discovery_port = 3483;
	c->time_sync = 1;
}

/**
 * @brief Run the program entry point.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
	sb_config cfg;
 const char *ownership_token_path=NULL;
 int arbitration_enabled=0;
	int udp, tcp, http, i;
	client cs[CLIENTS];
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
	struct pollfd pf[7 + 2*CLIENTS];
#ifndef SB_MUSIC_RUNTIME
	const char *playback_root = NULL;
	int playback_network_fixture = 0;
	int playback_ipc = -1;
	unsigned playback_http_port = 0;
#endif
#else
	struct pollfd pf[5 + 2*CLIENTS];
#endif
	int poll_count = 3 + CLIENTS;
#ifdef SB_MUSIC_BROWSE_INTEGRATION
#ifndef SB_MUSIC_RUNTIME
	const char *music_fixture = NULL;
	int test_storage_supported = 1;
#endif
	poll_count++;
#endif
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
	poll_count += 2;
#endif
	defaults(&cfg);
	active_config = &cfg;
	clients = cs;
	nowplaying = np_create(120, 32);
	if (!nowplaying) {
		fputs("nowplaying: out of memory\n", stderr);
		return 1;
	}

	if (argc == 2 && !strcmp(argv[1], "--version")) {
		printf("sbbase %s (LMS compatibility %s)\n",
		       SBBASE_VERSION,
		       cfg.lms_version);
		return 0;
	}

	if (argc == 2 && !strcmp(argv[1], "--self-test")) {
		unsigned char q[] = {'e', 'N', 'A', 'M', 'E', 0}, o[96];
		return sb_discovery_response(
			       q, sizeof q, o, sizeof o, &cfg, "127.0.0.1")
			       ? 0
			       : 2;
	}

	if (argc == 3 && !strcmp(argv[1], "--check-config")) {
		if (load_catalog(argv[2]) < 0)
			return 2;
		puts(catalog_data);
		return 0;
	}

	for (i = 1; i < argc; i++) {
#ifdef SB_MUSIC_RUNTIME
        int parsed=music_runtime_argument(argc,argv,&i);
        if(parsed<0)return 2;
        if(parsed)continue;
#endif
  if(!strcmp(argv[i],"--ownership-token")&&i+1<argc){ownership_token_path=argv[++i];continue;}
  if(!strcmp(argv[i],"--tinysc-arbitration")){arbitration_enabled=1;continue;}
#if defined(SB_MUSIC_PLAYBACK_INTEGRATION) && !defined(SB_MUSIC_RUNTIME)
		if (!strcmp(argv[i], "--test-network-playback")) { playback_network_fixture=1; continue; }
		if (!strcmp(argv[i], "--test-music-root") && i + 1 < argc) {
			playback_root = argv[++i];
			continue;
		}
		if (!strcmp(argv[i], "--test-music-mock-adapters")) {
			local_mock_adapters = 1;
			continue;
		}
		if ((!strcmp(argv[i], "--test-playback-ipc") ||
		     !strcmp(argv[i], "--test-playback-http-port")) &&
		    i + 1 < argc) {
			int is_fd = !strcmp(argv[i], "--test-playback-ipc");
			char *end;
			errno = 0;
			long value = strtol(argv[++i], &end, 10);
			if (errno || *end || value < (is_fd ? 3 : 1) ||
			    value > (is_fd ? INT32_MAX : 65535))
				return 2;
			if (is_fd)
				playback_ipc = (int)value;
			else
				playback_http_port = (unsigned)value;
			continue;
		}
#endif
#if defined(SB_MUSIC_BROWSE_INTEGRATION) && !defined(SB_MUSIC_RUNTIME)
		if (!strcmp(argv[i], "--test-music-db") && i + 1 < argc) {
			music_fixture = argv[++i];
			continue;
		}
		if (!strcmp(argv[i], "--test-music-no-storage")) {
			test_storage_supported = 0;
			continue;
		}
#endif
		if (!strcmp(argv[i], "--config") && i + 1 < argc) {
			const char *path = argv[++i];
			snprintf(cfg.catalog_path,
				 sizeof cfg.catalog_path,
				 "%s",
				 path);
			if (load_catalog(path) < 0) {
				fprintf(stderr,
					"invalid or unreadable config: %s\n",
					path);
				return 2;
			}
		}
#ifndef SB_MUSIC_RUNTIME
        else if (!strcmp(argv[i], "--test-nowplaying"))
			test_nowplaying = 1;
#endif
		else {
			fprintf(stderr,
				"usage: %s [--config FILE] "
				"[--test-nowplaying] [--ownership-token FILE] [--tinysc-arbitration]\n",
				argv[0]);
			return 2;
		}
	}

#ifdef SB_MUSIC_RUNTIME
 if(music_runtime.check_only){
  if(!music_browse_only||ownership_token_path||music_idle_only||music_runtime.scan||music_runtime.scan_only||
     music_runtime.initialize||music_runtime.resource_fd>=0||music_runtime.port){np_destroy(nowplaying);return 2;}
  int ok=runtime_open_check();music_runtime_stop();np_destroy(nowplaying);return ok?0:2;
 }
#endif
 /* Persistent opt-in also covers legacy applet supervisors after a Base-only
  * restart. Presence requests arbitration; the worker validates ownership and
  * permissions. An unreadable policy must not silently disable arbitration. */
 {
  struct stat policy;
  if(!lstat("/mnt/storage/standalonebase/tinysc-arbitration.enabled",&policy)||errno!=ENOENT)
   arbitration_enabled=1;
 }
 #ifdef SB_MUSIC_RUNTIME
 if(music_browse_only && (ownership_token_path || music_runtime.scan_only || music_idle_only)){
  fputs("music: browse-only excludes ownership and scan-only modes\n",stderr);return 2;
 }
 #endif
#ifdef SB_MUSIC_RUNTIME
 if(music_idle_only && (!ownership_token_path || music_runtime.scan_only)){
  fputs("music: idle qualification requires production ownership\n",stderr);return 2;
 }
#endif
 if(ownership_token_path){
#if defined(SB_MUSIC_PLAYBACK_INTEGRATION) && !defined(SB_MUSIC_RUNTIME)
  if(playback_root){fputs("production ownership and fixture runtime are exclusive\n",stderr);return 2;}
#endif
  sb_owner_init(&production_owner,1);
  if(!sb_ob_open(&ownership_bridge,&production_owner,ownership_token_path)){
   fputs("ownership: private provisioning failed\n",stderr);return 2;
  }
 }
	signal(SIGINT, stop);
	char alarm_path[512] = "";
	if (cfg.catalog_path[0]) {
		const char *slash = strrchr(cfg.catalog_path, '/');
		if (slash)
			snprintf(alarm_path,
				 sizeof alarm_path,
				 "%.*s/alarms.conf",
				 (int)(slash - cfg.catalog_path),
				 cfg.catalog_path);
		else
			snprintf(alarm_path, sizeof alarm_path, "alarms.conf");
	}
	alarms = sb_alarms_open(alarm_path);
	if (!alarms) {
		fputs("alarms: out of memory\n", stderr);
		return 1;
	}
	signal(SIGTERM, stop);
	signal(SIGHUP, reload);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGSEGV, fatal);
	signal(SIGABRT, fatal);
	signal(SIGBUS, fatal);
	signal(SIGILL, fatal);
	signal(SIGFPE, fatal);
	atexit(death_notice);

 if(arbitration_enabled){
  sb_ts_options options={.enabled=1,.poll_ms=500,.stop_timeout_ms=15000,
   .run_dir="/var/run/standalonebase",
   .policy_path="/mnt/storage/standalonebase/tinysc-arbitration.enabled",
   .original_service="/etc/init.d/squeezecenter.stock",
   .control_path="/usr/share/jive/applets/StandaloneBase/sb-tinysc-control"};
  tinysc_lifecycle=sb_ts_start(&options);
  if(!tinysc_lifecycle){fputs("TinySC arbitration initialization refused\n",stderr);return 2;}
  uint64_t until=np_monotonic_ms()+16000;
  while(running&&!sb_ts_ready(tinysc_lifecycle)&&np_monotonic_ms()<until){
   sb_ts_status status;sb_ts_snapshot(tinysc_lifecycle,&status);
   if(status.state==SB_TS_FAILED)break;
   struct pollfd event={sb_ts_fd(tinysc_lifecycle),POLLIN,0};
   (void)poll(&event,1,100);sb_ts_drain(tinysc_lifecycle);
  }
  if(!running||!sb_ts_ready(tinysc_lifecycle)){
   sb_ts_status status;sb_ts_snapshot(tinysc_lifecycle,&status);
   fprintf(stderr,"TinySC arbitration failed: %s (%u processes)\n",status.error,status.count);
   for(unsigned row=0;row<status.count;row++){
    const sb_ts_process *p=&status.processes[row];
    fprintf(stderr,"TINYSC_REMAIN pid=%ld start=%llu role=%u state=%c parent=%ld\n",
     (long)p->pid,(unsigned long long)p->start,p->role,p->state,(long)p->parent);
   }
   sb_ts_close(tinysc_lifecycle);return 2;
  }
 }

	if (test_nowplaying)
		np_demo(nowplaying);

#if defined(SB_MUSIC_BROWSE_INTEGRATION) && !defined(SB_MUSIC_RUNTIME)
	if (music_fixture && test_storage_supported) {
		char canonical[4096], marker[4096];
		if (!realpath(music_fixture, canonical) ||
		    strcmp(canonical, music_fixture) ||
		    strncmp(canonical, "/tmp/sb-music-browse-", 21) ||
		    strlen(canonical) >= sizeof marker - 32) {
			fputs("Unapproved fixture path\n", stderr);
			return 2;
		}
		strcpy(marker, canonical);
		char *slash = strrchr(marker, '/');
		if (!slash)
			return 2;
		strcpy(slash + 1, "WP3B_TEST_FIXTURE");
		int fd = open(marker, O_RDONLY | O_NOFOLLOW);
		char proof[32] = {0};
		ssize_t got = fd < 0 ? -1 : read(fd, proof, sizeof proof - 1);
		if (fd >= 0)
			close(fd);
		if (got != 24 || strcmp(proof, "WP3B disposable fixture\n")) {
			fputs("Fixture marker missing\n", stderr);
			return 2;
		}
		sb_ml_options options = {0};
		snprintf(options.path, sizeof options.path, "%s", canonical);
		options.busy_ms = 20;
		music_library = sb_music_library_start(&options);
		if (!music_library)
			return 2;
		sb_ml_result result;
		uint64_t until = np_monotonic_ms() + 5000;
		while (sb_music_library_receive(music_library, &result) ==
		       SB_ML_BUSY) {
			if (np_monotonic_ms() >= until)
				return 2;
			struct timespec rest = {0, 1000000};
			nanosleep(&rest, NULL);
		}
		if (result.status != SB_ML_OK)
			return 2;
		sb_ml_request source = {0};
		source.operation = SB_ML_SOURCE;
		source.data.media = music_media_snapshot;
		if (sb_music_library_exchange(
			    music_library, &source, &result) != SB_ML_OK)
			return 2;
		music_browse = sb_browse_start(music_library,
					       &music_media_snapshot,
					       1,
					       test_storage_supported);
		if (!music_browse)
			return 2;
	}
#endif
#if defined(SB_MUSIC_PLAYBACK_INTEGRATION) && !defined(SB_MUSIC_RUNTIME)
	if (playback_network_fixture) {
		if(playback_root||music_browse||!local_mock_adapters||playback_ipc<0||!playback_http_port)return 2;
		sb_owner_init(&local_owner,1);
		if(!sb_owner_register(&local_owner,SB_OWNER_MUSIC,104))return 2;
		local_music=sb_playback_create(1,NULL,NULL,&local_owner,nowplaying,playback_ipc,playback_http_port,playback_send,NULL);
		if(!local_music||!sb_playback_probe_resources(local_music,np_monotonic_ms()))return 2;
	} else if (playback_root || playback_ipc >= 0 || playback_http_port) {
		if (!music_browse || !local_mock_adapters || !playback_root ||
		    playback_ipc < 0 || !playback_http_port ||
		    !playback_start(
			    playback_root, playback_ipc, playback_http_port))
			return 2;
	}
#endif
#ifdef SB_MUSIC_RUNTIME
    if((!music_browse_only && !ownership_bridge.enabled) || !music_runtime_start()){sb_ob_close(&ownership_bridge);return 2;}
    if (music_runtime.scan_only) {
        sb_ml_request close_request = {0};
        sb_ml_result result;
        close_request.operation = SB_ML_CLOSE;
        int ok = sb_music_library_exchange(music_library, &close_request, &result) == SB_ML_OK;
        uint64_t until = np_monotonic_ms() + 5000;
        if (ok) {
            sb_ml_status destroyed;
            while ((destroyed = sb_music_library_destroy(music_library)) == SB_ML_BUSY && np_monotonic_ms() < until) {
                struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
            }
            ok = destroyed == SB_ML_OK;
        }
        music_runtime_stop();
        sb_ob_close(&ownership_bridge);
        return ok ? 0 : 2;
    }
#endif
	udp = listener(SOCK_DGRAM, cfg.discovery_port);
	tcp = listener(SOCK_STREAM, cfg.slim_port);
	http = listener(SOCK_STREAM, cfg.http_port);
	if (udp < 0 || tcp < 0 || http < 0) {
		perror("listener");
		return 1;
	}
	for (i = 0; i < CLIENTS; i++) {
		cs[i].fd = -1;
		cs[i].upnp_fd=-1;cs[i].upnp_reply=NULL;cs[i].upnp_selection=NULL;cs[i].upnp_items=NULL;
        cs[i].ownership_wait=0;cs[i].ownership_connection=0;
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
		cs[i].music_connection = cs[i].music_control_context = 0;
#endif
		cs[i].close_after_drain = 0;
		memset(&cs[i].output, 0, sizeof cs[i].output);
#ifdef SB_MUSIC_BROWSE_INTEGRATION
		cs[i].music_http = NULL;
		cs[i].music_subscription = NULL;
		cs[i].music_sub_call = NULL;
		cs[i].music_menu_path[0] = cs[i].music_path[0] = 0;
		cs[i].music_menu_version = cs[i].music_view_version =
			cs[i].music_output_epoch = 0;
		cs[i].music_menu_visible = 0;
#endif
	}
	fprintf(stderr,
		"sbbase %s (LMS %s): %s UDP/TCP %u, HTTP %u\n",
		SBBASE_VERSION,
		cfg.lms_version,
		cfg.advertise_ip,
		cfg.slim_port,
		cfg.http_port);
	if (test_nowplaying)
		fprintf(stderr, "NOWPLAYING TEST enabled\n");
 sb_ts_operational(tinysc_lifecycle,1);

	while (running) {
		for(i=0;i<CLIENTS;i++)if(cs[i].fd>=0)upnp_pump(&cs[i]);
		if (reload_catalog) {
			reload_catalog = 0;
			if (cfg.catalog_path[0] &&
			    load_catalog(cfg.catalog_path) >= 0)
				fprintf(stderr,
					"catalog reloaded: %s\n",
					cfg.catalog_path);
			else
				fprintf(stderr,
					"catalog reload rejected; retaining "
					"active catalog\n");
		}
#ifdef SB_MUSIC_RUNTIME
        music_runtime_tick();
#endif
		ownership_events(cs);
		pf[0] = (struct pollfd){udp, POLLIN, 0};
		pf[1] = (struct pollfd){tcp, POLLIN, 0};
		pf[2] = (struct pollfd){http, POLLIN, 0};
		for (i = 0; i < CLIENTS; i++)
			pf[3 + i] = (struct pollfd){
				cs[i].fd,
				(short)((cs[i].close_after_drain ? 0 : POLLIN) |
					((cs[i].output.length || cs[i].upnp_reply) ? POLLOUT : 0)),
				0};
#ifdef SB_MUSIC_BROWSE_INTEGRATION
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
		playback_service();
		pf[4 + CLIENTS] =
			(struct pollfd){sb_playback_poll_fd(local_music, 0),
					sb_playback_ipc_events(local_music),
					0};
		pf[5 + CLIENTS] = (struct pollfd){
			sb_playback_poll_fd(local_music, 1), POLLIN, 0};
#endif
		music_events();
		pf[3 + CLIENTS] =
			(struct pollfd){sb_browse_fd(music_browse), POLLIN, 0};
		/* Pending large responses need writable readiness even between
		 * chunks. */
		for (i = 0; i < CLIENTS; i++)
			if (cs[i].fd >= 0 && cs[i].music_http &&
			    cs[i].music_http->sending)
				pf[3 + i].events |= POLLOUT;
#endif
  pf[poll_count]=(struct pollfd){sb_ts_fd(tinysc_lifecycle),POLLIN,0};
        for(i=0;i<CLIENTS;i++)pf[poll_count+1+i]=(struct pollfd){cs[i].upnp_fd,POLLIN,0};
		if (poll(pf, poll_count+1+CLIENTS, 1000) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
  sb_ts_drain(tinysc_lifecycle);
#ifdef SB_MUSIC_RUNTIME
  music_runtime_tick();
#endif

#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
		playback_service();
#endif
		if (np_expire(nowplaying))
			publish_playerstatus();
		int alarm_connected = 0;
		for (i = 0; i < CLIENTS; i++)
			if (cs[i].fd >= 0 && cs[i].kind == 1 && cs[i].helo &&
			    !strcmp(cs[i].slim_player, player_id))
				alarm_connected = 1;
		if (sb_alarms_tick(alarms,
				   time(NULL),
				   np_monotonic_ms(),
				   alarm_connected))
			publish_playerstatus();
		if (sb_alarms_active(alarms) && sb_sleep_cancel())
			publish_playerstatus();
		if (sb_sleep_tick(np_monotonic_ms(), alarm_connected)) {
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
			if (local_music &&
			    local_owner.value.current == SB_OWNER_MUSIC)
				sb_playback_control(
					local_music,
					'q',
					sb_queue_snapshot(
						sb_playback_queue(local_music))
						.context,
					np_monotonic_ms());
			else
#endif
				np_queue_command(nowplaying, "stop", 0);
			publish_playerstatus();
		}
		firmware_publish(NULL);

		if (pf[0].revents & POLLIN) {
			unsigned char in[SB_MAX_DISCOVERY + 1], out[1024];
			struct sockaddr_in peer;
			socklen_t pl = sizeof peer;
			ssize_t n = recvfrom(udp,
					     in,
					     sizeof in,
					     0,
					     (struct sockaddr *)&peer,
					     &pl);
			if (n > 0) {
				size_t z =
					sb_discovery_response(in,
							      (size_t)n,
							      out,
							      sizeof out,
							      &cfg,
							      cfg.advertise_ip);
				if (z)
					sendto(udp,
					       out,
					       z,
					       0,
					       (struct sockaddr *)&peer,
					       pl);
			}
		}

		for (int li = 1; li <= 2; li++)
			if (pf[li].revents & POLLIN) {
				int fd = accept(pf[li].fd, NULL, NULL);
				if(fd>=0&&nonblock(fd)<0){close(fd);fd=-1;}
				if (fd >= 0) {
					for (i = 0;
					     i < CLIENTS && cs[i].fd >= 0;
					     i++)
						;
					if (i == CLIENTS) {
						fprintf(stderr,
							"ACCEPT REJECT kind=%d "
							"fd=%d full\n",
							li,
							fd);
						close(fd);
					} else {
						fprintf(stderr,
							"ACCEPT kind=%d fd=%d "
							"slot=%d\n",
							li,
							fd,
							i);
						cs[i].fd = fd;
						cs[i].kind = -li;
						cs[i].used = 0;
						cs[i].helo = 0;
						cs[i].close_after_drain = 0;
						cs[i].http_resume = 0;
						cs[i].keepalive = time(NULL);
						cs[i].slim_poll = cs[i].keepalive;
						cs[i].client_id[0] = 0;
						cs[i].playerstatus_subscribed =
							0;
						cs[i].subscribed_player[0] = 0;
					}
				}
			}

		for (i = 0; i < CLIENTS; i++)
			if (cs[i].fd >= 0) {
				short rev;
				if (cs[i].kind < 0) {
					cs[i].kind = -cs[i].kind;
					continue;
				}
				rev = pf[3 + i].revents;
				if (sb_output_expired(&cs[i].output,
						      np_monotonic_ms()) ||
				    ((rev & POLLOUT) &&
				     sb_output_flush(&cs[i].output,
						     cs[i].fd,
						     np_monotonic_ms()) < 0)) {
					close_client(&cs[i]);
					continue;
				}
				if (cs[i].close_after_drain &&
				    !cs[i].output.length) {
					close_client(&cs[i]);
					continue;
				}
				if (rev & (POLLERR | POLLHUP | POLLNVAL)) {
					close_client(&cs[i]);
					continue;
				}
				if(cs[i].upnp_fd>=0||cs[i].upnp_reply){upnp_pump(&cs[i]);continue;}
#ifdef SB_MUSIC_BROWSE_INTEGRATION
				if (cs[i].music_http) {
					if (rev & POLLIN) {
						char byte;
						ssize_t n = recv(
							cs[i].fd,
							&byte,
							1,
							MSG_PEEK |
								MSG_DONTWAIT);
						if (n == 0 || n > 0) {
							close_client(&cs[i]);
							continue;
						}
					}
					music_http_pump(&cs[i]);
					continue;
				}
#endif
				if ((rev & POLLIN) &&
				    !cs[i].close_after_drain) {
					ssize_t n = recv(cs[i].fd,
							 cs[i].buf + cs[i].used,
							 sizeof(cs[i].buf) - 1 -
								 cs[i].used,
							 0);
					if (n < 0 && (errno == EINTR ||
						      errno == EAGAIN ||
						      errno == EWOULDBLOCK))
						continue;
					if (n <= 0) {
						close_client(&cs[i]);
						continue;
					}
						cs[i].keepalive = time(NULL);
					if (test_nowplaying && cs[i].kind == 2 && n > 0) {
						char hx[49];
						int m = n < 16 ? (int)n : 16;
						for (int q = 0; q < m; q++)
							sprintf(hx + 3 * q,
								"%02x ",
								cs[i].buf[cs[i].used +
									 (size_t)q]);
						hx[3 * m] = 0;
						fprintf(stderr,
							"COMET RECV slot=%d fd=%d "
							"n=%ld used=%lu head=%s\n",
							i,
							cs[i].fd,
							(long)n,
							(unsigned long)cs[i].used,
							hx);
					}
					cs[i].used += (size_t)n;
					cs[i].buf[cs[i].used] = 0;
					if (cs[i].kind == 1)
						handle_slim(&cs[i]);
					else if (cs[i].kind == 2)
						handle_http(&cs[i], &cfg);
				}
				/* A completed music request may have shifted
				 * pipelined bytes down; re-enter the parser
				 * without waiting for the next recv. */
				if (cs[i].http_resume && !cs[i].close_after_drain
#ifdef SB_MUSIC_BROWSE_INTEGRATION
				    && !cs[i].music_http
#endif
				) {
					cs[i].http_resume = 0;
					if (cs[i].fd >= 0 && cs[i].kind == 2)
						handle_http(&cs[i], &cfg);
				}
				if (cs[i].fd >= 0 && cs[i].kind == 1 &&
				    cs[i].helo &&
				    time(NULL) - cs[i].slim_poll >= 4)
					keepalive(&cs[i]);
				if (cs[i].fd >= 0 && cs[i].kind == 2 &&
				    time(NULL) - cs[i].keepalive >=
					    HTTP_IDLE_SECONDS) {
					fprintf(stderr,
						"HTTP IDLE CLOSE fd=%d\n",
						cs[i].fd);
					close_client(&cs[i]);
				}
				if (cs[i].fd >= 0 && !cs[i].close_after_drain &&
				    cs[i].kind == 3 &&
				    time(NULL) - cs[i].keepalive >=
					    STREAM_HEARTBEAT_SECONDS)
					stream_heartbeat(&cs[i]);
			}
	}
	for (i = 0; i < CLIENTS; i++)
		close_client(&cs[i]);
	close(udp);
	close(tcp);
	close(http);
#ifdef SB_MUSIC_BROWSE_INTEGRATION
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
	if (local_music) {
		uint64_t until = np_monotonic_ms() + 5000;
		while (!sb_playback_destroy(local_music)) {
			if (np_monotonic_ms() >= until)
				return 2;
			struct timespec rest = {0, 1000000};
			nanosleep(&rest, NULL);
		}
		local_music = NULL;
	}
#endif
	if (music_browse) {
		while (sb_browse_stop(music_browse) == SB_ML_BUSY) {
			struct timespec rest = {0, 1000000};
			nanosleep(&rest, NULL);
		}
		uint64_t until = np_monotonic_ms() + 5000;
		while (sb_browse_destroy(music_browse) == SB_ML_BUSY) {
			if (np_monotonic_ms() >= until)
				return 2;
			struct timespec rest = {0, 1000000};
			nanosleep(&rest, NULL);
		}
		sb_ml_request close_request = {0};
		sb_ml_result result;
		close_request.operation = SB_ML_CLOSE;
		if (sb_music_library_exchange(
			    music_library, &close_request, &result) == SB_ML_OK)
			(void)sb_music_library_destroy(music_library);
	}
#endif
#ifdef SB_MUSIC_RUNTIME
    music_runtime_stop();
#endif
	sb_ob_close(&ownership_bridge);
 sb_ts_operational(tinysc_lifecycle,0);
 sb_ts_close(tinysc_lifecycle);
	np_destroy(nowplaying);
	sb_alarms_close(alarms);
	return 0;
}
