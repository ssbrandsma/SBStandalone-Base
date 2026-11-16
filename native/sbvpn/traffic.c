/* Native, memory-only accounting for the owned TUN interface. */
#include "traffic.h"
#include "mongoose.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

static uint64_t add(uint64_t a, uint64_t b)
{
	return UINT64_MAX - a < b ? UINT64_MAX : a + b;
}

int vpn_traffic_counter(const char *s, uint64_t *value)
{
	if (!s || !value)
		return -1;
	while (*s == ' ' || *s == '\t')
		s++;
	if (!isdigit((unsigned char)*s))
		return -1;
	errno = 0;
	char *end;
	unsigned long long v = strtoull(s, &end, 10);
	if (errno == ERANGE)
		return -1;
	while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
		end++;
	if (*end)
		return -1;
	*value = (uint64_t)v;
	return 0;
}

static int valid_interface(const char *name)
{
	return name && *name && strlen(name) < 16 &&
	       strspn(name,
		      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123"
		      "456789_.-") == strlen(name) &&
	       strcmp(name, ".") && strcmp(name, "..");
}

int vpn_traffic_proc(const char *text, const char *name, VpnTrafficSample *out)
{
	if (!text || !out || !valid_interface(name))
		return -1;
	for (const char *line = text; *line;) {
		const char *end = strchr(line, '\n');
		if (!end)
			end = line + strlen(line);
		const char *p = line;
		while (p < end && isspace((unsigned char)*p))
			p++;
		const char *colon = memchr(p, ':', (size_t)(end - p));
		if (colon && (size_t)(colon - p) == strlen(name) &&
		    !memcmp(p, name, strlen(name))) {
			uint64_t fields[16];
			p = colon + 1;
			for (unsigned i = 0; i < 16; i++) {
				while (p < end && isspace((unsigned char)*p))
					p++;
				const char *start = p;
				while (p < end && isdigit((unsigned char)*p))
					p++;
				size_t n = (size_t)(p - start);
				if (!n || n > 20 ||
				    (p < end && !isspace((unsigned char)*p)))
					return -1;
				char number[21];
				memcpy(number, start, n);
				number[n] = 0;
				if (vpn_traffic_counter(number, &fields[i]))
					return -1;
			}
			while (p < end && isspace((unsigned char)*p))
				p++;
			if (p != end)
				return -1;
			memset(out, 0, sizeof *out);
			out->rx = fields[0];
			out->rx_packets = fields[1];
			out->tx = fields[8];
			out->tx_packets = fields[9];
			out->source = 2;
			return 0;
		}
		line = *end ? end + 1 : end;
	}
	return -1;
}

static int read_counter(const char *name, const char *field, uint64_t *out)
{
	char path[128], buffer[64];
	snprintf(path,
		 sizeof path,
		 "/sys/class/net/%s/statistics/%s",
		 name,
		 field);
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	size_t n = fread(buffer, 1, sizeof buffer - 1, f);
	int failed = ferror(f) || !feof(f);
	fclose(f);
	buffer[n] = 0;
	return failed ? -1 : vpn_traffic_counter(buffer, out);
}

int vpn_traffic_read(const char *name, int source, VpnTrafficSample *out)
{
	if (!valid_interface(name))
		return -1;
	unsigned before = if_nametoindex(name);
	if (!before)
		return -1;
	VpnTrafficSample sample;
	memset(&sample, 0, sizeof sample);
	int ok = -1;
	if (source != 2 && !read_counter(name, "rx_bytes", &sample.rx) &&
	    !read_counter(name, "tx_bytes", &sample.tx) &&
	    !read_counter(name, "rx_packets", &sample.rx_packets) &&
	    !read_counter(name, "tx_packets", &sample.tx_packets)) {
		sample.source = 1;
		ok = 0;
	}
	if (ok && source != 1) {
		FILE *f = fopen("/proc/net/dev", "r");
		if (!f)
			return -1;
		char text[16384];
		size_t n = fread(text, 1, sizeof text - 1, f);
		int failed = ferror(f) || !feof(f);
		fclose(f);
		text[n] = 0;
		if (!failed)
			ok = vpn_traffic_proc(text, name, &sample);
	}
	unsigned after = if_nametoindex(name);
	if (ok || before != after)
		return -1;
	sample.index = after;
	*out = sample;
	return 0;
}

