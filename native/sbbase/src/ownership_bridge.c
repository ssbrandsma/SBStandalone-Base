#define _POSIX_C_SOURCE 200809L
#include "ownership_bridge.h"
#include "../../common/music_resource_ipc.h"
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int number(const char *s, uint64_t *out)
{
	uint64_t n = 0;
	if (!*s || (*s == '0' && s[1]))
		return 0;
	for (; *s; s++) {
		unsigned v = (unsigned)(*s - '0');
		if (v > 9 || n > (UINT64_MAX - v) / 10)
			return 0;
		n = n * 10 + v;
	}
	*out = n;
	return 1;
}

int sb_ob_open(sb_ownership_bridge *b, sb_physical_owner *o, const char *path)
{
	char parent[512];
	unsigned char random[32];
	struct stat st;
	int fd = -1, dir = -1;
	if (!b || !o || !o->enabled || !path || strlen(path) >= sizeof parent)
		return 0;
	memset(b, 0, sizeof *b);
	b->token_dir = -1;
	b->owner = o;
	strcpy(parent, path);
	char *leaf = strrchr(parent, '/');
	if (!leaf || leaf == parent || !leaf[1] ||
	    strlen(leaf + 1) >= sizeof b->token_name)
		return 0;
	strcpy(b->token_name, leaf + 1);
	*leaf = 0;
	if (!strcmp(b->token_name, ".") || !strcmp(b->token_name, ".."))
		return 0;
	dir = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (dir < 0 || fstat(dir, &st) || st.st_uid != geteuid() ||
	    (st.st_mode & 0777) != 0700)
		goto fail;
	if (!sb_music_resource_entropy(random))
		goto fail;
	for (unsigned i = 0; i < 32; i++)
		snprintf(b->token + i * 2, 3, "%02x", random[i]);
	fd = openat(dir,
		    b->token_name,
		    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
		    0600);
	if (fd < 0)
		goto fail;
	if (fstat(fd, &st))
		goto file_fail;
	b->token_dev = (uint64_t)st.st_dev;
	b->token_inode = (uint64_t)st.st_ino;
	if (write(fd, b->token, 64) != 64)
		goto file_fail;
	close(fd);
	b->token_dir = dir;
	b->enabled = 1;
	return 1;
file_fail:
	close(fd);
	unlinkat(dir, b->token_name, 0);
fail:
	if (dir >= 0)
		close(dir);
	memset(b->token, 0, sizeof b->token);
	return 0;
}

void sb_ob_bind_music(sb_ownership_bridge *b,
		      sb_ob_music_proof proof,
		      int (*begin)(void *, uint64_t),
		      int (*ready)(void *),
		      void *ctx)
{
	if (b) {
		b->music_proof = proof;
		b->music_begin = begin;
		b->music_ready = ready;
		b->music_context = ctx;
	}
}

void sb_ob_close(sb_ownership_bridge *b)
{
	if (!b)
		return;
	if (b->enabled) {
		for (unsigned i = 1; i <= 4; i++)
			if (b->adapters[i].incarnation)
				sb_owner_crashed(b->owner,
						 (sb_music_owner)i,
						 b->adapters[i].incarnation);
		struct stat st;
		if (!fstatat(b->token_dir,
			     b->token_name,
			     &st,
			     AT_SYMLINK_NOFOLLOW) &&
		    (uint64_t)st.st_dev == b->token_dev &&
		    (uint64_t)st.st_ino == b->token_inode)
			unlinkat(b->token_dir, b->token_name, 0);
		close(b->token_dir);
	}
	memset(b, 0, sizeof *b);
	b->token_dir = -1;
}

int sb_ob_auth(const sb_ownership_bridge *b, const char *token, size_t n)
{
	if (!b || !b->enabled || !token || n != 64)
		return 0;
	unsigned diff = 0;
	for (unsigned i = 0; i < 64; i++)
		diff |= (unsigned char)b->token[i] ^ (unsigned char)token[i];
	return diff == 0;
}

