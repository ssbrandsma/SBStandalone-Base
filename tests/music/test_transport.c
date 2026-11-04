#define _POSIX_C_SOURCE 200809L
#include "slim_controller.h"
#include "output_queue.h"
#include "sbbase.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint64_t nanos(void)
{
	struct timespec t;
	assert(!clock_gettime(CLOCK_MONOTONIC, &t));
	return (uint64_t)t.tv_sec * 1000000000u + t.tv_nsec;
}

static void put(unsigned char *p, uint32_t n)
{
	p[0] = n >> 24;
	p[1] = n >> 16;
	p[2] = n >> 8;
	p[3] = n;
}

static sb_slim_hello h;
static sb_music_stream_id id = {1, 3, 1, 1, {{{1}}, 1}};

static sb_slim_event status(const char *code, uint32_t j)
{
	unsigned char p[51] = {0};
	sb_slim_event e;
	memcpy(p, code, 4);
	put(p + 7, 4096);
	put(p + 11, 2048);
	put(p + 29, 8192);
	put(p + 33, 4096);
	put(p + 25, j);
	put(p + 43, 1234);
	assert(sb_slim_parse("STAT", p, 51, &e) == 1);
	return e;
}

static sb_slim_event response(int ok)
{
	const char *p = ok ? "HTTP/1.0 200 OK\r\nContent-Length: 298\r\n\r\n"
			   : "HTTP/1.1 404 Not Found\r\n\r\n";
	sb_slim_event e;
	assert(sb_slim_parse("RESP", (const unsigned char *)p, strlen(p), &e) ==
	       1);
	return e;
}

static void parsing(void)
{
	unsigned char p[1100] = {7, 0, 2, 0, 0, 0, 0, 1};
	sb_slim_event e, unchanged;
	memcpy(p + 34, "ENModel=fab4,Firmware=7.7.3,mp3,flc,ogg", 39);
	size_t n = 34 + strlen((const char *)p + 34);
	assert(sb_slim_parse("HELO", p, n, &e) == 1);
	h = e.hello;
	assert(h.device == 7 && !strcmp(h.model, "fab4") &&
	       !strcmp(h.firmware, "7.7.3") && h.codecs == 7);
	p[24] = 0x40;
	p[26] = 1;
	p[30] = 2;
	assert(sb_slim_parse("HELO", p, n, &e) == 1 && e.hello.reconnect &&
	       e.hello.bytes_received ==
		       ((uint64_t)0x1000000 << 32) + 0x2000000);
	for (size_t i = 0; i < 36; i++) {
		if (i == 20)
			continue;
		assert(sb_slim_parse("HELO", p, i, &e) == -1);
	}
	memset(&unchanged, 0xa5, sizeof unchanged);
	e = unchanged;
	p[2] = 3;
	assert(sb_slim_parse("HELO", p, n, &e) == -1 &&
	       !memcmp(&e, &unchanged, sizeof e));
	p[2] = 2;
	memset(p, 0, sizeof p);
	p[0] = 7;
	p[2] = 2;
	memcpy(p + 18, "EN", 2);
	assert(sb_slim_parse("HELO", p, 20, &e) == 1 && !e.hello.has_uuid &&
	       !e.hello.codecs);
	memset(p, 0, sizeof p);
	memcpy(p, "STMs", 4);
	put(p + 7, 4096);
	put(p + 29, 8192);
	for (size_t i = 0; i < 60; i++)
		assert(sb_slim_parse("STAT", p, i, &e) ==
		       ((i == 51 || i == 53 || i == 57) ? 1 : -1));
	put(p + 11, 4097);
	assert(sb_slim_parse("STAT", p, 51, &e) == -1);
	p[0] = 0;
	assert(sb_slim_parse("DSCO", p, 1, &e) == 1);
	p[0] = 4;
	assert(sb_slim_parse("DSCO", p, 1, &e) == 1);
	p[0] = 5;
	assert(sb_slim_parse("DSCO", p, 1, &e) == -1);
	assert(sb_slim_parse("DSCO", p, 2, &e) == -1);
	assert(sb_slim_parse("UNKN", NULL, 0, &e) == 0);
	const char *bad[] = {"HTTP/1.1 200 OK\n\n",
			     "HTTP/1.1 200 OK\r\n\r\nbody",
			     "HTTP/1.1 600 Bad\r\n\r\n",
			     "HTTP/1.1 20x Bad\r\n\r\n"};
	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
		assert(sb_slim_parse("RESP",
				     (const unsigned char *)bad[i],
				     strlen(bad[i]),
				     &e) == -1);
	unsigned char frames[160] = {0};
	size_t consumed;
	const unsigned char *payload;
	uint32_t len;
	char op[5];
	memcpy(frames, "STAT", 4);
	put(frames + 4, 51);
	memcpy(frames + 8, "STMt", 4);
	for (size_t i = 0; i < 59; i++)
		assert(sb_parse_client_frame(
			       frames, i, op, &payload, &len, &consumed) == 0);
	memcpy(frames + 59, frames, 59);
	assert(sb_parse_client_frame(
		       frames, 118, op, &payload, &len, &consumed) == 1 &&
	       consumed == 59);
	assert(sb_parse_client_frame(frames + consumed,
				     118 - consumed,
				     op,
				     &payload,
				     &len,
				     &consumed) == 1);
	put(frames + 4, 65537);
	assert(sb_parse_client_frame(
		       frames, 8, op, &payload, &len, &consumed) == -1);
	/* Bounded arbitrary-byte probes; deterministic, no live downloads. */
	uint32_t random = 1;
	for (unsigned iter = 0; iter < 10000; iter++) {
		for (size_t i = 0; i < sizeof p; i++) {
			random = random * 1664525u + 1013904223u;
			p[i] = (unsigned char)(random >> 24);
		}
		const char *ops[] = {"HELO", "STAT", "RESP", "DSCO"};
		(void)sb_slim_parse(ops[iter % 4], p, iter % sizeof p, &e);
	}
}

