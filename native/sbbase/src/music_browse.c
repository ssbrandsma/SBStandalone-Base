#define _POSIX_C_SOURCE 200809L
#include "music_browse.h"
#include "nowplaying.h"
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sb_browse {
	sb_music_async *async;
	sb_music_media media;
	char player[64];
	int supported, ready, scan_active, stopped, source_dirty;
	unsigned source_attempts;
	uint64_t source_job, summary_job, next_summary, version, revision,
		media_version, retired[SB_ASYNC_SLOTS];
	uint32_t tracks, albums, artists;
	unsigned live_calls;
	unsigned playback_codecs;
	uint64_t control_context;
};

struct sb_browse_call {
	sb_music_query query;
	uint64_t job, deadline;
	uint32_t wanted, received, total;
	size_t used;
	int done, success, started, info, valid;
	unsigned playback_codecs;
	uint64_t control_context;
	char mode[16];
	char json[SB_BROWSE_JSON_MAX];
};

static int emit(char *out, size_t cap, size_t *used, const char *fmt, ...)
{
	if (*used >= cap)
		return 0;
	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(out + *used, cap - *used, fmt, args);
	va_end(args);
	if (n < 0 || (size_t)n >= cap - *used)
		return 0;
	*used += (size_t)n;
	return 1;
}

static void fail(sb_browse_call *c, const char *error)
{
	c->done = 1;
	c->success = 0;
	c->used = 0;
	emit(c->json,
	     sizeof c->json,
	     &c->used,
	     "{\"count\":0,\"offset\":%u,\"item_loop\":[],\"error\":\"%s\"}",
	     c->query.offset,
	     error);
}

static void retire(sb_browse *b, uint64_t id)
{
	if (!id)
		return;
	if (sb_music_async_cancel(b->async, id) != SB_ML_BUSY)
		return;
	for (unsigned i = 0; i < SB_ASYNC_SLOTS; i++)
		if (b->retired[i] == id)
			return;
	for (unsigned i = 0; i < SB_ASYNC_SLOTS; i++)
		if (!b->retired[i]) {
			b->retired[i] = id;
			return;
		}
}

static int integer(const char *s, uint64_t max, uint64_t *n)
{
	if (!s || !*s || (s[0] == '0' && s[1]))
		return 0;
	uint64_t v = 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9' ||
		    v > (max - (unsigned)(*s - '0')) / 10)
			return 0;
		v = v * 10 + (unsigned)(*s - '0');
	}
	if (v > max)
		return 0;
	*n = v;
	return 1;
}

int sb_browse_native(const sb_lms_command *c)
{
	return c && c->count &&
	       (!strcmp(c->arguments[0], "browselibrary") ||
		!strcmp(c->arguments[0], "info"));
}

sb_browse *sb_browse_start(sb_music_library *library,
			   const sb_music_media *media,
			   int enabled,
			   int supported)
{
	if (!enabled || !supported || !library || !media || !media->generation)
		return NULL;
	sb_browse *b = calloc(1, sizeof *b);
	if (!b)
		return NULL;
	b->async = sb_music_async_start(library, 1);
	if (!b->async) {
		free(b);
		return NULL;
	}
	b->supported = supported;
	b->media = *media;
	b->ready = !!media->online;
	b->version = 1;
	b->media_version = 1;
	return b;
}

int sb_browse_fd(const sb_browse *b)
{
	return b ? sb_music_async_fd(b->async) : -1;
}

void sb_browse_player(sb_browse *b, const char *p)
{
	if (b && p && strlen(p) < sizeof b->player)
		strcpy(b->player, p);
}

int sb_browse_media(sb_browse *b, const sb_music_media *media, int active)
{
	if (!b || !media || !media->generation ||
	    media->generation < b->media.generation ||
	    (!b->media.online && media->online &&
	     media->generation == b->media.generation))
		return 0;
	int changed =
		!sb_music_source_equal(&media->source, &b->media.source) ||
		media->generation != b->media.generation ||
		media->online != b->media.online;
	if (changed) {
		retire(b, b->source_job);
		retire(b, b->summary_job);
		b->source_job = b->summary_job = 0;
		b->ready = 0;
		b->media = *media;
		b->media_version++;
		b->version++;
		b->next_summary = 0;
		b->source_dirty = 1;
		b->source_attempts = 0;
	}
	if (b->scan_active && !active)
		b->next_summary = 0;
	b->scan_active = !!active;
	return 1;
}