int sb_ob_snapshot(sb_ownership_bridge *b,
		   unsigned adapter,
		   uint64_t now,
		   char *out,
		   size_t cap)
{
	if (!b || !b->enabled || adapter > 4 || !out)
		return 0;
	sb_physical_owner *o = b->owner;
	sb_music_ownership *v = &o->value;
	int n = snprintf(
		out,
		cap,
		"{\"version\":1,\"incarnation\":\"%" PRIu64
		"\",\"revision\":\"%" PRIu64 "\","
		"\"owner\":%u,\"requested\":%u,\"generation\":\"%" PRIu64
		"\",\"connection\":\"%" PRIu64 "\","
		"\"sequence\":\"%" PRIu64 "\",\"request\":\"%" PRIu64
		"\",\"remaining_ms\":%u,\"yield\":%u,\"connected\":%s,"
		"\"controls\":%s,\"suspended\":%s,\"music_ready\":%s,"
		"\"unavailable_mask\":%u,\"startup_native_absent_mask\":%u,"
		"\"idle_only\":%s,\"idle_admitted\":%s}",
		b->adapters[adapter].incarnation,
		o->revision,
		(unsigned)v->current,
		(unsigned)v->requested,
		v->generation,
		v->connection_generation,
		b->adapters[adapter].sequence,
		v->request_id,
		v->yield == SB_YIELD_REQUESTED && v->deadline_ms > now
			? (unsigned)(v->deadline_ms - now)
			: 0,
		(unsigned)v->yield,
		o->connected ? "true" : "false",
		adapter && sb_owner_controls(o,
					     (sb_music_owner)adapter,
					     v->generation,
					     v->connection_generation)
			? "true"
			: "false",
		o->suspended ? "true" : "false",
		b->music_ready && b->music_ready(b->music_context) ? "true"
								   : "false",
		b->unavailable_mask,
		b->startup_native_absent_mask,
		b->idle_only ? "true" : "false", b->idle_admitted ? "true" : "false");
	return n > 0 && (size_t)n < cap;
}

void sb_ob_tick(sb_ownership_bridge *b, uint64_t now)
{
	if (!b || !b->enabled)
		return;
	sb_owner_tick(b->owner, now);
	int ready = b->music_ready && b->music_ready(b->music_context);
	if (ready != b->last_music_ready) {
		b->last_music_ready = ready;
		b->owner->revision++;
	}
	for (unsigned i = 1; i <= 4; i++) {
		sb_ob_adapter *a = &b->adapters[i];
		if (a->incarnation && now >= a->seen_ms &&
		    now - a->seen_ms >= 45000) {
			uint64_t gone = a->incarnation;
			if (i == SB_OWNER_MUSIC && b->music_begin &&
			    b->owner->value.current == SB_OWNER_MUSIC)
				(void)b->music_begin(b->music_context, now);
			sb_owner_crashed(
				b->owner, (sb_music_owner)i, gone);
			a->incarnation = 0;
			b->idle_mask &= ~(1u << i);
			if (i == SB_OWNER_MUSIC)
				b->idle_admitted = 0;
			fprintf(stderr,
				"MUSIC_OWNERSHIP adapter=%u incarnation=%llu timed-out\n",
				i, (unsigned long long)gone);
		}
	}
}

void sb_ob_alarm(sb_ownership_bridge *b, int hold)
{
	if (!b || !b->enabled)
		return;
	b->alarm_hold = !!hold;
	int wanted = b->alarm_hold || b->admission_holds;
	if (b->owner->suspended != wanted)
		sb_owner_suspend(b->owner, wanted);
}

void sb_ob_connection(sb_ownership_bridge *b, uint64_t c, int attached)
{
	if (!b || !b->enabled)
		return;
	if (attached && b->observed_connection == c && b->owner->connected &&
	    b->owner->value.connection_generation == c)
		return;
	if (attached) {
		b->observed_connection = c;
		b->reconnect_pending = b->owner->value.current != SB_OWNER_NONE;
	}
	b->idle_mask = 0;
	b->idle_admitted = 0;
	if (attached && b->owner->connected &&
	    b->owner->value.connection_generation == c)
		return;
	if (attached) {
		if (b->owner->connected)
			sb_owner_disconnect(
				b->owner,
				b->owner->value.connection_generation);
		sb_owner_connect(b->owner, c, 0);
	} else
		sb_owner_disconnect(b->owner, c);
}