static void begin(sb_transport *t)
{
	assert(sb_transport_init(t, 1));
	assert(sb_transport_attach(t, 1, &h));
	assert(!sb_transport_prepare(t, &id, 'm', 0));
	assert(sb_transport_quiescent(t, 1, 3));
	assert(!sb_transport_prepare(t, &id, 'a', 0));
	assert(sb_transport_prepare(t, &id, 'm', 0));
	assert(sb_transport_command(t, &id, 's', 0));
}

static int ev(sb_transport *t, const char *code, unsigned j)
{
	sb_slim_event e = status(code, j);
	return sb_transport_event(t, 1, &id, &e, j);
}

static void transport(void)
{
	sb_transport t, disabled;
	memset(&disabled, 0xa5, sizeof disabled);
	t = disabled;
	assert(!sb_transport_init(&t, 0) && !memcmp(&t, &disabled, sizeof t));
	begin(&t);
	assert(ev(&t, "STMf", 0));
	assert(ev(&t, "STMc", 1));
	assert(!ev(&t, "STMc", 1));
	assert(!ev(&t, "STMc", 2));
	assert(ev(&t, "STMe", 2));
	assert(ev(&t, "STMh", 3));
	assert(ev(&t, "STMl", 4));
	assert(!ev(&t, "STMs", 5));
	sb_slim_event r = response(1);
	assert(sb_transport_event(&t, 1, &id, &r, 5));
	assert(ev(&t, "STMs", 6) && t.state == SB_T_PLAYING);
	assert(!ev(&t, "STMs", 7));
	assert(t.elapsed_ms == 1234);
	assert(sb_transport_command(&t, &id, 'p', 8));
	assert(ev(&t, "STMp", 9) && t.state == SB_T_PAUSED);
	assert(sb_transport_command(&t, &id, 'u', 10));
	assert(ev(&t, "STMr", 11) && t.state == SB_T_PLAYING);
	assert(!ev(&t, "STMp", 10)); /* stale jiffies */
	unsigned char reason = 0;
	sb_slim_event d;
	assert(sb_slim_parse("DSCO", &reason, 1, &d) == 1);
	assert(sb_transport_event(&t, 1, &id, &d, 12) &&
	       !t.stream.terminal_seen);
	assert(ev(&t, "STMd", 13) && t.state == SB_T_DRAINING &&
	       !t.stream.terminal_seen);
	assert(!ev(&t, "STMd", 14));
	assert(ev(&t, "STMu", 15) && t.terminal_notifications == 1);
	assert(!ev(&t, "STMu", 16) && !ev(&t, "STMd", 17));
	sb_music_stream_id next = id;
	next.epoch = 2;
	assert(!sb_transport_prepare(&t, &next, 'm', 20));
	sb_transport_detach(&t, 1);
	assert(sb_transport_attach(&t, 2, &h));
	assert(!ev(&t, "STMs", 18));
	assert(!sb_transport_attach(&t, 1, &h));
	assert(!sb_transport_quiescent(&t, 1, 3));
	assert(sb_transport_quiescent(&t, 2, 4));
	next.connection_generation = 2;
	next.owner_generation = 4;
	assert(sb_transport_prepare(&t, &next, 'f', 20));
	assert(sb_transport_command(&t, &next, 's', 20));
	assert(!sb_transport_event(&t, 2, &id, &r, 21));
	sb_transport_detach(&t, 2);
	assert(t.error == SB_T_CONNECTION_LOST &&
	       t.terminal_notifications == 2);
	begin(&t);
	r = response(0);
	assert(sb_transport_event(&t, 1, &id, &r, 2) &&
	       t.error == SB_T_HTTP_ERROR);
	begin(&t);
	assert(ev(&t, "STMn", 1) && t.error == SB_T_CODEC_ERROR);
	begin(&t);
	assert(sb_transport_tick(&t, 10000) && t.error == SB_T_TIMEOUT);
	assert(!sb_transport_tick(&t, 20000));
	begin(&t);
	assert(sb_transport_command(&t, &id, 'q', 2));
	assert(ev(&t, "STMf", 3));
	assert(!ev(&t, "STMs", 4));
	assert(!t.stream.terminal_seen && !sb_transport_stopped(&t, &id, 0));
	assert(sb_transport_stopped(&t, &id, 1) &&
	       t.terminal_notifications == 1);
	assert(!sb_transport_stopped(&t, &id, 1));
	begin(&t);
	assert(ev(&t, "STMd", 1));
	assert(ev(&t, "STMu", 2) &&
	       t.error == SB_T_PROTOCOL_ERROR); /* no audible start */
	begin(&t);
	reason = 2;
	assert(sb_slim_parse("DSCO", &reason, 1, &d) == 1);
	assert(sb_transport_event(&t, 1, &id, &d, 2) &&
	       t.error == SB_T_INPUT_ERROR);
	begin(&t);
	r = response(1);
	assert(sb_transport_event(&t, 1, &id, &r, 1));
	assert(ev(&t, "STMs", 2));
	assert(ev(&t, "STMo", 3) && t.state == SB_T_BUFFERING);
	assert(ev(&t, "STMt", 0xfffffff0u) == 0); /* older across wrap */
	t.last_jiffies = 0xfffffff0u;
	assert(ev(
		&t, "STMt", 4)); /* ordered after uint32 firmware clock wrap */
	assert(!ev(&t, "STMt", 4));
	sb_transport_detach(&t, 1);
	assert(t.state == SB_T_DISCONNECTED);
}