void sb_browse_tick(sb_browse *b, uint64_t now)
{
	if (!b || b->stopped)
		return;
	sb_ml_result result;
	for (unsigned i = 0; i < SB_ASYNC_SLOTS; i++)
		if (b->retired[i] &&
		    sb_music_async_cancel(b->async, b->retired[i]) !=
			    SB_ML_BUSY)
			b->retired[i] = 0;
	if (b->source_dirty && !b->source_job && b->source_attempts < 3) {
		sb_ml_request r = {0};
		r.operation = SB_ML_SOURCE;
		r.data.media = b->media;
		if (sb_music_async_submit(
			    b->async, &r, now + 5000, &b->source_job) ==
		    SB_ML_OK) {
			b->source_dirty = 0;
			b->source_attempts++;
		}
	}
	if (b->source_job &&
	    sb_music_async_poll(b->async, b->source_job, &result) == SB_ML_OK) {
		b->source_job = 0;
		int ready = result.status == SB_ML_OK && b->media.online;
		if (ready != b->ready)
			b->version++;
		b->ready = ready;
	}
	if (b->summary_job &&
	    sb_music_async_poll(b->async, b->summary_job, &result) ==
		    SB_ML_OK) {
		b->summary_job = 0;
		if (result.status == SB_ML_OK && !b->scan_active && b->ready) {
			if (b->revision != result.revision) {
				b->revision = result.revision;
				b->version++;
			}
			b->tracks = result.data.summary.tracks;
			b->albums = result.data.summary.albums;
			b->artists = result.data.summary.artists;
		}
	}
	if (b->ready && !b->scan_active && !b->source_job && !b->summary_job &&
	    now >= b->next_summary) {
		sb_ml_request r = {0};
		r.operation = SB_ML_BROWSE_SUMMARY;
		r.data.query.media = b->media;
		r.data.query.limit = 1;
		if (sb_music_async_submit(
			    b->async, &r, now + 5000, &b->summary_job) ==
		    SB_ML_OK)
			b->next_summary = now + 1000;
	}
}

uint64_t sb_browse_version(const sb_browse *b)
{
	return b ? b->version : 0;
}

int sb_browse_available(const sb_browse *b)
{
	return b && !b->stopped && b->supported && b->ready && b->media.online;
}