int sb_ob_command(sb_ownership_bridge *b,
		  const char *input,
		  size_t size,
		  uint64_t now,
		  char *out,
		  size_t cap)
{
	char text[SB_OB_MESSAGE + 1], original[SB_OB_MESSAGE + 1], *part[12];
	uint64_t n[10];
	if (!b || !b->enabled)
		return 404;
	if (!input || !size || size > SB_OB_MESSAGE || memchr(input, 0, size))
		return 400;
	memcpy(text, input, size);
	text[size] = 0;
	memcpy(original, text, size + 1);
	unsigned count = 0;
	part[count++] = text;
	for (size_t i = 0; i < size; i++) {
		if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126)
			return 400;
		if (text[i] == ' ') {
			if (!i || text[i - 1] == 0 || i + 1 == size ||
			    count == 12)
				return 400;
			text[i] = 0;
			part[count++] = text + i + 1;
		}
	}
	if (count != 12 || strcmp(part[0], "SBOWN1"))
		return 400;
	for (unsigned i = 0; i < 10; i++)
		if (!number(part[i + 2], &n[i]))
			return 400;
	/* owner incarnation sequence generation connection request timeout
	 * flags revision reserved */
	unsigned who = (unsigned)n[0];
	if (n[0] < 1 || n[0] > 4 || !n[2] || n[6] > 10000 || n[7] > 7 || n[9])
		return 400;
	const char *op = part[1];
	int hello = !strcmp(op, "hello"), query = !strcmp(op, "query");
	if (!hello && !query && strcmp(op, "register") &&
	    strcmp(op, "request") && strcmp(op, "ack") &&
	    strcmp(op, "release") && strcmp(op, "idle") &&
	    strcmp(op, "cancel") && strcmp(op, "fault") &&
	    strcmp(op, "reconnect") && strcmp(op, "cleanup") &&
	    strcmp(op, "suspend") && strcmp(op, "admit-idle"))
		return 400;
	if (!query && n[8])
		return 400;
	if (query && (n[5] || n[6] || n[7]))
		return 400;
	if (hello && (n[1] || n[3] || n[4] || n[5] || n[6] || n[7]))
		return 400;
	if (!hello && !query && strcmp(op, "request") && n[6])
		return 400;
	if ((!strcmp(op, "cleanup") || !strcmp(op, "fault") ||
	     !strcmp(op, "cancel")) &&
	    n[7])
		return 400;
	sb_ob_adapter *a = &b->adapters[who];
	sb_physical_owner *o = b->owner;
	sb_ob_tick(b, now);
	if (!strcmp(part[1], "hello") && !n[1]) {
		return sb_ob_snapshot(b, who, now, out, cap) ? 200 : 500;
	}
	if (!strcmp(part[1], "query") && a->incarnation &&
	    n[1] == a->incarnation) {
		a->seen_ms = now;
		if (!sb_ob_snapshot(b, who, now, out, cap))
			return 500;
		return n[8] == o->revision ? 202 : 200;
	}
	if (n[2] == a->sequence && !strcmp(original, a->command)) {
		if (!a->incarnation)
			return 409;
		return sb_ob_snapshot(b, who, now, out, cap) ? a->status : 500;
	}
	if (n[2] <= a->sequence)
		return 409;
	int result = 0;
	if (!strcmp(op, "register")) {
		if (b->idle_only && who != SB_OWNER_MUSIC)
			return 409;
		if (n[1] || n[3] || n[4] || n[5] || n[6] || n[7] || n[8] ||
		    b->next_incarnation == UINT64_MAX)
			return 400;
		if (a->incarnation)
			return 409;
		uint64_t incarnation = ++b->next_incarnation;
		if (!sb_owner_register(o, (sb_music_owner)who, incarnation))
			return 409;
		a->incarnation = incarnation;
		result = 1;
	} else {
		if (!a->incarnation || n[1] != a->incarnation)
			return 409;
		if (n[3] != o->value.generation ||
		    n[4] != o->value.connection_generation)
			return 409;
		else if (!strcmp(op, "cleanup")) {
			result = who == SB_OWNER_MUSIC &&
				 o->value.current == SB_OWNER_MUSIC &&
				 b->music_begin &&
				 b->music_begin(b->music_context, now);
		} else if (!strcmp(op, "admit-idle")) {
			/* Authenticated Music binding attests real application fencing,
			 * exact hooks and independent firmware observations. This is
			 * never an ACK on behalf of unregistered external applications. */
			const unsigned external = (1u << SB_OWNER_RADIO) |
				(1u << SB_OWNER_SPOTIFY) | (1u << SB_OWNER_QOBUZ);
			if (!b->idle_only || who != SB_OWNER_MUSIC || n[5] || n[7] != 7 ||
			    !o->connected || o->suspended || b->admission_holds || b->alarm_hold ||
			    o->value.current != SB_OWNER_NONE || o->value.requested != SB_OWNER_NONE ||
			    b->reconnect_pending || b->idle_admitted ||
			    b->unavailable_mask != external ||
			    b->startup_native_absent_mask != ((1u<<SB_OWNER_SPOTIFY)|(1u<<SB_OWNER_QOBUZ)) ||
			    o->registrations[SB_OWNER_RADIO] || o->registrations[SB_OWNER_SPOTIFY] ||
			    o->registrations[SB_OWNER_QOBUZ] || !b->music_ready ||
			    !b->music_ready(b->music_context))
				return 409;
			result = b->music_proof && b->music_proof(b->music_context,
				a->incarnation, n[4], n[3], now);
			if (result) {
				o->baseline_idle = 1;
				b->idle_admitted = 1;
				o->revision++;
			}
		} else if (!strcmp(op, "suspend")) {
			if (n[7] > 1)
				return 400;
			if (n[7])
				b->admission_holds |= 1u << who;
			else
				b->admission_holds &= ~(1u << who);
			sb_ob_alarm(b, b->alarm_hold);
			result = 1;
		} else if (!strcmp(op, "request")) {
			if (b->unavailable_mask & (1u << who))
				return 409;
			if (n[5] || n[7] || !n[6] ||
			    o->last_request == UINT64_MAX)
				return 400;
			result = sb_owner_request(o,
						  (sb_music_owner)who,
						  o->last_request + 1,
						  now,
						  (unsigned)n[6]);
		} else if (!strcmp(op, "ack")) {
			if (who == SB_OWNER_MUSIC && n[7] == 7) {
				if (o->value.yield != SB_YIELD_REQUESTED ||
				    n[5] != o->value.request_id)
					return 409;
				result = b->music_proof &&
					 b->music_proof(b->music_context,
							a->incarnation,
							n[4],
							n[3],
							now);
			} else
				result = sb_owner_ack(o,
						      (sb_music_owner)who,
						      a->incarnation,
						      n[5],
						      n[3],
						      n[4],
						      now,
						      n[7] == 7,
						      n[7] & 1,
						      n[7] & 2,
						      n[7] & 4);
		} else if (!strcmp(op, "release")) {
			if (o->value.yield == SB_YIELD_REQUESTED)
				return 409;
			if (who == SB_OWNER_MUSIC) {
				result = n[7] == 7 && b->music_proof &&
					 b->music_proof(b->music_context,
							a->incarnation,
							n[4],
							n[3],
							now);
			} else
				result = sb_owner_release(o,
							  (sb_music_owner)who,
							  a->incarnation,
							  n[3],
							  n[4],
							  n[7] & 1,
							  n[7] & 2,
							  n[7] & 4);
		} else if (!strcmp(op, "idle")) {
			if (o->connected && !o->suspended &&
			    o->value.current == SB_OWNER_NONE && n[7] == 7 &&
			    (who != SB_OWNER_MUSIC ||
			     (b->music_proof && b->music_proof(b->music_context,
							       a->incarnation,
							       n[4],
							       n[3],
							       now)))) {
				b->idle_mask |= 1u << who;
				o->baseline_idle = b->idle_mask == 30;
				o->revision++;
				result = 1;
			}
		} else if (!strcmp(op, "cancel")) {
			if (o->value.requested == (sb_music_owner)who &&
			    o->value.request_id == n[5] &&
			    o->value.yield == SB_YIELD_REQUESTED) {
				o->value.requested = SB_OWNER_NONE;
				o->value.yield = SB_YIELD_REFUSED;
				o->revision++;
				result = 1;
			}
		} else if (!strcmp(op, "fault")) {
			if (who == SB_OWNER_MUSIC && o->value.current == SB_OWNER_MUSIC && b->music_begin)
				(void)b->music_begin(b->music_context, now);
			result = sb_owner_crashed(
				o, (sb_music_owner)who, a->incarnation);
			a->incarnation = 0;
			b->idle_mask &= ~(1u << who);
			if (who == SB_OWNER_MUSIC)
				b->idle_admitted = 0;
		} else if (!strcmp(op, "reconnect")) {
			if (!b->reconnect_pending || !o->connected ||
			    o->value.yield != SB_YIELD_REVOKED ||
			    o->value.current != (sb_music_owner)who ||
			    n[7] != 7)
				return 409;
			if (who == SB_OWNER_MUSIC)
				result = b->music_proof &&
					 b->music_proof(b->music_context,
							a->incarnation,
							n[4],
							n[3],
							now);
			else
				result = sb_owner_release(o,
							  (sb_music_owner)who,
							  a->incarnation,
							  n[3],
							  n[4],
							  1,
							  1,
							  1);
			if (result)
				b->reconnect_pending = 0;
		} else
			return 400;
	}
	if (!result)
		return 409;
	a->seen_ms = now;
	a->sequence = n[2];
	strcpy(a->command, original);
	a->status = 200;
	if (!sb_ob_snapshot(b, who, now, a->reply, sizeof a->reply) ||
	    strlen(a->reply) >= cap)
		return 500;
	strcpy(out, a->reply);
	return 200;
}