static void setup_owner(sb_physical_owner *o, sb_music_owner first)
{
	assert(sb_owner_init(o, 1));
	for (int i = 1; i <= 4; i++)
		assert(sb_owner_register(o, (sb_music_owner)i, 100 + i));
	assert(sb_owner_connect(o, 1, 1));
	assert(sb_owner_request(o, first, 1, 0, 100) == 1);
}

static void ownership(void)
{
	sb_physical_owner o, unchanged;
	memset(&o, 0xa5, sizeof o);
	unchanged = o;
	assert(!sb_owner_init(&o, 0) && !memcmp(&o, &unchanged, sizeof o));
	for (int from = 1; from <= 4; from++)
		for (int to = 1; to <= 4; to++)
			if (from != to) {
				setup_owner(&o, (sb_music_owner)from);
				uint64_t g = o.value.generation;
				assert(sb_owner_controls(
					&o, (sb_music_owner)from, g, 1));
				assert(sb_owner_request(&o,
							(sb_music_owner)to,
							2,
							1,
							100) == 2);
				assert(!sb_owner_controls(
					&o, (sb_music_owner)to, g, 1));
				assert(!sb_owner_request(
					&o, SB_OWNER_MUSIC, 3, 2, 100));
				assert(!sb_owner_ack(&o,
						     (sb_music_owner)from,
						     100 + from,
						     1,
						     g,
						     1,
						     2,
						     1,
						     1,
						     1,
						     1));
				assert(!sb_owner_ack(&o,
						     (sb_music_owner)from,
						     100 + from,
						     2,
						     g + 1,
						     1,
						     2,
						     1,
						     1,
						     1,
						     1));
				assert(sb_owner_ack(&o,
						    (sb_music_owner)from,
						    100 + from,
						    2,
						    g,
						    1,
						    2,
						    1,
						    1,
						    1,
						    1));
				assert(o.value.current == (sb_music_owner)to &&
				       o.value.generation == g + 1);
				assert(!sb_owner_ack(&o,
						     (sb_music_owner)from,
						     100 + from,
						     2,
						     g,
						     1,
						     3,
						     1,
						     1,
						     1,
						     1));
				assert(!sb_owner_controls(
					&o, (sb_music_owner)from, g, 1));
				assert(sb_owner_controls(
					&o, (sb_music_owner)to, g + 1, 1));
				assert(!sb_owner_release(&o,
							 (sb_music_owner)to,
							 100 + to,
							 g + 1,
							 1,
							 1,
							 0,
							 1));
				assert(sb_owner_release(&o,
							(sb_music_owner)to,
							100 + to,
							g + 1,
							1,
							1,
							1,
							1));
				assert(!sb_owner_release(&o,
							 (sb_music_owner)to,
							 100 + to,
							 g + 1,
							 1,
							 1,
							 1,
							 1));
			}
	for (int proof = 0; proof < 4; proof++) {
		setup_owner(&o, SB_OWNER_RADIO);
		uint64_t g = o.value.generation;
		assert(sb_owner_request(&o, SB_OWNER_MUSIC, 2, 1, 100) == 2);
		assert(sb_owner_ack(&o,
				    SB_OWNER_RADIO,
				    101,
				    2,
				    g,
				    1,
				    2,
				    proof != 0,
				    proof != 1,
				    proof != 2,
				    proof != 3));
		assert(o.value.current == SB_OWNER_RADIO &&
		       o.value.generation == g &&
		       o.value.yield == SB_YIELD_REFUSED);
	}
	setup_owner(&o, SB_OWNER_SPOTIFY);
	uint64_t g = o.value.generation;
	assert(sb_owner_request(&o, SB_OWNER_MUSIC, 2, 1, 100) == 2);
	assert(sb_owner_tick(&o, 101));
	assert(!sb_owner_ack(
		       &o, SB_OWNER_SPOTIFY, 102, 2, g, 1, 102, 1, 1, 1, 1) &&
	       o.value.current == SB_OWNER_SPOTIFY);
	assert(sb_owner_request(&o, SB_OWNER_MUSIC, 3, 103, 100) == 2);
	sb_owner_suspend(&o, 1);
	assert(!sb_owner_request(&o, SB_OWNER_MUSIC, 4, 104, 100));
	assert(!sb_owner_ack(
		&o, SB_OWNER_SPOTIFY, 102, 3, g, 1, 105, 1, 1, 1, 1));
	sb_owner_suspend(&o, 0);
	assert(sb_owner_request(&o, SB_OWNER_MUSIC, 5, 106, 100) == 2);
	sb_owner_disconnect(&o, 1);
	assert(!sb_owner_ack(
		&o, SB_OWNER_SPOTIFY, 102, 5, g, 1, 107, 1, 1, 1, 1));
	assert(sb_owner_connect(&o, 2, 1));
	assert(!sb_owner_request(&o, SB_OWNER_MUSIC, 6, 108, 100));
	assert(sb_owner_release(
		&o, SB_OWNER_SPOTIFY, 102, o.value.generation, 2, 1, 1, 1));
	assert(sb_owner_request(&o, SB_OWNER_MUSIC, 7, 109, 100) == 1);
	assert(sb_owner_crashed(&o, SB_OWNER_MUSIC, 104));
	assert(!sb_owner_request(&o, SB_OWNER_RADIO, 8, 110, 100));
	assert(sb_owner_register(&o, SB_OWNER_MUSIC, 204));
	assert(sb_owner_release(
		&o, SB_OWNER_MUSIC, 204, o.value.generation, 2, 1, 1, 1));
	assert(sb_owner_request(&o, SB_OWNER_RADIO, 9, 111, 100) == 1);
	o.value.generation = UINT64_MAX;
	assert(!sb_owner_request(&o, SB_OWNER_MUSIC, 10, 112, 100));
	setup_owner(&o, SB_OWNER_MUSIC);
	sb_transport t;
	assert(sb_transport_init(&t, 1));
	assert(sb_transport_attach(&t, 1, &h));
	assert(sb_transport_quiescent(&t, 1, o.value.generation));
	sb_music_stream_id owned = id;
	owned.owner_generation = o.value.generation;
	assert(sb_transport_owned_prepare(&t, &o, &owned, 'm', 1));
	assert(sb_transport_owned_command(&t, &o, &owned, 's', 1));
	assert(sb_owner_request(&o, SB_OWNER_RADIO, 2, 2, 100) == 2);
	assert(!sb_transport_owned_command(&t, &o, &owned, 's', 3));
	assert(sb_transport_owned_command(&t, &o, &owned, 'q', 3));
	assert(!sb_transport_stopped(&t, &owned, 0));
	assert(sb_transport_stopped(&t, &owned, 1));
	assert(sb_owner_ack(&o,
			    SB_OWNER_MUSIC,
			    104,
			    2,
			    o.value.generation,
			    1,
			    4,
			    1,
			    1,
			    1,
			    1));
	assert(!sb_transport_owned_command(&t, &o, &owned, 'q', 5));
}