static int parse(sb_browse *b, sb_browse_call *c, const sb_lms_command *cmd)
{
	if (cmd->player[0] && strcmp(cmd->player, b->player))
		return 0;
	if (!strcmp(cmd->arguments[0], "info")) {
		if (cmd->count != 3 && cmd->count != 4)
			return 0;
		if (strcmp(cmd->arguments[1], "total"))
			return 0;
		const char *type = cmd->arguments[2];
		c->info = !strcmp(type, "songs")     ? 1
			  : !strcmp(type, "albums")  ? 2
			  : !strcmp(type, "artists") ? 3
						     : 0;
		return c->info &&
		       (cmd->count == 3 || !strcmp(cmd->arguments[3], "?"));
	}
	uint64_t n;
	if (cmd->count < 4 || strcmp(cmd->arguments[1], "items") ||
	    !integer(cmd->arguments[2], UINT32_MAX - SB_BROWSE_WIRE_PAGE, &n))
		return 0;
	c->query.offset = (uint32_t)n;
	if (!integer(cmd->arguments[3], SB_BROWSE_WIRE_PAGE, &n))
		return 0;
	c->wanted = (uint32_t)n;
	const char *mode = sb_lms_tag(cmd, "mode");
	if (!mode || strlen(mode) >= sizeof c->mode)
		return 0;
	strcpy(c->mode, mode);
	c->query.kind = !strcmp(mode, "artists")  ? SB_QUERY_ARTISTS
			: !strcmp(mode, "albums") ? SB_QUERY_ALBUMS
			: !strcmp(mode, "tracks") ? SB_QUERY_TRACKS
			: !strcmp(mode, "genres") ? SB_QUERY_GENRES
			: !strcmp(mode, "bmf")	  ? SB_QUERY_FOLDERS
						  : SB_QUERY_PLAYLISTS;
	if (c->query.kind == SB_QUERY_PLAYLISTS)
		return 0;
	c->query.sort = c->query.kind == SB_QUERY_TRACKS ? SB_SORT_ALBUM_TRACK
							 : SB_SORT_NAME;
	for (unsigned i = 4; i < cmd->count; i++) {
		const char *tag = cmd->arguments[i], *v = strchr(tag, ':');
		if (!v)
			return 0;
		v++;
		if (!strncmp(tag, "mode:", 5))
			continue;
		if (!strncmp(tag, "menu:", 5)) {
			if (strcmp(v, "1"))
				return 0;
			continue;
		}
		/* Stock SlimBrowser appends this display preference to every
		 * parameterized browse action. It grants no playback actions. */
		if (!strncmp(tag, "useContextMenu:", 15)) {
			if (strcmp(v, "0") && strcmp(v, "1"))
				return 0;
			continue;
		}
		if (!strncmp(tag, "subscribe:", 10)) {
			if (!integer(v, 3600, &n))
				return 0;
			continue;
		}
		if (!integer(v, INT64_MAX, &n))
			return 0;
		if (!strncmp(tag, "artist_id:", 10))
			c->query.artist_id = n;
		else if (!strncmp(tag, "album_id:", 9))
			c->query.album_id = n;
		else if (!strncmp(tag, "genre_id:", 9))
			c->query.genre_id = n;
		else if (!strncmp(tag, "folder_id:", 10))
			c->query.folder_id = n;
		else if (!strncmp(tag, "library_revision:", 17))
			c->query.library_revision = n;
		else
			return 0;
	}
	if (c->query.kind == SB_QUERY_FOLDERS &&
	    (c->query.artist_id || c->query.genre_id || c->query.album_id))
		return 0;
	c->query.limit = c->wanted > 50 ? 50 : c->wanted ? c->wanted : 1;
	return 1;
}

sb_browse_call *
sb_browse_begin(sb_browse *b, const sb_lms_command *cmd, uint64_t deadline)
{
	if (!b || b->stopped || !cmd || !sb_browse_native(cmd) ||
	    b->live_calls >= 8)
		return NULL;
	sb_browse_call *c = calloc(1, sizeof *c);
	if (!c)
		return NULL;
	b->live_calls++;
	c->playback_codecs = b->playback_codecs;
	c->control_context = b->control_context;
	c->query.media = b->media;
	c->query.limit = 1;
	c->deadline = deadline;
	c->valid = parse(b, c, cmd);
	if (!c->valid)
		fail(c, "unsupported_or_invalid_browse");
	else if (!sb_browse_available(b))
		fail(c, "library_offline");
	return c;
}

static int playback_actions(sb_browse_call *c,
			    const sb_music_row *r,
			    const sb_ml_browse_info *info)
{
	if (!info->codec_mask || (info->codec_mask & ~c->playback_codecs) ||
	    (r->kind != SB_QUERY_TRACKS && r->kind != SB_QUERY_ALBUMS))
		return 1;
	const char *key = r->kind == SB_QUERY_TRACKS ? "track_id" : "album_id";
	if (!emit(c->json, sizeof c->json, &c->used, ",\"actions\":{"))
		return 0;
	static const char *names[] = {"go", "play", "add", "insert"};
	static const char *commands[] = {"load", "load", "add", "insert"};
	for (unsigned n = r->kind == SB_QUERY_TRACKS ? 0 : 1; n < 4; n++) {
		if (!emit(c->json,
			  sizeof c->json,
			  &c->used,
			  "%s\"%s\":{\"cmd\":[\"playlistcontrol\"],\"params\":{"
			  "\"cmd\":\"%s\",\"%s\":\"%" PRIu64
			  "\",\"queue_context\":\"%" PRIu64 "\"}%s}",
			  n == (r->kind == SB_QUERY_TRACKS ? 0u : 1u) ? ""
								      : ",",
			  names[n],
			  commands[n],
			  key,
			  r->id,
			  c->control_context,
			  n < 2 ? ",\"nextWindow\":\"nowPlaying\"" : ""))
			return 0;
	}
	return emit(c->json, sizeof c->json, &c->used, "}");
}