int vpn_traffic_bits(void)
{
	struct utsname u;
	/* The measured 2.6.26 kernel exports net_device_stats unsigned longs.
	 * Modern Linux's sysfs/proc implementation exports 64-bit link stats.
	 */
	if (!uname(&u) && !strncmp(u.release, "2.6.26", sizeof "2.6.26" - 1))
		return (int)(sizeof(unsigned long) * CHAR_BIT);
	return 64;
}

void vpn_traffic_begin(VpnTraffic *t,
		       const char *profile,
		       const char *reason,
		       uint64_t now,
		       int64_t wall)
{
	unsigned session = t->session + 1;
	memset(t, 0, sizeof *t);
	t->session = session;
	t->started_ms = now;
	t->started_at = wall;
	snprintf(t->profile, sizeof t->profile, "%s", profile);
	snprintf(t->reason, sizeof t->reason, "%s", reason);
}

void vpn_traffic_attach(VpnTraffic *t, const char *name, int bits)
{
	snprintf(t->interface, sizeof t->interface, "%s", name);
	t->counter_bits = bits;
	t->running = 1;
	t->completed = 0;
	t->source = 0;
	t->have_previous = 0;
	t->rx_rate = t->tx_rate = 0;
	t->rate_ms = 0;
	t->rate_ready = 0;
}

void vpn_traffic_pause(VpnTraffic *t, int completed)
{
	t->running = 0;
	t->completed = completed;
	t->have_previous = 0;
	t->rate_ms = 0;
	t->rate_ready = 0;
	t->rx_rate = t->tx_rate = 0;
}

void vpn_traffic_missing(VpnTraffic *t)
{
	t->sample_failed = 1;
	t->rx_rate = t->tx_rate = 0;
	t->rate_ms = 0;
	t->rate_ready = 0;
}

static uint64_t
delta(uint64_t current, uint64_t old, int bits, int *reset, int *wrap)
{
	if (current >= old)
		return current - old;
	/* A small boundary crossing is a wrap; other decreases are resets. */
	if (bits == 32 && old >= UINT32_MAX - 67108864u && old <= UINT32_MAX &&
	    current < 67108864u) {
		*wrap = 1;
		return (uint64_t)UINT32_MAX - old + 1 + current;
	}
	if (bits == 64 && old >= UINT64_MAX - 67108864u &&
	    current < 67108864u) {
		*wrap = 1;
		return UINT64_MAX - old + 1 + current;
	}
	*reset = 1;
	return current;
}

static uint64_t rate(uint64_t bytes, uint64_t elapsed)
{
	if (!elapsed || elapsed > UINT64_MAX / 1000)
		return 0;
	if (bytes / elapsed > UINT64_MAX / 1000)
		return UINT64_MAX;
	return bytes / elapsed * 1000 + (bytes % elapsed) * 1000 / elapsed;
}