static void outputs(void)
{
	int fd[2], small = 1024;
	sb_output q = {0};
	unsigned char data[SB_OUTPUT_CAPACITY], got[SB_OUTPUT_CAPACITY];
	for (size_t i = 0; i < sizeof data; i++)
		data[i] = (unsigned char)(i % 251);
	assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fd));
	assert(!setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof small));
	assert(sb_output_scope(&q, 1, 2, 3));
	assert(!sb_output_scoped(&q, fd[0], 1, 2, 4, data, 10, 0));
	assert(sb_output_scoped(&q, fd[0], 1, 2, 3, data, sizeof data, 1));
	assert(q.length);
	assert(!sb_output_scope(&q, 2, 2, 3));
	assert(!sb_output_enqueue(&q, fd[0], data, sizeof data, 2));
	assert(sb_output_flush(&q, fd[0], 3) == 0);
	size_t total = 0;
	for (unsigned i = 0; i < 200 && total < sizeof data; i++) {
		ssize_t n = recv(
			fd[1], got + total, sizeof got - total, MSG_DONTWAIT);
		if (n > 0)
			total += (size_t)n;
		assert(sb_output_flush(&q, fd[0], 4) >= 0);
	}
	assert(total == sizeof data && !memcmp(data, got, total) && !q.length &&
	       !q.data);
	assert(sb_output_enqueue(&q, fd[0], data, sizeof data, 10));
	assert(q.length && sb_output_expired(&q, 15010));
	assert(sb_output_flush(&q, fd[0], 15010) == -1);
	sb_output_clear(&q);
	close(fd[1]);
	assert(!sb_output_enqueue(&q, fd[0], data, 10, 0));
	close(fd[0]);
	for (int i = 0; i < 10000; i++) {
		assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fd));
		assert(sb_output_enqueue(&q, fd[0], data, 1, 0));
		sb_output_clear(&q);
		close(fd[0]);
		close(fd[1]);
	}
}