static int
row(sb_browse_call *c, const sb_music_row *r, const sb_ml_browse_info *info)
{
	char name[512], secondary[512], artist[512];
	np_json_escape(name, sizeof name, r->name);
	np_json_escape(secondary, sizeof secondary, r->secondary);
	np_json_escape(artist, sizeof artist, info->artist);
	if (!emit(c->json,
		  sizeof c->json,
		  &c->used,
		  "%s{\"id\":\"%" PRIu64 "\",\"text\":\"%s\",\"name2\":\"%s\"",
		  c->received ? "," : "",
		  r->id,
		  name,
		  secondary))
		return 0;
	if (r->kind == SB_QUERY_TRACKS) {
		int playable = info->codec_mask &&
			       !(info->codec_mask & ~c->playback_codecs);
		if (playable && !playback_actions(c, r, info))
			return 0;
		return emit(c->json,
			    sizeof c->json,
			    &c->used,
			    "%s,\"track_id\":\"%" PRIu64
			    "\",\"artist\":\"%s\",\"album\":\"%s\",\"disc\":%u,"
			    "\"tracknum\":%u,\"year\":%u,\"duration\":%.3f}",
			    playable ? ""
				     : ",\"action\":\"none\",\"style\":"
				       "\"itemNoAction\"",
			    r->id,
			    artist,
			    secondary,
			    info->disc,
			    info->track,
			    info->year,
			    r->duration_ms / 1000.0);
	}
	const char *target = r->kind == SB_QUERY_ARTISTS  ? "albums"
			     : r->kind == SB_QUERY_ALBUMS ? "tracks"
			     : r->kind == SB_QUERY_GENRES ? "artists"
							  : "bmf";
	const char *key = r->kind == SB_QUERY_ARTISTS  ? "artist_id"
			  : r->kind == SB_QUERY_ALBUMS ? "album_id"
			  : r->kind == SB_QUERY_GENRES ? "genre_id"
						       : "folder_id";
	/* Item action overrides base only for mixed folder/track pages. */
	if (r->kind == SB_QUERY_FOLDERS)
		return emit(c->json,
			    sizeof c->json,
			    &c->used,
			    ",\"actions\":{\"go\":{\"cmd\":[\"browselibrary\","
			    "\"items\"],\"params\":{\"menu\":1,\"mode\":"
			    "\"bmf\",\"folder_id\":\"%" PRIu64 "\"}}}}",
			    r->id);
	(void)target;
	if (!playback_actions(c, r, info))
		return 0;
	return emit(c->json,
		    sizeof c->json,
		    &c->used,
		    ",\"year\":%u,\"commonParams\":{\"%s\":\"%" PRIu64 "\"}}",
		    info->year,
		    key,
		    r->id);
}

