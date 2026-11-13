#include "slim_controller.h"
#include "sbbase.h"
#include <stdio.h>
#include <string.h>

static uint16_t be16(const unsigned char *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t be32(const unsigned char *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | p[3];
}

static void put32(unsigned char *p, uint32_t n)
{
	p[0] = (unsigned char)(n >> 24);
	p[1] = (unsigned char)(n >> 16);
	p[2] = (unsigned char)(n >> 8);
	p[3] = (unsigned char)n;
}

static int field(char *out, size_t cap, const char *p, size_t n)
{
	if (!n || n >= cap ||
	    (out[0] && (strlen(out) != n || memcmp(out, p, n))))
		return 0;
	memcpy(out, p, n);
	out[n] = 0;
	return 1;
}

static int hello(const unsigned char *p, size_t n, sb_slim_hello *h)
{
	size_t fixed = n == 20 ? 20 : 36, offset = fixed == 20 ? 8 : 24;
	unsigned any = 0;
	if (n < fixed || n - fixed > SB_SLIM_CAPS_MAX || !p[0] || (p[2] & 1))
		return 0;
	for (size_t i = 2; i < 8; i++)
		any |= p[i];
	if (!any)
		return 0;
	h->device = p[0];
	h->revision = p[1];
	memcpy(h->mac, p + 2, 6);
	if (fixed == 36) {
		memcpy(h->uuid, p + 8, 16);
		h->has_uuid = 1;
	}
	h->flags = be16(p + offset);
	h->reconnect = !!(h->flags & 0x4000);
	h->bytes_received =
		((uint64_t)be32(p + offset + 2) << 32) | be32(p + offset + 6);
	for (size_t i = 0; i < 2; i++) {
		unsigned c = p[offset + 10 + i];
		if (c < 'A' || c > 'Z')
			return 0;
		h->locale[i] = (char)c;
	}
	for (size_t i = fixed; i < n; i++)
		if (p[i] < 32 || p[i] > 126)
			return 0;
	memcpy(h->capabilities, p + fixed, n - fixed);
	const char *t = h->capabilities;
	while (*t) {
		const char *end = strchr(t, ',');
		size_t len = end ? (size_t)(end - t) : strlen(t);
		if (!len)
			return 0;
		if (len == 3 && !memcmp(t, "mp3", 3))
			h->codecs |= SB_SLIM_CODEC_MP3;
		if (len == 3 && !memcmp(t, "flc", 3))
			h->codecs |= SB_SLIM_CODEC_FLAC;
		if (len == 3 && !memcmp(t, "ogg", 3))
			h->codecs |= SB_SLIM_CODEC_VORBIS;
		if (len >= 6 && !memcmp(t, "Model=", 6) &&
		    !field(h->model, sizeof h->model, t + 6, len - 6))
			return 0;
		if (len >= 9 && !memcmp(t, "Firmware=", 9) &&
		    !field(h->firmware, sizeof h->firmware, t + 9, len - 9))
			return 0;
		if (!end)
			break;
		t = end + 1;
		if (!*t)
			return 0;
	}
	return 1;
}

int sb_slim_parse(const char op[4],
		  const unsigned char *p,
		  size_t n,
		  sb_slim_event *out)
{
	sb_slim_event e;
	if (!op || !out || (!p && n))
		return -1;
	memset(&e, 0, sizeof e);
	if (!memcmp(op, "HELO", 4)) {
		e.kind = SB_SLIM_HELO;
		if (!p || !hello(p, n, &e.hello))
			return -1;
	} else if (!memcmp(op, "STAT", 4)) {
		if (!p || (n != 51 && n != 53 && n != 57))
			return -1;
		e.kind = SB_SLIM_STAT;
		for (size_t i = 0; i < 4; i++)
			if (p[i] < 32 || p[i] > 126)
				return -1;
		memcpy(e.code, p, 4);
		e.decode_size = be32(p + 7);
		e.decode_full = be32(p + 11);
		e.bytes_received =
			((uint64_t)be32(p + 15) << 32) | be32(p + 19);
		e.signal = be16(p + 23);
		e.jiffies = be32(p + 25);
		e.output_size = be32(p + 29);
		e.output_full = be32(p + 33);
		e.voltage = be16(p + 41);
		e.elapsed_ms = be32(p + 43);
		e.server_timestamp = be32(p + 47);
		if (n >= 53)
			e.error_code = be16(p + 51);
		if (e.decode_full > e.decode_size ||
		    e.output_full > e.output_size)
			return -1;
	} else if (!memcmp(op, "RESP", 4)) {
		if (!p || n < 16 || n > SB_SLIM_HTTP_MAX ||
		    memcmp(p, "HTTP/1.", 7) || (p[7] != '0' && p[7] != '1') ||
		    p[8] != ' ' || p[9] < '1' || p[9] > '5' || p[10] < '0' ||
		    p[10] > '9' || p[11] < '0' || p[11] > '9' || p[12] != ' ' ||
		    memcmp(p + n - 4, "\r\n\r\n", 4))
			return -1;
		for (size_t i = 0; i < n; i++) {
			if (!p[i] ||
			    (p[i] < 32 && p[i] != '\r' && p[i] != '\n' &&
			     p[i] != '\t') ||
			    p[i] == 127)
				return -1;
			if ((p[i] == '\r' &&
			     (i + 1 == n || p[i + 1] != '\n')) ||
			    (p[i] == '\n' && (!i || p[i - 1] != '\r')))
				return -1;
			if (i + 4 < n && !memcmp(p + i, "\r\n\r\n", 4))
				return -1;
		}
		e.kind = SB_SLIM_RESP;
		e.http_status = (uint16_t)((p[9] - '0') * 100 +
					   (p[10] - '0') * 10 + p[11] - '0');
	} else if (!memcmp(op, "DSCO", 4)) {
		if (!p || n != 1 || p[0] > 4)
			return -1;
		e.kind = SB_SLIM_DSCO;
		e.reason = p[0];
	} else
		return 0;
	*out = e;
	return 1;
}

size_t sb_slim_strm(char cmd,
		    uint32_t arg,
		    const sb_slim_start *s,
		    unsigned char *out,
		    size_t cap)
{
	unsigned char p[256] = {0};
	size_t n = 24;
	if (!strchr("spuqft", cmd) || !cmd || !out)
		return 0;
	p[0] = (unsigned char)cmd;
	p[1] = '0';
	p[2] = 'm';
	memset(p + 3, '?', 4);
	p[10] = '0';
	if (cmd == 's') {
		if (!s || !strchr("mfo", s->codec) || !s->codec || !s->port ||
		    s->port > 65535 || s->threshold_kib > 255 ||
		    s->output_threshold > 255 || s->token[64] != 0)
			return 0;
		for (size_t i = 0; i < 64; i++)
			if (!((s->token[i] >= '0' && s->token[i] <= '9') ||
			      (s->token[i] >= 'a' && s->token[i] <= 'f')))
				return 0;
		p[1] = '1';
		p[2] = (unsigned char)s->codec;
		p[7] = (unsigned char)s->threshold_kib;
		p[12] = (unsigned char)s->output_threshold;
		p[18] = (unsigned char)(s->port >> 8);
		p[19] = (unsigned char)s->port;
		p[20] = 127;
		p[23] = 1; /* explicit loopback, not arbitrary caller address */
		int written = snprintf((char *)p + 24,
				       sizeof p - 24,
				       "GET /music/stream/%s HTTP/1.0\r\nHost: "
				       "127.0.0.1\r\nConnection: close\r\n\r\n",
				       s->token);
		if (written < 0 || (size_t)written >= sizeof p - 24)
			return 0;
		n += (size_t)written;
	} else {
		if (s || ((cmd == 'q' || cmd == 'f') && arg))
			return 0;
		put32(p + 14, arg);
	}
	return sb_server_frame("strm", p, n, out, cap);
}

size_t sb_slim_audg(
	uint32_t l, uint32_t r, uint32_t seq, unsigned char *out, size_t cap)
{
	unsigned char p[22] = {0};
	if (l > 65536 || r > 65536 || !out)
		return 0;
	put32(p, l >> 9);
	put32(p + 4, r >> 9); /* old 8.8 + precise 16.16 */
	put32(p + 10, l);
	put32(p + 14, r);
	put32(p + 18, seq);
	return sb_server_frame("audg", p, sizeof p, out, cap);
}

size_t sb_slim_aude(unsigned enabled, unsigned char *out, size_t cap)
{
	unsigned char p[2];
	if (enabled > 1 || !out)
		return 0;
	p[0] = p[1] = (unsigned char)enabled;
	return sb_server_frame("aude", p, 2, out, cap);
}

size_t sb_slim_cont(unsigned char *out, size_t cap)
{
	const unsigned char p[7] = {0};
	return out ? sb_server_frame("cont", p, 7, out, cap) : 0;
}

static int id_equal(const sb_music_stream_id *a, const sb_music_stream_id *b)
{
	return a && b && a->epoch && a->epoch == b->epoch &&
	       a->owner_generation == b->owner_generation &&
	       a->connection_generation == b->connection_generation &&
	       a->media_generation == b->media_generation &&
	       a->track.track_id == b->track.track_id &&
	       sb_music_source_equal(&a->track.source, &b->track.source);
}

static void
terminal(sb_transport *t, sb_transport_error error, sb_music_event event)
{
	if (sb_music_stream_event(&t->stream, &t->stream.id, event))
		t->terminal_notifications++;
	t->error = error;
	t->state = error ? SB_T_ERROR : SB_T_STOPPED;
	t->pending = 0;
	t->deadline_ms = 0;
	t->revision++;
}

int sb_transport_init(sb_transport *t, int enabled)
{
	if (!t || !enabled)
		return 0;
	memset(t, 0, sizeof *t);
	t->enabled = 1;
	t->revision = 1;
	return 1;
}

int sb_transport_attach(sb_transport *t, uint64_t c, const sb_slim_hello *h)
{
	if (!t || !t->enabled || !h || !c || c <= t->last_connection ||
	    t->attached)
		return 0;
	t->hello = *h;
	t->connection = t->last_connection = c;
	t->attached = 1;
	t->state = SB_T_CONNECTED;
	t->quiescent = t->used_connection = t->have_jiffies = 0;
	t->seen_progress = 0;
	memset(t->last_status, 0, sizeof t->last_status);
	memset(&t->stream, 0, sizeof t->stream);
	t->error = SB_T_OK;
	t->deadline_ms = 0;
	t->pending = 0;
	t->revision++;
	return 1;
}

void sb_transport_detach(sb_transport *t, uint64_t c)
{
	if (!t || !t->enabled || !t->attached || c != t->connection)
		return;
	if (t->stream.id.epoch && !t->stream.terminal_seen)
		terminal(t, SB_T_CONNECTION_LOST, SB_EVENT_FAILED);
	t->attached = t->quiescent = 0;
	t->state = SB_T_DISCONNECTED;
	t->deadline_ms = 0;
	t->revision++;
}

int sb_transport_quiescent(sb_transport *t, uint64_t c, uint64_t owner)
{
	if (!t || !t->enabled || !t->attached || c != t->connection || !owner ||
	    t->used_connection)
		return 0;
	t->owner_generation = owner;
	t->quiescent = 1;
	t->state = SB_T_IDLE;
	t->revision++;
	return 1;
}

int sb_transport_prepare(sb_transport *t,
			 const sb_music_stream_id *id,
			 char codec,
			 uint64_t now)
{
	unsigned bit = codec == 'm'   ? 1
		       : codec == 'f' ? 2
		       : codec == 'o' ? 4
				      : 0;
	if (!t || !t->enabled || !id || !t->attached || !t->quiescent ||
	    t->used_connection || id->connection_generation != t->connection ||
	    id->owner_generation != t->owner_generation || !id->epoch ||
	    id->epoch <= t->last_epoch || !id->media_generation ||
	    !id->track.track_id ||
	    !sb_music_source_equal(&id->track.source, &id->track.source) ||
	    now > UINT64_MAX - 10000)
		return 0;
	if (!(bit & t->hello.codecs))
		return 0;
	memset(&t->stream, 0, sizeof t->stream);
	t->stream.id = *id;
	t->last_epoch = id->epoch;
	t->used_connection = 1;
	t->state = SB_T_PREPARING;
	t->deadline_ms = now + 10000;
	t->elapsed_ms = 0;
	t->http_status = t->input_reason = 0;
	t->have_jiffies = 0;
	t->revision++;
	return 1;
}

int sb_transport_command(sb_transport *t,
			 const sb_music_stream_id *id,
			 char cmd,
			 uint64_t now)
{
	if (!t || !t->enabled || !t->attached || !id_equal(id, &t->stream.id) ||
	    t->stream.terminal_seen || now > UINT64_MAX - 10000)
		return 0;
	if (cmd == 's' && t->state == SB_T_PREPARING)
		t->state = SB_T_CONNECTING_STREAM;
	else if (cmd == 'p' && t->state == SB_T_PLAYING && !t->pending)
		t->pending = 'p';
	else if (cmd == 'u' && t->state == SB_T_PAUSED && !t->pending)
		t->pending = 'u';
	else if ((cmd == 'q' || cmd == 'f') && t->state != SB_T_STOPPING) {
		t->state = SB_T_STOPPING;
		t->pending = cmd;
	} else
		return 0;
	t->deadline_ms = now + 10000;
	t->revision++;
	return 1;
}

int sb_transport_event(sb_transport *t,
		       uint64_t c,
		       const sb_music_stream_id *id,
		       const sb_slim_event *e,
		       uint64_t now)
{
	if (!t || !e || !t->enabled || !t->attached || c != t->connection ||
	    !id_equal(id, &t->stream.id) || t->stream.terminal_seen ||
	    t->state == SB_T_PREPARING)
		return 0;
	if (sb_transport_tick(t, now))
		return 0;
	if (e->kind == SB_SLIM_STAT) {
		if (t->have_jiffies && e->jiffies == t->last_jiffies &&
		    !memcmp(e->code, t->last_status, 4))
			return 0;
		if (t->have_jiffies &&
		    (uint32_t)(e->jiffies - t->last_jiffies) > 0x7fffffffU)
			return 0;
		/* Unknown status cannot advance ordering or transport. */
		if (memcmp(e->code, "STM", 3) ||
		    !strchr("cehlrpsdufnto", e->code[3]) || !e->code[3])
			return 0;
		t->last_jiffies = e->jiffies;
		t->have_jiffies = 1;
		memcpy(t->last_status, e->code, 4);
		if (t->state == SB_T_STOPPING) {
			/* STMf means input flush, not decoder/output stopped. A
			 * trusted quiescence acknowledgement is required below,
			 * not this packet. */
			if (e->code[3] == 'f') {
				t->revision++;
				return 1;
			}
			return 0;
		}
		/* Finite input progress events are idempotent for this one
		 * stream. */
		static const char progress_codes[] = "cehl";
		const char *progress = strchr(progress_codes, e->code[3]);
		if (progress) {
			unsigned bit = 1u
				       << (unsigned)(progress - progress_codes);
			if (t->seen_progress & bit)
				return 0;
			t->seen_progress |= (uint16_t)bit;
		}
		switch (e->code[3]) {
		case 'c':
		case 'e':
			sb_music_stream_event(
				&t->stream, id, SB_EVENT_CONNECTED);
			if (t->state == SB_T_CONNECTING_STREAM)
				t->state = SB_T_BUFFERING;
			break;
		case 'h':
		case 'l':
			if (t->state == SB_T_CONNECTING_STREAM)
				t->state = SB_T_BUFFERING;
			break;
		case 's':
			if (t->http_status != 200 || t->state == SB_T_PAUSED)
				return 0;
			if (!sb_music_stream_event(
				    &t->stream, id, SB_EVENT_AUDIBLE)) {
				/* WP4a.6 F2: rebuffer recovery. AUDIBLE is
				 * one-shot per stream, so a repeat STMs
				 * after a flush/underrun cannot re-fire it;
				 * accept it as transport recovery out of
				 * BUFFERING only. Duplicates in PLAYING
				 * keep rejecting below. */
				if (t->state != SB_T_BUFFERING ||
				    !(t->stream.seen_events &
				      (1u << SB_EVENT_AUDIBLE)))
					return 0;
			}
			t->state = SB_T_PLAYING;
			t->deadline_ms = 0;
			break;
		case 'p':
			if (t->pending != 'p')
				return 0;
			t->state = SB_T_PAUSED;
			t->pending = 0;
			t->deadline_ms = 0;
			break;
		case 'r':
			if (t->pending != 'u')
				return 0;
			t->state = SB_T_PLAYING;
			t->pending = 0;
			t->deadline_ms = 0;
			break;
		case 'd':
			if (!sb_music_stream_event(
				    &t->stream, id, SB_EVENT_DECODER_READY))
				return 0;
			if (now > UINT64_MAX - 30000) {
				terminal(t, SB_T_TIMEOUT, SB_EVENT_FAILED);
				return 1;
			}
			t->state = SB_T_DRAINING;
			/* Fab4 drains a full output buffer in ~10 s after
			 * STMd; allow 30 s base plus sliding proof-of-life
			 * below so a talking decoder never races the clock. */
			t->deadline_ms = now + 30000;
			break;
		case 'u':
			if (!(t->stream.seen_events &
			      (1u << SB_EVENT_DECODER_READY)))
				return 0;
			if (!(t->stream.seen_events & (1u << SB_EVENT_AUDIBLE)))
				terminal(t,
					 SB_T_PROTOCOL_ERROR,
					 SB_EVENT_FAILED);
			else
				terminal(t, SB_T_OK, SB_EVENT_DRAINED);
			return 1;
		case 'n':
			terminal(t, SB_T_CODEC_ERROR, SB_EVENT_FAILED);
			return 1;
		case 'f': /* flush handling depends on stream phase */
			/* strm-s itself flushes old input before connecting. */
			if (t->state == SB_T_CONNECTING_STREAM &&
			    !t->http_status && !t->stream.seen_events)
				break;
			/* WP4a.6 F2: input starvation while paused is
			 * benign. The decoder holds its output; resume
			 * re-buffers. Killing the stream here would strand
			 * a resumable pause. */
			if (t->state == SB_T_PAUSED)
				break;
			/* WP4a.6 F2: a mid-play flush is a bounded rebuffer
			 * window, not an instant protocol error. STMs
			 * recovers to PLAYING (clearing the window); STMd
			 * moves to DRAINING with its own window; silence
			 * times out via the tick. Drain-phase and
			 * pre-stream flushes still fail closed below. */
			if (t->state == SB_T_PLAYING ||
			    t->state == SB_T_BUFFERING) {
				if (now > UINT64_MAX - 15000) {
					terminal(t,
						 SB_T_TIMEOUT,
						 SB_EVENT_FAILED);
					return 1;
				}
				t->state = SB_T_BUFFERING;
				t->deadline_ms = now + 15000;
				break;
			}
			terminal(t, SB_T_PROTOCOL_ERROR, SB_EVENT_FAILED);
			return 1;
		case 'o': /* recoverable underrun, no automatic resume */
			t->state = SB_T_BUFFERING;
			break;
		case 't':
			/* Proof-of-life while draining: a decoder that keeps
			 * reporting time is still emptying its buffer, so
			 * slide the drain window. Silent elsewhere: PLAYING
			 * and PAUSED carry no deadline, and an already
			 * expired window must still time out via the tick. */
			if (t->state == SB_T_DRAINING &&
			    t->deadline_ms && now < t->deadline_ms &&
			    now <= UINT64_MAX - 10000)
				t->deadline_ms = now + 10000;
			break;
		default:
			return 0;
		}
		if ((t->stream.seen_events & (1u << SB_EVENT_AUDIBLE)) &&
		    e->elapsed_ms >= t->elapsed_ms)
			t->elapsed_ms = e->elapsed_ms;
	} else if (e->kind == SB_SLIM_RESP) {
		if (t->http_status || t->state == SB_T_STOPPING)
			return 0;
		t->http_status = e->http_status;
		if (e->http_status != 200) {
			terminal(t, SB_T_HTTP_ERROR, SB_EVENT_FAILED);
			return 1;
		}
		if (t->state == SB_T_CONNECTING_STREAM)
			t->state = SB_T_BUFFERING;
	} else if (e->kind == SB_SLIM_DSCO) {
		if (t->state == SB_T_STOPPING)
			return 0;
		t->input_reason = e->reason;
		if (e->reason) {
			terminal(t, SB_T_INPUT_ERROR, SB_EVENT_FAILED);
			return 1;
		}
		if (!sb_music_stream_event(&t->stream, id, SB_EVENT_INPUT_EOF))
			return 0;
	} else
		return 0;
	t->revision++;
	return 1;
}

int sb_transport_stopped(sb_transport *t,
			 const sb_music_stream_id *id,
			 int confirmed)
{
	if (!t || !t->enabled || !t->attached || t->state != SB_T_STOPPING ||
	    !confirmed || !id_equal(id, &t->stream.id) ||
	    t->stream.terminal_seen)
		return 0;
	terminal(t, SB_T_OK, SB_EVENT_STOPPED);
	return 1;
}

/* A terminal stream frees its SlimProto connection for a later stream.
 * The next stream re-proves quiescence, mints a fresh epoch and
 * re-activates its own resource; nothing here starts audio. The
 * terminal guard (id, state, terminal_seen) is preserved verbatim: late
 * firmware events must keep rejecting against the retired stream, and
 * the playback owner-change fence keys clean releases off it. Only the
 * connection pin and per-stream progress bits are retired. Jiffy
 * ordering survives: firmware jiffies are uptime-monotonic, so a new
 * stream never legitimately rewinds them. */
int sb_transport_release_stream(sb_transport *t)
{
	if (!t || !t->enabled || !t->attached || !t->used_connection ||
	    !t->stream.terminal_seen || (t->state != SB_T_STOPPED &&
					 t->state != SB_T_ERROR))
		return 0;
	t->used_connection = 0;
	t->quiescent = 0;
	t->owner_generation = 0;
	t->seen_progress = 0;
	t->revision++;
	return 1;
}

int sb_transport_tick(sb_transport *t, uint64_t now)
{
	if (!t || !t->enabled || !t->attached || !t->deadline_ms ||
	    now < t->deadline_ms || t->stream.terminal_seen)
		return 0;
	terminal(t, SB_T_TIMEOUT, SB_EVENT_FAILED);
	return 1;
}

static int owner_valid(sb_music_owner o)
{
	return o > SB_OWNER_NONE && o <= SB_OWNER_MUSIC;
}

static int advance(sb_physical_owner *o)
{
	if (o->value.generation == UINT64_MAX)
		return 0;
	o->value.generation++;
	o->revision++;
	return 1;
}

int sb_owner_init(sb_physical_owner *o, int enabled)
{
	if (!o || !enabled)
		return 0;
	memset(o, 0, sizeof *o);
	o->enabled = 1;
	o->value.generation = 1;
	o->revision = 1;
	return 1;
}

int sb_owner_register(sb_physical_owner *o,
		      sb_music_owner who,
		      uint64_t adapter)
{
	if (!o || !o->enabled || !owner_valid(who) || !adapter ||
	    o->registrations[who] ||
	    (o->value.current == who && o->value.yield != SB_YIELD_REVOKED))
		return 0;
	o->registrations[who] = adapter;
	o->revision++;
	return 1;
}

int sb_owner_connect(sb_physical_owner *o, uint64_t c, int idle)
{
	if (!o || !o->enabled || !c || c <= o->value.connection_generation ||
	    o->connected || !advance(o))
		return 0;
	o->connected = 1;
	o->value.connection_generation = c;
	o->value.requested = SB_OWNER_NONE;
	o->value.yield = SB_YIELD_REVOKED;
	o->value.physical_release_confirmed = 0;
	o->baseline_idle = !!idle && o->value.current == SB_OWNER_NONE;
	return 1;
}

void sb_owner_disconnect(sb_physical_owner *o, uint64_t c)
{
	if (!o || !o->enabled || !o->connected ||
	    c != o->value.connection_generation)
		return;
	o->connected = o->baseline_idle = 0;
	o->value.requested = SB_OWNER_NONE;
	o->value.yield = SB_YIELD_REVOKED;
	o->value.physical_release_confirmed = 0;
	(void)advance(o);
}

int sb_owner_request(sb_physical_owner *o,
		     sb_music_owner who,
		     uint64_t req,
		     uint64_t now,
		     unsigned timeout)
{
	if (!o || !o->enabled || !o->connected || o->suspended ||
	    !owner_valid(who) || !o->registrations[who] || !req ||
	    req <= o->last_request || !timeout || timeout > 10000 ||
	    now > UINT64_MAX - timeout ||
	    o->value.yield == SB_YIELD_REQUESTED ||
	    o->value.generation == UINT64_MAX || who == o->value.current)
		return 0;
	o->last_request = req;
	if (o->value.current == SB_OWNER_NONE) {
		if (!o->baseline_idle)
			return 0;
		o->value.current = who;
		o->value.requested = SB_OWNER_NONE;
		o->value.yield = SB_YIELD_IDLE;
		o->baseline_idle = 0;
		return advance(o);
	}
	if (!o->registrations[o->value.current] ||
	    o->value.yield == SB_YIELD_REVOKED)
		return 0;
	o->value.requested = who;
	o->value.request_id = req;
	o->value.deadline_ms = now + timeout;
	o->value.yield = SB_YIELD_REQUESTED;
	o->value.physical_release_confirmed = 0;
	o->revision++;
	return 2;
}

int sb_owner_ack(sb_physical_owner *o,
		 sb_music_owner prev,
		 uint64_t adapter,
		 uint64_t req,
		 uint64_t gen,
		 uint64_t conn,
		 uint64_t now,
		 int accepted,
		 int producer,
		 int hooks,
		 int decoder)
{
	if (!o || !o->enabled || !o->connected || !owner_valid(prev) ||
	    o->value.current != prev || !adapter ||
	    o->registrations[prev] != adapter ||
	    o->value.generation == UINT64_MAX)
		return 0;
	if (sb_owner_tick(o, now))
		return 0;
	accepted = accepted && producer && hooks && decoder;
	if (!sb_music_yield_ack(&o->value, req, gen, conn, now, accepted))
		return 0;
	o->revision++;
	if (!accepted) {
		o->value.requested = SB_OWNER_NONE;
		return 1;
	}
	if (!sb_music_can_commit_owner(&o->value, now))
		return 0;
	o->value.current = o->value.requested;
	o->value.requested = SB_OWNER_NONE;
	o->value.yield = SB_YIELD_IDLE;
	o->value.physical_release_confirmed = 0;
	return advance(o);
}

int sb_owner_release(sb_physical_owner *o,
		     sb_music_owner who,
		     uint64_t adapter,
		     uint64_t gen,
		     uint64_t conn,
		     int producer,
		     int hooks,
		     int decoder)
{
	if (!o || !o->enabled || !o->connected || !owner_valid(who) ||
	    o->value.current != who || !adapter ||
	    o->registrations[who] != adapter || gen != o->value.generation ||
	    conn != o->value.connection_generation || !producer || !hooks ||
	    !decoder || !advance(o))
		return 0;
	o->value.current = o->value.requested = SB_OWNER_NONE;
	o->value.yield = SB_YIELD_IDLE;
	o->value.physical_release_confirmed = 0;
	o->baseline_idle = 1;
	return 1;
}

int sb_owner_crashed(sb_physical_owner *o, sb_music_owner who, uint64_t adapter)
{
	if (!o || !o->enabled || !owner_valid(who) || !adapter ||
	    o->registrations[who] != adapter)
		return 0;
	o->registrations[who] = 0;
	if (o->value.current == who) {
		o->value.requested = SB_OWNER_NONE;
		o->value.yield = SB_YIELD_REVOKED;
		o->baseline_idle = 0;
		o->value.physical_release_confirmed = 0;
		(void)advance(o);
	} else if (o->value.requested == who) {
		o->value.requested = SB_OWNER_NONE;
		o->value.yield = SB_YIELD_REFUSED;
	}
	o->revision++;
	return 1;
}

int sb_owner_tick(sb_physical_owner *o, uint64_t now)
{
	if (!o || !o->enabled || o->value.yield != SB_YIELD_REQUESTED ||
	    now < o->value.deadline_ms)
		return 0;
	o->value.yield = SB_YIELD_TIMEOUT;
	o->value.requested = SB_OWNER_NONE;
	o->value.physical_release_confirmed = 0;
	o->revision++;
	return 1;
}

void sb_owner_suspend(sb_physical_owner *o, int hold)
{
	if (!o || !o->enabled)
		return;
	/* The service loop re-applies the alarm hold every tick; only an
	 * actual state transition may advance the revision. */
	hold = !!hold;
	int changed = o->suspended != hold;
	o->suspended = hold;
	if (hold && o->value.yield == SB_YIELD_REQUESTED) {
		o->value.yield = SB_YIELD_REFUSED;
		o->value.requested = SB_OWNER_NONE;
		changed = 1;
	}
	if (changed)
		o->revision++;
}

int sb_owner_controls(const sb_physical_owner *o,
		      sb_music_owner who,
		      uint64_t gen,
		      uint64_t conn)
{
	return o && o->enabled && o->connected && owner_valid(who) &&
	       o->registrations[who] && o->value.current == who &&
	       o->value.generation == gen &&
	       o->value.connection_generation == conn &&
	       o->value.yield == SB_YIELD_IDLE;
}

int sb_transport_owned_prepare(sb_transport *t,
			       const sb_physical_owner *o,
			       const sb_music_stream_id *id,
			       char codec,
			       uint64_t now)
{
	if (!id || !sb_owner_controls(o,
				      SB_OWNER_MUSIC,
				      id->owner_generation,
				      id->connection_generation))
		return 0;
	return sb_transport_prepare(t, id, codec, now);
}

int sb_transport_owned_command(sb_transport *t,
			       const sb_physical_owner *o,
			       const sb_music_stream_id *id,
			       char cmd,
			       uint64_t now)
{
	if (!id)
		return 0;
	int owns = sb_owner_controls(o,
				     SB_OWNER_MUSIC,
				     id->owner_generation,
				     id->connection_generation);
	/* Yield cleanup may stop the previous Music decoder, but cannot start
	 * it. */
	if (!owns && (cmd == 'q' || cmd == 'f') && o && o->enabled &&
	    o->connected && o->value.current == SB_OWNER_MUSIC &&
	    o->registrations[SB_OWNER_MUSIC] &&
	    o->value.generation == id->owner_generation &&
	    o->value.connection_generation == id->connection_generation &&
	    o->value.yield == SB_YIELD_REQUESTED)
		owns = 1;
	return owns ? sb_transport_command(t, id, cmd, now) : 0;
}