static void hex(const char *name, const unsigned char *p, size_t n)
{
	printf("%s ", name);
	for (size_t i = 0; i < n; i++)
		printf("%02x", p[i]);
	puts("");
}

static void wire(void)
{
	unsigned char p[512];
	sb_slim_start s = {'m', 9000, {0}, 64, 20};
	memset(s.token, 'a', 64);
	size_t n = sb_slim_strm('s', 0, &s, p, sizeof p);
	assert(n);
	hex("start", p, n);
	const char *cmd = "puqft";
	for (size_t i = 0; i < 5; i++) {
		char name[] = {cmd[i], 0};
		n = sb_slim_strm(cmd[i],
				 i < 2 || i == 4 ? 0x12345678 : 0,
				 NULL,
				 p,
				 sizeof p);
		assert(n);
		hex(name, p, n);
	}
	n = sb_slim_audg(65536, 32768, 0x12345678, p, sizeof p);
	hex("audg", p, n);
	n = sb_slim_aude(1, p, sizeof p);
	hex("aude", p, n);
	n = sb_slim_cont(p, sizeof p);
	hex("cont", p, n);
	assert(!sb_slim_audg(65537, 0, 0, p, sizeof p));
	assert(!sb_slim_aude(2, p, sizeof p));
	assert(!sb_slim_strm('q', 1, NULL, p, sizeof p));
	s.token[0] = '\r';
	assert(!sb_slim_strm('s', 0, &s, p, sizeof p));
	s.token[0] = 'a';
	s.port = 65536;
	assert(!sb_slim_strm('s', 0, &s, p, sizeof p));
	assert(!sb_slim_cont(p, 12));
	assert(!sb_slim_strm('t', 0, NULL, p, 29));
}