static int header(sb_browse_call *c, uint64_t revision)
{
	const char *target = c->query.kind == SB_QUERY_ARTISTS	? "albums"
			     : c->query.kind == SB_QUERY_ALBUMS ? "tracks"
								: "artists";
	if (!emit(c->json,
		  sizeof c->json,
		  &c->used,
		  "{\"count\":%u,\"offset\":%u,\"playlist_timestamp\":%" PRIu64
		  ",\"library_revision\":%" PRIu64
		  ",\"media_generation\":%" PRIu64
		  ",\"window\":{\"text\":\"%s\"},\"base\":{\"actions\":{",
		  c->total,
		  c->query.offset,
		  revision,
		  revision,
		  c->query.media.generation,
		  c->query.kind == SB_QUERY_ARTISTS  ? "Artists"
		  : c->query.kind == SB_QUERY_ALBUMS ? "Albums"
		  : c->query.kind == SB_QUERY_TRACKS ? "Tracks"
		  : c->query.kind == SB_QUERY_GENRES ? "Genres"
						     : "Music Folder"))
		return 0;
	if (c->query.kind == SB_QUERY_ARTISTS ||
	    c->query.kind == SB_QUERY_ALBUMS ||
	    c->query.kind == SB_QUERY_GENRES) {
		if (!emit(c->json,
			  sizeof c->json,
			  &c->used,
			  "\"go\":{\"cmd\":[\"browselibrary\",\"items\"],"
			  "\"itemsParams\":\"commonParams\",\"params\":{"
			  "\"menu\":1,\"mode\":\"%s\"",
			  target))
			return 0;
		if (c->query.genre_id && !emit(c->json,
					       sizeof c->json,
					       &c->used,
					       ",\"genre_id\":\"%" PRIu64 "\"",
					       c->query.genre_id))
			return 0;
		if (c->query.artist_id &&
		    !emit(c->json,
			  sizeof c->json,
			  &c->used,
			  ",\"artist_id\":\"%" PRIu64 "\"",
			  c->query.artist_id))
			return 0;
		if (!emit(c->json, sizeof c->json, &c->used, "}}"))
			return 0;
	}
	return emit(c->json, sizeof c->json, &c->used, "}},\"item_loop\":[");
}

static int submit_page(sb_browse *b, sb_browse_call *c)
{
	if (np_monotonic_ms() >= c->deadline) {
		fail(c, "browse_timeout");
		return 1;
	}

	sb_ml_request request = {0};
	request.operation = c->info ? SB_ML_BROWSE_SUMMARY : SB_ML_BROWSE;
	request.data.query = c->query;
	request.data.query.offset += c->received;
	if (!c->info && c->received) {
		unsigned left = c->wanted - c->received;
		request.data.query.limit = left > 50 ? 50 : left;
	}
	sb_ml_status s =
		sb_music_async_submit(b->async, &request, c->deadline, &c->job);
	if (s == SB_ML_BUSY)
		return 0;
	if (s != SB_ML_OK) {
		fail(c, sb_music_library_status(s));
		return 1;
	}
	return 0;
}

int sb_browse_poll(sb_browse *b, sb_browse_call *c)
{
	if (!b || !c)
		return 1;
	if (c->done)
		return 1;
	uint64_t now = np_monotonic_ms();
	if (!sb_browse_available(b) ||
	    !sb_music_media_current(&c->query.media, &b->media)) {
		retire(b, c->job);
		c->job = 0;
		fail(c, "media_changed");
		return 1;
	}
	if (now >= c->deadline) {
		retire(b, c->job);
		c->job = 0;
		fail(c, "browse_timeout");
		return 1;
	}
	if (!c->job)
		return submit_page(b, c);

	sb_ml_result result;
	sb_ml_status s = sb_music_async_poll(b->async, c->job, &result);
	if (s == SB_ML_BUSY)
		return 0;
	c->job = 0;
	if (s != SB_ML_OK || result.status != SB_ML_OK) {
		fail(c,
		     s == SB_ML_OK ? sb_music_library_status(result.status)
				   : sb_music_library_status(s));
		return 1;
	}
	if (c->info) {
		unsigned n = c->info == 1   ? result.data.summary.tracks
			     : c->info == 2 ? result.data.summary.albums
					    : result.data.summary.artists;
		emit(c->json,
		     sizeof c->json,
		     &c->used,
		     "{\"_%s\":%u,\"library_revision\":%" PRIu64
		     ",\"online\":1}",
		     c->info == 1   ? "songs"
		     : c->info == 2 ? "albums"
				    : "artists",
		     n,
		     result.revision);
		c->success = c->done = 1;
		return 1;
	}
	if (!c->started) {
		c->total = result.data.query.page.total;
		c->query.library_revision = result.revision;
		c->started = 1;
		if (!header(c, result.revision)) {
			fail(c, "response_too_large");
			return 1;
		}
	}
	if (result.revision != c->query.library_revision) {
		fail(c, "stale");
		return 1;
	}
	unsigned count = result.data.query.page.count;
	for (unsigned i = 0; i < count && c->received < c->wanted; i++) {
		if (!row(c,
			 &result.data.query.rows[i],
			 &result.data.query.info[i])) {
			fail(c, "response_too_large");
			return 1;
		}
		c->received++;
	}
	if (c->received >= c->wanted ||
	    c->query.offset + c->received >= c->total || count == 0) {
		if (!emit(c->json, sizeof c->json, &c->used, "]}")) {
			fail(c, "response_too_large");
			return 1;
		}
		c->success = c->done = 1;
		return 1;
	}
	/* Queue the next native page before returning to poll: otherwise a
	 * consumed completion could leave no wakeup until the next timer tick.
	 */
	return submit_page(b, c);
}