void vpn_traffic_update(VpnTraffic *t,
			const VpnTrafficSample *s,
			uint64_t now,
			int64_t wall)
{
	if (!t->running)
		return;
	if (t->source && s->source != t->source) {
		vpn_traffic_missing(t);
		return;
	}
	int fresh = !t->have_previous || s->index != t->previous.index,
	    reset = 0, wrap = 0;
	uint64_t dr = fresh ? s->rx
			    : delta(s->rx,
				    t->previous.rx,
				    t->counter_bits,
				    &reset,
				    &wrap);
	uint64_t dt = fresh ? s->tx
			    : delta(s->tx,
				    t->previous.tx,
				    t->counter_bits,
				    &reset,
				    &wrap);
	uint64_t dp = fresh ? s->rx_packets
			    : delta(s->rx_packets,
				    t->previous.rx_packets,
				    t->counter_bits,
				    &reset,
				    &wrap);
	uint64_t dq = fresh ? s->tx_packets
			    : delta(s->tx_packets,
				    t->previous.tx_packets,
				    t->counter_bits,
				    &reset,
				    &wrap);
	if (t->have_previous && fresh)
		t->resets++;
	if (reset)
		t->resets++;
	if (wrap)
		t->wraps++;
	t->rx = add(t->rx, dr);
	t->tx = add(t->tx, dt);
	t->rx_packets = add(t->rx_packets, dp);
	t->tx_packets = add(t->tx_packets, dq);
	if (fresh || reset || now < t->sampled_ms) {
		t->rx_rate = t->tx_rate = 0;
		t->rate_ms = now;
		t->rate_ready = 1;
		t->rate_rx = t->rx;
		t->rate_tx = t->tx;
	} else if (t->rate_ready && now > t->rate_ms &&
		   now - t->rate_ms >= 500) {
		t->rx_rate = rate(t->rx - t->rate_rx, now - t->rate_ms);
		t->tx_rate = rate(t->tx - t->rate_tx, now - t->rate_ms);
		/* Reject rate spikes after anomalous resets without fabricating
		 * bandwidth. */
		if (t->rx_rate > 1073741824u || t->tx_rate > 1073741824u)
			t->rx_rate = t->tx_rate = 0;
		t->rate_ms = now;
		t->rate_ready = 1;
		t->rate_rx = t->rx;
		t->rate_tx = t->tx;
	} else if (!t->rate_ready) {
		t->rate_ms = now;
		t->rate_ready = 1;
		t->rate_rx = t->rx;
		t->rate_tx = t->tx;
	}
	t->sample_failed = 0;
	t->source = s->source;
	t->previous = *s;
	t->sampled_ms = now;
	t->sampled_at = wall;
	t->has_sample = t->have_previous = 1;
}

char *vpn_traffic_json(const VpnTraffic *t, uint64_t now)
{
	uint64_t age = t->has_sample && now >= t->sampled_ms
			       ? (now - t->sampled_ms) / 1000
			       : 0;
	int available =
		t->has_sample && !t->sample_failed && (!t->running || age <= 6);
	return mg_mprintf(
		"{\"available\":%s,\"collecting\":%s,\"completed\":%s,"
		"\"session_id\":%u,\"profile_id\":%m,\"interface\":%m,"
		"\"source\":%m,\"counter_bits\":%d,\"rx_bytes\":%llu,\"tx_"
		"bytes\":%llu,\"total_bytes\":%llu,\"rx_bytes_per_second\":%"
		"llu,\"tx_bytes_per_second\":%llu,\"rx_packets\":%llu,\"tx_"
		"packets\":%llu,\"sampled_at\":%lld,\"sample_age_seconds\":%"
		"llu,\"session_started_at\":%lld,\"interval_reason\":%m,"
		"\"counter_resets\":%u,\"counter_wraps\":%u,\"accounting\":"
		"\"memory_only; new interval after manager restart\"}",
		available ? "true" : "false",
		t->running ? "true" : "false",
		t->completed ? "true" : "false",
		t->session,
		MG_ESC(t->profile),
		MG_ESC(t->interface),
		MG_ESC(t->source == 1	? "sysfs"
		       : t->source == 2 ? "proc_net_dev"
					: "unavailable"),
		t->counter_bits,
		(unsigned long long)t->rx,
		(unsigned long long)t->tx,
		(unsigned long long)add(t->rx, t->tx),
		(unsigned long long)(available && t->running ? t->rx_rate : 0),
		(unsigned long long)(available && t->running ? t->tx_rate : 0),
		(unsigned long long)t->rx_packets,
		(unsigned long long)t->tx_packets,
		(long long)t->sampled_at,
		(unsigned long long)age,
		(long long)t->started_at,
		MG_ESC(t->reason),
		t->resets,
		t->wraps);
}