int main(int argc, char **argv)
{
	if (argc == 4 && !strcmp(argv[1], "--decode")) {
		unsigned char payload[4096];
		size_t n = strlen(argv[3]);
		sb_slim_event e;
		if (strlen(argv[2]) != 4 || n % 2 || n / 2 > sizeof payload)
			return 2;
		for (size_t i = 0; i < n / 2; i++) {
			unsigned byte;
			if (sscanf(argv[3] + 2 * i, "%2x", &byte) != 1)
				return 2;
			payload[i] = (unsigned char)byte;
		}
		if (sb_slim_parse(argv[2], payload, n / 2, &e) != 1)
			return 2;
		printf("%u\n", (unsigned)e.kind);
		return 0;
	}
	parsing();
	transport();
	ownership();
	outputs();
	if (argc > 1 && !strcmp(argv[1], "--wire")) {
		wire();
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "--benchmark")) {
		sb_slim_event e = status("STMt", 1);
		unsigned char p[51] = {0};
		memcpy(p, "STMt", 4);
		uint64_t start = nanos();
		for (unsigned i = 0; i < 1000000; i++)
			assert(sb_slim_parse("STAT", p, 51, &e) == 1);
		uint64_t parse = nanos() - start;
		sb_physical_owner o;
		start = nanos();
		for (unsigned i = 0; i < 100000; i++) {
			setup_owner(&o, SB_OWNER_RADIO);
			assert(sb_owner_request(
				       &o, SB_OWNER_MUSIC, 2, 1, 100) == 2);
			assert(sb_owner_ack(&o,
					    SB_OWNER_RADIO,
					    101,
					    2,
					    o.value.generation,
					    1,
					    2,
					    1,
					    1,
					    1,
					    1));
		}
		uint64_t handoff = nanos() - start;
		sb_transport t;
		begin(&t);
		sb_slim_event timer = status("STMt", 0);
		start = nanos();
		for (unsigned i = 0; i < 1000000; i++) {
			timer.jiffies = i;
			assert(sb_transport_event(&t, 1, &id, &timer, 1));
		}
		uint64_t events = nanos() - start;
		printf("{\"transport_bytes\":%zu,\"event_bytes\":%zu,\"owner_"
		       "bytes\":%zu,\"output_state_bytes\":%zu,\"queue_"
		       "capacity\":%u,\"parse_ns_per_event\":%.3f,\"event_ns\":"
		       "%.3f,\"simulated_handoff_ns\":%.3f}\n",
		       sizeof t,
		       sizeof e,
		       sizeof o,
		       sizeof(sb_output),
		       SB_OUTPUT_CAPACITY,
		       parse / 1000000.0,
		       events / 1000000.0,
		       handoff / 100000.0);
	} else
		puts("SlimProto, transport, ownership and bounded output tests "
		     "passed");
	return 0;
}