const char *sb_browse_json(const sb_browse_call *c)
{
	return c ? c->json : "{\"error\":\"browse_busy\"}";
}

int sb_browse_valid(const sb_browse_call *c)
{
	return c && c->valid;
}

int sb_browse_success(const sb_browse_call *c)
{
	return c && c->success;
}

void sb_browse_cancel(sb_browse *b, sb_browse_call *c)
{
	if (c) {
		if (b) {
			retire(b, c->job);
			if (b->live_calls)
				b->live_calls--;
		}
		free(c);
	}
}

int sb_browse_nodes(
	sb_browse *b, int remove, char *out, size_t capacity, unsigned *count)
{
	static const char *ids[] = {"myMusicArtists",
				    "myMusicAlbums",
				    "myMusicGenres",
				    "myMusicMusicFolder", "myMusicTracks"};
	static const char *labels[] = {
		"Artists", "Albums", "Genres", "Music Folder", "Tracks"};
	static const char *modes[] = {"artists", "albums", "genres", "bmf", "tracks"};
	static const unsigned weights[] = {10, 20, 30, 70, 40};
	size_t used = 0;
	*count = 0;
	out[0] = 0;
	if (!b || (!remove && !sb_browse_available(b)))
		return 1;
	for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
		if (remove) {
			if (!emit(out,
				  capacity,
				  &used,
				  "%s{\"id\":\"%s\",\"node\":\"myMusic\"}",
				  i ? "," : "",
				  ids[i]))
				return 0;
		} else if (!emit(out,
				 capacity,
				 &used,
				 "%s{\"id\":\"%s\",\"node\":\"myMusic\","
				 "\"text\":\"%s\",\"weight\":%u,\"actions\":{"
				 "\"go\":{\"cmd\":[\"browselibrary\",\"items\"]"
				 ",\"params\":{\"menu\":1,\"mode\":\"%s\"}}}}",
				 i ? "," : "",
				 ids[i],
				 labels[i],
				 weights[i],
				 modes[i]))
			return 0;
		(*count)++;
	}
	return 1;
}

sb_ml_status sb_browse_stop(sb_browse *b)
{
	if (!b)
		return SB_ML_INVALID;
	b->stopped = 1;
	return sb_music_async_stop(b->async);
}

sb_ml_status sb_browse_destroy(sb_browse *b)
{
	if (!b || b->live_calls)
		return SB_ML_BUSY;
	sb_ml_status s = sb_music_async_destroy(b->async);
	if (s == SB_ML_OK)
		free(b);
	return s;
}

uint64_t sb_browse_media_version(const sb_browse *b)
{
	return b ? b->media_version : 0;
}

unsigned sb_browse_pending_peak(sb_browse *b)
{
	return b ? sb_music_async_peak(b->async) : 0;
}

sb_music_async *sb_browse_dispatcher(sb_browse *b)
{
	return b ? b->async : NULL;
}

void sb_browse_playback(sb_browse *b, unsigned codecs)
{
	if (b && b->playback_codecs != (codecs & 3u)) {
		b->playback_codecs = codecs & 3u;
		b->version++;
	}
}

void sb_browse_control_context(sb_browse *b, uint64_t context)
{
	if (b && b->control_context != context) {
		b->control_context = context;
		b->version++;
	}
}
