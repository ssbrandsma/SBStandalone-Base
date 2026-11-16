#define _POSIX_C_SOURCE 200809L
#include "music_playback.h"
#include "../../common/music_http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <unistd.h>
#include <poll.h>

struct sb_music_playback {
	sb_music_async *async;
	sb_music_queue *queue;
	sb_music_load *load;
	sb_music_file *file;
	sb_physical_owner *owner;
	np_manager *np;
	sb_music_media media;
	sb_music_media local_media;
	sb_music_source_id network_source;
	sb_music_network_track network;
	int remote_capable;
	sb_transport transport;
	sb_music_stream_id stream;
	sb_music_send send;
	void *send_context;
	int ipc;
	unsigned port;
	unsigned selection_count;
	uint64_t operation, epoch, sequence, file_request, deadline,
		observed_owner;
	uint64_t context, detail_job;
	uint64_t retired_file, yield_request;
	uint64_t completed;
	uint64_t terminal_handled;
	int completed_success;
	sb_ml_track candidate;
	sb_resource_message message;
	int ipc_pending, ipc_sent, reset, stopping, staged, owner_requested,
		existing;
	int candidate_registered, revoke_candidate;
	int resource_active;
	int single_mp3, attempted;
	sb_queue_mode stop_previous_mode;
	int ipc_probe_required, ipc_verified;
	uint64_t ipc_probe_deadline, ipc_sent_ms;
	uint64_t keepalive_ms;
	uint64_t active_expires;
	sb_play_phase phase;
	sb_queue_edit edit;
	char error[64];
};

static void transport_status(sb_music_playback *, uint64_t now);

static void retire_file(sb_music_playback *p)
{
	if (p->file_request)
		p->retired_file = p->file_request;
	p->file_request = 0;
	if (p->retired_file &&
	    sb_music_file_cancel(p->file, p->retired_file) != SB_ML_BUSY)
		p->retired_file = 0;
}

static void fail(sb_music_playback *p, const char *error)
{
	fprintf(stderr, "MUSIC_PLAYBACK_FAIL error=%s phase=%d op=%llu ctx=%llu\n",
		error, (int)p->phase, (unsigned long long)p->operation,
		(unsigned long long)p->context);
	if (p->owner_requested && p->owner->value.yield == SB_YIELD_REQUESTED &&
	    p->owner->value.requested == SB_OWNER_MUSIC) {
		p->owner->value.requested = SB_OWNER_NONE;
		p->owner->value.yield = SB_YIELD_REFUSED;
		p->owner->value.physical_release_confirmed = 0;
		p->owner->revision++;
	}
	p->owner_requested = 0;
	snprintf(p->error, sizeof p->error, "%s", error);
	p->phase = SB_PLAY_FAILED;
	/* The single-track latch covers one live stream, not the process:
	 * a failure without a live stream must not brick later loads. */
	if (!(p->transport.used_connection &&
	      !p->transport.stream.terminal_seen))
		p->attempted = 0;
	if (p->completed < p->operation) {
		p->completed = p->operation;
		p->completed_success = 0;
	}
	if (p->candidate_registered)
		p->revoke_candidate = 1;
	(void)sb_music_load_cancel(p->load);
	retire_file(p);
	if (p->detail_job &&
	    sb_music_async_cancel(p->async, p->detail_job) != SB_ML_BUSY)
		p->detail_job = 0;
	if (!p->ipc_sent && p->ipc_pending) {
		if (p->message.fd >= 0)
			close(p->message.fd);
		p->message.fd = -1;
		p->ipc_pending = 0;
	}
	sb_queue_abort(p->queue, p->operation);
	p->staged = 0;
	p->existing = 0;
}

static int issue(sb_music_playback *p, unsigned operation)
{
	if (p->ipc_pending)
		return 0;
	if (p->sequence == UINT64_MAX) {
		if (p->ipc >= 0)
			close(p->ipc);
		p->ipc = -1; /* peer loss revokes capabilities if no RESET can
				be sent */
		fail(p, "resource_sequence_exhausted");
		return 0;
	}
	p->message.operation = operation;
	p->message.sequence = ++p->sequence;
	p->ipc_pending = 1;
	p->ipc_sent = 0;
	return 1;
}

static void publish(sb_music_playback *p)
{
	int owns = sb_owner_controls(p->owner,
				     SB_OWNER_MUSIC,
				     p->owner->value.generation,
				     p->transport.connection);
	if (!owns) {
		np_native_music(p->np, NULL, 0);
		return;
	}
	sb_queue_status q = sb_queue_snapshot(p->queue);
	np_status status = {0};
	status.state =
		q.mode == SB_Q_PLAYING	? NP_PLAYING
		: q.mode == SB_Q_PAUSED ? NP_PAUSED
		: q.mode == SB_Q_BUFFERING || q.mode == SB_Q_WAIT_CONNECTION
			? NP_BUFFERING
		: q.mode == SB_Q_ERROR ? NP_ERROR
				       : NP_STOPPED;
	if(p->transport.state==SB_T_STOPPING&&!p->transport.stream.terminal_seen)status.state=NP_BUFFERING;
	status.position_ms = q.elapsed_ms;
	status.playlist_revision = q.revision;
	status.capabilities.play = status.capabilities.pause =
		status.capabilities.stop = 1;
	status.capabilities.next = status.capabilities.previous = q.count > 1;
	const sb_queue_item *item = sb_queue_at(p->queue, q.audible);
	if (item) {
		status.duration_known = item->duration_ms != 0;
		status.duration_ms = item->duration_ms;
		snprintf(status.track.id,
			 sizeof status.track.id,
			 "%s:%" PRIu64,
			 item->identity.track.kind==SB_TRACK_NETWORK?"network":"music",
			 item->identity.track.kind==SB_TRACK_NETWORK?item->identity.track.network_id:item->identity.track.track_id);
		snprintf(status.track.title,
			 sizeof status.track.title,
			 "%s",
			 item->title);
		snprintf(status.track.artist,
			 sizeof status.track.artist,
			 "%s",
			 item->artist);
		snprintf(status.track.album,
			 sizeof status.track.album,
			 "%s",
			 item->album);
		strcpy(status.track.audio.codec,
		       item->codec == 'm' ? "MP3" : "FLAC");
		snprintf(status.track.artwork,sizeof status.track.artwork,"%s",item->artwork);
		snprintf(status.track.genre,sizeof status.track.genre,"%s",item->genre);
		status.track.track_number=item->track_number;
		status.track.audio.lossless = item->codec == 'm' ? 1 : 2;
	}
	(void)np_native_music(p->np, &status, 1);
}

sb_music_playback *sb_playback_create(int enabled,
				      sb_music_async *async,
				      const sb_storage_root *root,
				      sb_physical_owner *owner,
				      np_manager *np,
				      int ipc,
				      unsigned port,
				      sb_music_send send,
				      void *send_context)
{
	if (!enabled || (!!async!=!!root) || !owner || !owner->enabled || !np ||
	    ipc < 0 || !port || port > 65535 || !send)
		return NULL;
	if (!sb_resource_adopt(ipc))
		return NULL;
	unsigned char incarnation[SB_MUSIC_TOKEN_BYTES];
	if (!sb_music_resource_entropy(incarnation))
		return NULL;
	uint64_t context = 0;
	for (unsigned n = 0; n < 8; n++)
		context = (context << 8) | incarnation[n];
	context = (context & INT64_MAX) | 1u;
	sb_music_playback *p = calloc(1, sizeof *p);
	if (!p)
		return NULL;
	p->message.fd = -1;
	p->queue = sb_queue_create(1);
	if (!p->queue) {
		free(p);
		return NULL;
	}
	if(async)p->load = sb_music_load_create(async, p->queue);
	if (async && !p->load) {
		sb_queue_destroy(p->queue);
		free(p);
		return NULL;
	}
	p->file = sb_music_file_start(1, root);
	if (!p->file) {
		sb_music_load_destroy(p->load);
		sb_queue_destroy(p->queue);
		free(p);
		return NULL;
	}
	p->async = async;
	p->owner = owner;
	p->np = np;
	p->ipc = ipc;
	p->port = port;
	p->send = send;
	p->send_context = send_context;
	if(root)p->media=p->local_media = root->media;
	else p->media.online=1;
	memcpy(p->network_source.bytes,incarnation,16);
	p->context = context;
	p->observed_owner = owner->value.current;
	sb_queue_context(p->queue, p->context, owner->value.generation);
	sb_transport_init(&p->transport, 1);
	return p;
}

static void network_candidate(sb_music_playback *p,const sb_queue_item *item)
{
 memset(&p->candidate,0,sizeof p->candidate);p->candidate.identity=item->identity.track;
 p->candidate.media_generation=item->network.generation;p->candidate.duration_ms=item->duration_ms;
 strcpy(p->candidate.codec,"mp3");snprintf(p->candidate.title,sizeof p->candidate.title,"%s",item->title);
 snprintf(p->candidate.album,sizeof p->candidate.album,"%s",item->album);snprintf(p->candidate.artists[0],sizeof p->candidate.artists[0],"%s",item->artist);p->candidate.artist_count=*item->artist?1:0;
 p->network=item->network;p->media=(sb_music_media){item->identity.track.source,item->network.generation,1};
}
int sb_playback_network_pending(const sb_music_playback *p,const char *source)
{return p&&p->completed<p->operation&&p->candidate.identity.kind==SB_TRACK_NETWORK&&(!source||!strcmp(p->network.server,source));}
sb_queue_result sb_playback_load_network(sb_music_playback *p,sb_queue_edit edit,const sb_queue_item *items,unsigned count,uint64_t context,uint64_t now)
{
 if(!p||!items||!count||count>SB_QUEUE_LIMIT||context!=p->context||p->stopping||!p->remote_capable||p->owner->suspended)return SB_Q_UNAVAILABLE;
 if(p->ipc_pending||p->reset||p->retired_file||p->detail_job||(p->phase!=SB_PLAY_IDLE&&p->phase!=SB_PLAY_ACTIVE&&p->phase!=SB_PLAY_FAILED))return SB_Q_BUSY;
 if(p->owner->value.current!=SB_OWNER_NONE&&p->owner->value.current!=SB_OWNER_MUSIC)return SB_Q_BUSY;
 if(p->operation==UINT64_MAX)return SB_Q_FULL;
 sb_queue_status snapshot=sb_queue_snapshot(p->queue);uint64_t op=++p->operation;
 sb_queue_result result=sb_queue_begin(p->queue,edit,op,snapshot.revision,context);if(result!=SB_Q_OK)return result;
 sb_queue_item first={0};
 for(unsigned i=0;i<count;i++){
  sb_queue_item item=items[i];item.identity.track.kind=SB_TRACK_NETWORK;item.identity.track.track_id=0;
  item.identity.track.source=p->network_source;item.identity.track.network_id=op*SB_QUEUE_LIMIT+i;
  if(op>UINT64_MAX/SB_QUEUE_LIMIT){sb_queue_abort(p->queue,op);return SB_Q_FULL;}
  item.identity.media_generation=item.network.generation;
  if((result=sb_queue_stage(p->queue,op,&item,1))!=SB_Q_OK){sb_queue_abort(p->queue,op);return result;}
  if(!i)first=item;
 }
 p->selection_count=count;p->error[0]=0;p->edit=edit;p->owner_requested=0;
 if(edit!=SB_Q_LOAD){result=sb_queue_commit(p->queue,op);p->completed=op;p->completed_success=result==SB_Q_OK;return result;}
 network_candidate(p,&first);p->file_request=0;p->staged=1;p->existing=0;p->phase=SB_PLAY_FILE;p->deadline=now+20000;return SB_Q_OK;
}

sb_music_queue *sb_playback_queue(sb_music_playback *p)
{
	return p ? p->queue : NULL;
}

sb_play_phase sb_playback_phase(const sb_music_playback *p)
{
	return p ? p->phase : SB_PLAY_FAILED;
}

const char *sb_playback_error(const sb_music_playback *p)
{
	return p ? p->error : "disabled";
}

const sb_music_stream_id *sb_playback_stream(const sb_music_playback *p)
{
	return p ? &p->stream : NULL;
}

int sb_playback_poll_fd(const sb_music_playback *p, unsigned which)
{
	return !p	    ? -1
	       : which == 0 ? p->ipc
	       : which == 1 ? sb_music_file_fd(p->file)
			    : -1;
}

short sb_playback_ipc_events(const sb_music_playback *p)
{
	return p ? (short)(POLLIN |
			   (p->ipc_pending && !p->ipc_sent ? POLLOUT : 0))
		 : 0;
}

uint64_t sb_playback_operation(const sb_music_playback *p)
{
	return p ? p->operation : 0;
}

unsigned sb_playback_selection_count(const sb_music_playback *p)
{
	return p ? p->selection_count : 0;
}

int sb_playback_result(const sb_music_playback *p,
		       uint64_t op,
		       uint64_t context)
{
	if (!p || !op || context != p->context || op != p->operation)
		return -1;
	return p->completed == op ? (p->completed_success ? 1 : -1) : 0;
}

void sb_playback_cancel(sb_music_playback *p, uint64_t op, uint64_t context)
{
	if (p && op == p->operation && context == p->context &&
	    p->completed < op) {
		if (p->owner_requested &&
		    p->owner->value.yield == SB_YIELD_REQUESTED &&
		    p->owner->value.requested == SB_OWNER_MUSIC) {
			p->owner->value.requested = SB_OWNER_NONE;
			p->owner->value.yield = SB_YIELD_REFUSED;
			p->owner->value.physical_release_confirmed = 0;
			p->owner->revision++;
		}
		fail(p, "action_cancelled");
	}
}

sb_queue_result sb_playback_load(sb_music_playback *p,
				 sb_queue_edit edit,
				 uint64_t id,
				 int album,
				 uint64_t context,
				 uint64_t now)
{
	/* WP4a.6 switch: one live stream, replaceable by an explicit
	 * tap. LOAD is accepted mid-play; the superseded stream is
	 * retired in prepare_file after the candidate validates, so a
	 * rejected selection leaves the playing stream untouched. */
	if (p && p->single_mp3 && (edit != SB_Q_LOAD || album))
		return SB_Q_INVALID;
	if (!p || p->stopping || !p->media.online || context != p->context ||
	    now > UINT64_MAX - 20000)
		return SB_Q_STALE;
	if(!p->load||!p->local_media.online)return SB_Q_UNAVAILABLE;
	p->media=p->local_media;
	if (p->ipc_pending || p->reset || p->retired_file || p->detail_job ||
	    (p->phase != SB_PLAY_IDLE && p->phase != SB_PLAY_ACTIVE &&
	     p->phase != SB_PLAY_FAILED))
		return SB_Q_BUSY;
	if (p->operation == UINT64_MAX)
		return SB_Q_FULL;
	/* A new native selection is not permission to stop another owner. */
	sb_queue_result result = sb_music_load_begin(p->load,
						     &p->media,
						     edit,
						     ++p->operation,
						     context,
						     id,
						     album,
						     now);
	if (result != SB_Q_OK)
		return result;
	p->edit = edit;
	if (p->single_mp3) p->attempted = 1;
	p->phase = SB_PLAY_LOADING;
	p->deadline = now + 20000;
	p->error[0] = 0;
	p->selection_count = 0;
	p->staged = 1;
	p->owner_requested = p->existing = 0;
	return SB_Q_OK;
}

int sb_playback_attach(sb_music_playback *p,
		       uint64_t connection,
		       const sb_slim_hello *hello)
{
	if (!p || !sb_transport_attach(&p->transport, connection, hello))
		return 0;
	if (!sb_owner_connect(p->owner, connection, 0)) {
		sb_transport_detach(&p->transport, connection);
		return 0;
	}
	fprintf(stderr, "MUSIC_SLIM attached conn=%llu codecs=%u reconnect=%d model=%.31s fw=%.63s\n",
		(unsigned long long)connection, hello->codecs,
		!!hello->reconnect, hello->model, hello->firmware);
	return 1;
}

void sb_playback_detach(sb_music_playback *p, uint64_t connection)
{
	if (!p || connection != p->transport.connection)
		return;
	int expected = p->phase == SB_PLAY_NEED_CONNECTION;
	p->reset = 1;
	if (expected)
		p->terminal_handled = p->transport.stream.id.epoch;
	sb_transport_detach(&p->transport, connection);
	if (!expected)
		(void)sb_queue_confirm(p->queue,
				       p->context,
				       p->stream.owner_generation,
				       p->stream.epoch,
				       SB_Q_ERROR,
				       p->transport.elapsed_ms);
	sb_owner_disconnect(p->owner, connection);
	if (!expected)
		fail(p, "player_disconnected");
	else
		p->reset = 1;
	/* An already reserved stream cannot survive RESET; the candidate is
	 * reopened on a new connection rather than reusing its capability. */
	retire_file(p);
	np_native_music(p->np, NULL, 0);
}

int sb_playback_idle_proof(sb_music_playback *p,
			   uint64_t adapter,
			   uint64_t connection,
			   uint64_t generation,
			   uint64_t now,
			   int producer,
			   int hooks,
			   int decoder)
{
	if (!p || (p->ipc_probe_required && !p->ipc_verified) ||
	    !producer || !hooks || !decoder || !adapter ||
	    adapter != p->owner->registrations[SB_OWNER_MUSIC] ||
	    !p->owner->connected ||
	    connection != p->owner->value.connection_generation ||
	    generation != p->owner->value.generation)
		return 0;
	if (p->resource_active || p->reset ||
	    (p->ipc_pending && (p->message.operation == SB_RESOURCE_RESET ||
				p->message.operation == SB_RESOURCE_ACTIVATE)))
		return 0;
	if (p->owner->value.current == SB_OWNER_MUSIC) {
		(void)sb_transport_stopped(&p->transport, &p->stream, 1);
		if (p->owner->value.yield == SB_YIELD_REQUESTED)
			return sb_owner_ack(p->owner,
					    SB_OWNER_MUSIC,
					    adapter,
					    p->owner->value.request_id,
					    generation,
					    connection,
					    now,
					    1,
					    producer,
					    hooks,
					    decoder);
		return sb_owner_release(p->owner,
					SB_OWNER_MUSIC,
					adapter,
					generation,
					connection,
					producer,
					hooks,
					decoder);
	}
	if (p->owner->value.current != SB_OWNER_NONE)
		return 0;
	p->owner->baseline_idle = 1;
	return 1;
}

static int control(sb_music_playback *p, char command, uint64_t now)
{
	if (!sb_transport_owned_command(
		    &p->transport, p->owner, &p->stream, command, now))
		return 0;
	unsigned char frame[64];
	size_t length = sb_slim_strm(command, 0, NULL, frame, sizeof frame);
	if (!length || !p->send(p->send_context, frame, length, &p->stream)) {
		fail(p, "transport_output_failed");
		return 0;
	}
	fprintf(stderr, "MUSIC_STRM_SEND cmd=%c epoch=%llu owner=%llu conn=%llu\n",
		command, (unsigned long long)p->stream.epoch,
		(unsigned long long)p->stream.owner_generation,
		(unsigned long long)p->transport.connection);
	return 1;
}

int sb_playback_control(sb_music_playback *p,
			char command,
			uint64_t context,
			uint64_t now)
{
	if (!p || context != p->context || !strchr("puq", command) || !command)
		return 0;
	if ((command == 'q'||command=='p') && p->completed < p->operation){
		sb_playback_cancel(p, p->operation, context);
		if(!p->transport.used_connection||p->transport.stream.terminal_seen){p->reset=1;p->existing=0;return 1;}
	}
	if(command=='q'){
	 p->stop_previous_mode=sb_queue_snapshot(p->queue).mode;p->existing=0;
	 if(p->phase==SB_PLAY_NEED_CONNECTION&&(!p->transport.used_connection||p->transport.stream.terminal_seen)){p->phase=SB_PLAY_IDLE;p->reset=1;return 1;}
	}
	if (!control(p, command, now))
		return 0;
	if (command == 'q') {
		p->reset = 1;
		(void)sb_queue_confirm(p->queue,
				       p->context,
				       p->stream.owner_generation,
				       p->stream.epoch,
				       SB_Q_STOPPED,
				       0);
	}
	return 1;
}

/* Retire the superseded stream for an in-flight LOAD. The stop goes
 * out on the validated path, the slot is freed synchronously, and
 * the late firmware terminal is pre-absorbed so the tick path cannot
 * clobber the new load. The superseding strm-s makes the old
 * terminal moot: firmware serializes strm frames on one decoder, and
 * stale old-stream events keep rejecting against the retired
 * identity. Raw control() (not sb_playback_control) serves here: the
 * latter would cancel the new operation this retires for. */
static int retire_previous_stream(sb_music_playback *p, uint64_t now)
{
	if (!p->transport.used_connection ||
	    p->transport.stream.terminal_seen)
		return 1;
	if (p->transport.state != SB_T_STOPPING && !control(p, 'q', now))
		return 0;
	p->reset = 1;
	(void)sb_queue_confirm(p->queue,
			       p->context,
			       p->stream.owner_generation,
			       p->stream.epoch,
			       SB_Q_STOPPED,
			       0);
	if (!sb_transport_stopped(&p->transport, &p->stream, 1))
		return 0;
	p->terminal_handled = p->stream.epoch;
	(void)sb_transport_release_stream(&p->transport);
	p->attempted = 0;
	return 1;
}

int sb_playback_select(sb_music_playback *p,
		       uint32_t index,
		       uint64_t context,
		       uint64_t now)
{
	if (!p || p->single_mp3 || context != p->context ||
	    (p->phase != SB_PLAY_ACTIVE && p->phase != SB_PLAY_FAILED &&
	     p->phase != SB_PLAY_IDLE) ||
	    (!sb_owner_controls(p->owner,
				SB_OWNER_MUSIC,
				p->owner->value.generation,
				p->transport.connection) &&
	     !(p->owner->value.current == SB_OWNER_NONE &&
	       p->owner->baseline_idle)))
		return 0;
	sb_queue_status q = sb_queue_snapshot(p->queue);
	if (sb_queue_select(p->queue, index, q.revision, context) != SB_Q_OK)
		return 0;
	if (p->transport.used_connection &&
	    !p->transport.stream.terminal_seen &&
	    p->transport.state != SB_T_STOPPING && !control(p, 'q', now))
		return 0;
	p->phase = SB_PLAY_NEED_CONNECTION;
	p->deadline = now + 20000;
	p->existing = 1;
	p->reset = 1;
	return 1;
}

static void prepare_file(sb_music_playback *p, uint64_t now)
{
	if (p->retired_file)
		return;
	char codec = !strcmp(p->candidate.codec, "mp3")	   ? 'm'
		     : !strcmp(p->candidate.codec, "flac") ? 'f'
							   : 0;
	unsigned bit = codec == 'm'   ? SB_SLIM_CODEC_MP3
		       : codec == 'f' ? SB_SLIM_CODEC_FLAC
				      : 0;
	if (!bit || !(p->transport.hello.codecs & bit) ||
	    (p->single_mp3 && bit != SB_SLIM_CODEC_MP3)) {
		fail(p, "unsupported_player_codec");
		return;
	}
	/* A valid candidate supersedes the live stream. The RESET that
	 * revokes the old capability must reach the registry before the
	 * new RESERVE, so defer the file submit one round trip. */
	if (p->transport.used_connection &&
	    !p->transport.stream.terminal_seen) {
		if (!retire_previous_stream(p, now)) {
			fail(p, "previous_stream_stop_failed");
			return;
		}
		return;
	}
	if (!p->file_request) {
		if (p->epoch == UINT64_MAX) {
			fail(p, "stream_epoch_exhausted");
			return;
		}
		uint64_t ttl = p->candidate.duration_ms
				       ? p->candidate.duration_ms
				       : UINT64_C(14400000);
		if (ttl > 86400000 - 60000)
			ttl = 86400000 - 60000;
		if (now > UINT64_MAX - ttl - 60000) {
			fail(p, "resource_expiry_overflow");
			return;
		}
		uint64_t request = p->epoch + 1;
		sb_ml_status s;
		if(p->candidate.identity.kind==SB_TRACK_NETWORK)
		 s=sb_music_file_submit_network(p->file,request,&p->candidate,&p->network,p->owner->value.generation,request,now+ttl+60000);
		else s=sb_music_file_submit(p->file,
					     request,
					     &p->candidate,
					     p->owner->value.generation,
					     request,
					     now + ttl + 60000);
		if (s == SB_ML_BUSY)
			return;
		if (s != SB_ML_OK) {
			fail(p, "file_open_rejected");
			return;
		}
		p->file_request = p->epoch = request;
	}
	sb_resource_message message;
	sb_ml_status got =
		sb_music_file_result(p->file, p->file_request, &message);
	if (got == SB_ML_BUSY)
		return;
	if (got != SB_ML_OK) {
		fail(p, "indexed_file_unavailable");
		return;
	}
	p->file_request = 0; /* worker result consumed; epoch retains identity */
	p->message = message;
	if (!issue(p, message.resource.streaming?SB_RESOURCE_STREAM_RESERVE:SB_RESOURCE_RESERVE)) {
		close(message.fd);
		p->message.fd = -1;
		fail(p, "resource_busy");
		return;
	}
	p->candidate_registered = 1;
	p->phase = SB_PLAY_RESERVING;
}

static int ipc_tick(sb_music_playback *p)
{
	if (!p->ipc_pending)
		return 0;
	if (!p->ipc_sent) {
		int sent = sb_resource_send(p->ipc, &p->message);
		if (!sent)
			return 0;
		if (sent < 0) {
			fail(p, "resource_ipc_closed");
			return -1;
		}
		p->ipc_sent = 1;
		p->ipc_sent_ms = np_monotonic_ms();
	}
	sb_resource_message ack;
	int got = sb_resource_receive(p->ipc, &ack);
	if (!got)
		return 0;
	if (got < 0 || ack.operation != SB_RESOURCE_ACK ||
	    ack.sequence != p->message.sequence) {
		if (got > 0 && ack.fd >= 0)
			close(ack.fd);
		fail(p, "invalid_resource_ack");
		return -1;
	}
	unsigned operation = p->message.operation;
	if(operation==SB_RESOURCE_RESET)p->remote_capable=!!(ack.codec&SB_RESOURCE_CAP_STREAM);
	p->ipc_pending = p->ipc_sent = 0;
	if (p->message.fd >= 0)
		close(p->message.fd);
	p->message.fd = -1;
	if (ack.status) {
		fail(p, "resource_registration_failed");
		return -1;
	}
	return (int)operation;
}

void sb_playback_tick(sb_music_playback *p, uint64_t now)
{
	if (!p || p->stopping)
		return;
	/* A FAILED phase with nothing outstanding is stale (e.g. an idle
	 * player disconnect with no load in flight). Return to IDLE so a
	 * fresh attach can re-qualify; the last error is retained and the
	 * single-shot attempt flag is untouched. Anything pending, reserved,
	 * requested or staged keeps the failure latched, as does an
	 * unverified startup probe (an unpaired base must fail closed). */
	if (p->phase == SB_PLAY_FAILED && p->media.online &&
	    !(p->ipc_probe_required && !p->ipc_verified) &&
	    p->completed >= p->operation &&
	    !p->ipc_pending && !p->resource_active &&
	    !p->candidate_registered && !p->revoke_candidate && !p->reset &&
	    !p->file_request && !p->retired_file && !p->detail_job &&
	    !p->staged && !p->existing && !p->owner_requested) {
		p->phase = SB_PLAY_IDLE;
		fprintf(stderr, "MUSIC_PLAYBACK_RECOVERED op=%llu ctx=%llu error=%s\n",
			(unsigned long long)p->operation,
			(unsigned long long)p->context, p->error);
	}
	if (p->ipc_probe_required && !p->ipc_verified && now >= p->ipc_probe_deadline) {
		fail(p, "resource_startup_timeout");
		return;
	}
	if (p->retired_file &&
	    sb_music_file_cancel(p->file, p->retired_file) != SB_ML_BUSY)
		p->retired_file = 0;
	if (p->phase == SB_PLAY_FAILED && p->detail_job &&
	    sb_music_async_cancel(p->async, p->detail_job) != SB_ML_BUSY)
		p->detail_job = 0;
	sb_owner_tick(p->owner, now);
	sb_transport_tick(&p->transport, now);
	if (p->transport.stream.terminal_seen)
		transport_status(p, now);
	/* A committed external takeover invalidates every old request
	 * context. Expected releases (natural completion, clean stop, idle
	 * resync) change the owner with no live stream and no open
	 * operation: they stay transparent to already-rendered UI, whose
	 * next tap otherwise dies on a stale context with no feedback.
	 */
	int native_reconnect_release =
		p->existing && p->owner->value.current == SB_OWNER_NONE &&
		p->owner->baseline_idle;
	int native_acquisition =
		p->owner_requested && p->owner->value.current == SB_OWNER_MUSIC;
	if (p->observed_owner != p->owner->value.current &&
	    !native_reconnect_release && !native_acquisition) {
		/* Takeovers of a live stream or an open load rotate the
		 * fenced context and fail loudly. */
		if (((p->owner->value.current != SB_OWNER_NONE &&
		      p->owner->value.current != SB_OWNER_MUSIC) ||
		     p->completed < p->operation ||
		     !p->transport.stream.terminal_seen) &&
		    p->operation) {
			if (p->context == UINT64_MAX) {
				fail(p, "context_exhausted");
				return;
			}
			sb_queue_context(p->queue,
					 ++p->context,
					 p->owner->value.generation);
			fail(p, "physical_owner_changed");
		}
	}
	p->observed_owner = p->owner->value.current;
	int acknowledged = ipc_tick(p);
	if (acknowledged == SB_RESOURCE_RESET) {
		p->ipc_verified = 1;
		p->resource_active = p->candidate_registered =
			p->revoke_candidate = 0;
	}
	if (p->resource_active && now >= p->active_expires) {
		(void)control(p, 'q', now);
		p->reset = 1;
		(void)sb_queue_confirm(p->queue,
				       p->context,
				       p->stream.owner_generation,
				       p->stream.epoch,
				       SB_Q_ERROR,
				       p->transport.elapsed_ms);
		fail(p, "stream_token_expired");
	}
	if (p->resource_active && p->owner->value.current == SB_OWNER_MUSIC &&
	    !p->existing &&
	    (p->owner->value.generation != p->stream.owner_generation ||
	     !p->owner->connected)) {
		p->reset = 1;
		(void)sb_queue_confirm(p->queue,
				       p->context,
				       p->stream.owner_generation,
				       p->stream.epoch,
				       SB_Q_ERROR,
				       p->transport.elapsed_ms);
		fail(p, "physical_owner_revoked");
	}
	if (p->revoke_candidate && !p->ipc_pending) {
		if (issue(p, SB_RESOURCE_REVOKE)) {
			p->revoke_candidate = p->candidate_registered = 0;
		}
		publish(p);
		return;
	}
	if (p->reset && !p->ipc_pending) {
		if (issue(p, SB_RESOURCE_RESET))
			p->reset = 0;
		publish(p);
		return;
	}
	if (acknowledged < 0) {
		publish(p);
		return;
	}
	/* The startup probe has its own deadline; every later message must
	 * be acknowledged promptly or the load fails loudly instead of
	 * wedging with a pending capability. */
	if (p->ipc_pending && p->ipc_sent && p->ipc_verified &&
	    now >= p->ipc_sent_ms && now - p->ipc_sent_ms > 15000) {
		fprintf(stderr,
			"MUSIC_IPC_TIMEOUT op=%u seq=%llu now=%llu sent=%llu\n",
			p->message.operation,
			(unsigned long long)p->message.sequence,
			(unsigned long long)now,
			(unsigned long long)p->ipc_sent_ms);
		fail(p, "resource_ack_timeout");
		publish(p);
		return;
	}
	if (p->phase != SB_PLAY_ACTIVE && p->phase != SB_PLAY_IDLE &&
	    p->phase != SB_PLAY_FAILED && now >= p->deadline) {
		fail(p, "playback_action_timeout");
		publish(p);
		return;
	}
	/* Jive drops a server-silent SlimProto connection after 35 s
	 * (READ_TIMEOUT), which kills an active native stream on detach.
	 * Poll a live stream well inside that window. strm-t is answered
	 * with STMt and changes no decoder state (jive/audio/Playback.lua);
	 * a failed poll retries next tick and never fails the stream. */
	if (p->phase == SB_PLAY_ACTIVE && p->owner->connected &&
	    p->owner->value.current == SB_OWNER_MUSIC &&
	    p->transport.attached && now >= p->keepalive_ms &&
	    now - p->keepalive_ms >= 15000) {
		unsigned char frame[64];
		size_t length = sb_slim_strm('t',
					     (uint32_t)(now & 0xffffffffu),
					     NULL,
					     frame,
					     sizeof frame);
		if (length &&
		    p->send(p->send_context, frame, length, &p->stream)) {
			p->keepalive_ms = now;
			fprintf(stderr,
				"MUSIC_STRM_SEND cmd=t epoch=%llu owner=%llu conn=%llu\n",
				(unsigned long long)p->stream.epoch,
				(unsigned long long)p->stream.owner_generation,
				(unsigned long long)p->transport.connection);
		}
	}
	sb_music_load_tick(p->load, now);
	if (p->phase == SB_PLAY_LOADING) {
		if (sb_music_load_state(p->load) == SB_LOAD_FAILED)
			fail(p,
			     sb_music_library_status(
				     sb_music_load_error(p->load)));
		else if (sb_music_load_state(p->load) == SB_LOAD_READY) {
			p->selection_count = sb_music_load_count(p->load);
			if (p->edit != SB_Q_LOAD) {
				if (sb_queue_commit(p->queue, p->operation) !=
				    SB_Q_OK)
					fail(p, "queue_commit_failed");
				else {
					p->staged = 0;
					(void)sb_music_load_cancel(p->load);
					p->phase = p->stream.epoch
							   ? SB_PLAY_ACTIVE
							   : SB_PLAY_IDLE;
					p->completed = p->operation;
					p->completed_success = 1;
				}
			} else {
				p->candidate = *sb_music_load_first(p->load);
				p->file_request = 0;
				p->phase = SB_PLAY_FILE;
			}
		}
	}
	if (p->phase == SB_PLAY_NEED_CONNECTION && p->transport.attached &&
	    !p->transport.used_connection && !p->ipc_pending && !p->reset) {
		if (p->staged) {
			p->file_request = 0;
			p->phase = SB_PLAY_FILE;
		} else {
			sb_queue_status q = sb_queue_snapshot(p->queue);
			const sb_queue_item *item =
				sb_queue_at(p->queue, q.intended);
			if (!item || !item->available)
				fail(p, "queue_entry_unavailable");
			else if(item->identity.track.kind==SB_TRACK_NETWORK){network_candidate(p,item);p->file_request=0;p->phase=SB_PLAY_FILE;}
			else if (!p->detail_job) {
				sb_ml_request request = {0};
				request.operation = SB_ML_DETAIL;
				request.data.detail.identity =
					item->identity.track;
				request.data.detail.generation =
					item->identity.media_generation;
				(void)sb_music_async_submit(p->async,
							    &request,
							    now + 5000,
							    &p->detail_job);
			} else {
				sb_ml_result result;
				sb_ml_status s = sb_music_async_poll(
					p->async, p->detail_job, &result);
				if (s != SB_ML_BUSY) {
					p->detail_job = 0;
					if (s != SB_ML_OK ||
					    result.status != SB_ML_OK)
						fail(p,
						     "queue_track_lookup_"
						     "failed");
					else {
						p->candidate =
							result.data.track;
						p->file_request = 0;
						p->phase = SB_PLAY_FILE;
					}
				}
			}
		}
	}
	if (p->phase == SB_PLAY_FILE && !p->ipc_pending)
		prepare_file(p, now);
	if (p->phase == SB_PLAY_RESERVING &&
	    (acknowledged == SB_RESOURCE_RESERVE || acknowledged==SB_RESOURCE_STREAM_RESERVE))
		p->phase = SB_PLAY_OWNER;
	if (p->phase == SB_PLAY_OWNER) {
		if (p->transport.used_connection) {
			if (p->transport.state != SB_T_STOPPED &&
			    !p->transport.stream.terminal_seen &&
			    !control(p, 'q', now)) {
				fail(p, "current_stream_stop_failed");
				publish(p);
				return;
			}
			p->phase = SB_PLAY_NEED_CONNECTION;
			p->existing = 1;
			publish(p);
			return;
		}
		if (!sb_owner_controls(p->owner,
				       SB_OWNER_MUSIC,
				       p->owner->value.generation,
				       p->transport.connection)) {
			if (!p->owner_requested &&
			    p->owner->value.current != SB_OWNER_MUSIC) {
				if (p->owner->last_request == UINT64_MAX ||
				    !sb_owner_request(p->owner,
						      SB_OWNER_MUSIC,
						      p->owner->last_request +
							      1,
						      now,
						      10000)) {
					fail(p, "physical_owner_unavailable");
					publish(p);
					return;
				}
				p->owner_requested = 1;
			}
			if (p->owner->value.yield == SB_YIELD_REFUSED ||
			    p->owner->value.yield == SB_YIELD_TIMEOUT)
				fail(p, "physical_yield_refused");
		} else {
			if (!sb_transport_quiescent(
				    &p->transport,
				    p->transport.connection,
				    p->owner->value.generation)) {
				fail(p, "decoder_quiescence_missing");
				publish(p);
				return;
			}
			p->message.resource.owner_generation =
				p->owner->value.generation;
			if (issue(p, SB_RESOURCE_ACTIVATE))
				p->phase = SB_PLAY_ACTIVATING;
		}
	}
	if (p->phase == SB_PLAY_ACTIVATING &&
	    acknowledged == SB_RESOURCE_ACTIVATE) {
		p->candidate_registered = 0;
		p->resource_active = 1;
		p->active_expires = p->message.resource.expires_ms;
		p->stream = (sb_music_stream_id){p->epoch,
						 p->owner->value.generation,
						 p->media.generation,
						 p->transport.connection,
						 p->candidate.identity};
		sb_slim_start start = {0};
		start.codec = (char)p->message.codec;
		start.port = p->port;
		sb_music_token_encode(p->message.resource.token, start.token);
		unsigned char frame[SB_SLIM_HTTP_MAX + 32];
		size_t length =
			sb_slim_strm('s', 0, &start, frame, sizeof frame);
		if (!sb_owner_controls(p->owner,
				       SB_OWNER_MUSIC,
				       p->stream.owner_generation,
				       p->stream.connection_generation) ||
		    !length ||
		    !sb_transport_owned_prepare(&p->transport,
						p->owner,
						&p->stream,
						start.codec,
						now) ||
		    !sb_transport_owned_command(
			    &p->transport, p->owner, &p->stream, 's', now) ||
		    !p->send(p->send_context, frame, length, &p->stream)) {
			fail(p, "stream_start_failed");
			publish(p);
			return;
		}
		fprintf(stderr, "MUSIC_STRM_SEND cmd=s epoch=%llu owner=%llu conn=%llu\n",
			(unsigned long long)p->stream.epoch,
			(unsigned long long)p->stream.owner_generation,
			(unsigned long long)p->transport.connection);
		p->keepalive_ms = now;
		(void)sb_queue_bind_owner(
			p->queue, p->context, p->stream.owner_generation);
		if (p->staged &&
		    sb_queue_commit(p->queue, p->operation) != SB_Q_OK) {
			(void)control(p, 'q', now);
			fail(p, "queue_commit_failed");
			publish(p);
			return;
		}
		p->staged = 0;
		(void)sb_music_load_cancel(p->load);
		if (sb_queue_prepared(p->queue,
				      p->context,
				      p->stream.owner_generation,
				      p->epoch) != SB_Q_OK) {
			fail(p, "queue_start_failed");
			publish(p);
			return;
		}
		p->phase = SB_PLAY_ACTIVE;
		p->owner_requested = 0;
		p->existing = 0;
		p->completed = p->operation;
		p->completed_success = 1;
	}
	/* Music yields only via explicit adapter quiescence proof, never STATf.
	 */
	if (p->owner->value.current == SB_OWNER_MUSIC &&
	    p->owner->value.yield == SB_YIELD_REQUESTED) {
		if (p->yield_request != p->owner->value.request_id) {
			p->yield_request = p->owner->value.request_id;
			p->reset = 1;
		}
		if (p->transport.state != SB_T_STOPPING &&
		    !p->transport.stream.terminal_seen)
			(void)control(p, 'q', now);
	}
	publish(p);
}

static void transport_status(sb_music_playback *p, uint64_t now)
{
	sb_transport *t = &p->transport;
	const sb_music_stream_id *stream = &t->stream.id;
	if (t->stream.terminal_seen) {
		if (!stream->epoch || p->terminal_handled == stream->epoch)
			return;
		p->terminal_handled = stream->epoch;
		fprintf(stderr, "MUSIC_STREAM_TERMINAL state=%d epoch=%llu owner=%llu\n",
			(int)t->stream.state, (unsigned long long)stream->epoch,
			(unsigned long long)stream->owner_generation);
		if (t->stream.state == SB_STREAM_STOPPED) {
			if (p->completed < p->operation || p->existing) {
				/* Sub-tick race: the superseded stream's
				 * terminal landed after LOAD-accept but
				 * before its first tick. Free the slot for
				 * the in-flight load; it re-proves
				 * quiescence and epoch at ACTIVATE. */
				p->attempted = 0;
				p->reset = 1;
				(void)sb_transport_release_stream(t);
				return;
			}
			(void)sb_queue_confirm(p->queue,
					       p->context,
					       stream->owner_generation,
					       stream->epoch,
					       SB_Q_STOPPED,
					       0);
			p->phase = SB_PLAY_IDLE;
			p->reset = 1;
			/* Terminal stop frees the single-track latch and the
			 * connection; a later load re-qualifies from idle. */
			p->attempted = 0;
			(void)sb_transport_release_stream(t);
			return;
		}
		int drained = t->error == SB_T_OK &&
			      t->stream.state == SB_STREAM_DRAINED;
		/* A natural drain racing an open LOAD is the old stream's
		 * expected end, not a failure: its successor proceeds
		 * (the post-drain firmware reset below still runs for
		 * it). Failed terminals keep cancelling the pending load. */
		if (p->staged && !(drained && p->completed < p->operation)) {
			(void)sb_music_load_cancel(p->load);
			p->staged = 0;
			retire_file(p);
		}
		int advanced = sb_queue_terminal(p->queue,
						 p->context,
						 stream->owner_generation,
						 stream->epoch,
						 drained);
		p->reset = 1;
		if (advanced &&
		    sb_queue_snapshot(p->queue).mode == SB_Q_WAIT_CONNECTION) {
			p->phase = SB_PLAY_NEED_CONNECTION;
			p->deadline = now + 20000;
			p->existing = 1;
		} else if (!drained) {
			p->phase = SB_PLAY_FAILED;
			strcpy(p->error, "transport_failed");
		}
		/* WP4a.6: a naturally drained decoder keeps C-layer replay
		 * state that stalls the next strm-s (STMc + HTTP 200, then
		 * silence: decode never starts, no STMs). Reset the
		 * firmware with a post-drain stop while ownership is still
		 * ours. The transport is already terminal so the validated
		 * control() path refuses; this raw frame carries the
		 * completed stream identity on the still-attached
		 * connection. The resulting flush is rejected by the
		 * terminal guard, or absorbed as a rebuffer/connect
		 * self-flush if it lands in the next stream.
		 * Failure-terminal drains stay silent: a wedged drain may
		 * still be audible. */
		if (drained &&
		    p->owner->value.current == SB_OWNER_MUSIC && p->send) {
			unsigned char reset[64];
			size_t reset_length = sb_slim_strm('q',
							   0,
							   NULL,
							   reset,
							   sizeof reset);
			if (reset_length &&
			    p->send(p->send_context,
				    reset,
				    reset_length,
				    &p->stream))
				fprintf(stderr,
					"MUSIC_STRM_SEND cmd=q epoch=%llu owner=%llu conn=%llu\n",
					(unsigned long long)p->stream.epoch,
					(unsigned long long)p->stream.owner_generation,
					(unsigned long long)p->transport.connection);
		}
		/* Terminal drain/failure also retires the finished stream:
		 * NEED_CONNECTION advance and later loads share one freed
		 * connection, each re-proving quiescence and epoch. */
		p->attempted = 0;
		(void)sb_transport_release_stream(t);
	} else if (t->state == SB_T_PLAYING || t->state == SB_T_PAUSED) {
		(void)sb_queue_confirm(p->queue,
				       p->context,
				       stream->owner_generation,
				       stream->epoch,
				       t->state == SB_T_PLAYING ? SB_Q_PLAYING
								: SB_Q_PAUSED,
				       t->elapsed_ms);
	} else if (t->state == SB_T_BUFFERING) {
		(void)sb_queue_confirm(p->queue,
				       p->context,
				       stream->owner_generation,
				       stream->epoch,
				       SB_Q_BUFFERING,
				       t->elapsed_ms);
	}
}

int sb_playback_event(sb_music_playback *p,
		      uint64_t connection,
		      const sb_music_stream_id *stream,
		      const sb_slim_event *event,
		      uint64_t now)
{
	if (p && event)
		fprintf(stderr, "MUSIC_SLIM_EVENT kind=%d code=%.4s jif=%u http=%u epoch=%llu\n",
			(int)event->kind, event->code, event->jiffies,
			event->http_status,
			(unsigned long long)(stream ? stream->epoch : 0));
	if (!p || !stream ||
	    !sb_transport_event(&p->transport, connection, stream, event, now))
		return 0;
	transport_status(p, now);
	publish(p);
	return 1;
}

void sb_playback_media_removed(sb_music_playback *p,
			       const sb_music_media *media)
{
	if(p&&media&&sb_music_source_equal(&p->local_media.source,&media->source)&&p->local_media.generation==media->generation)p->local_media.online=0;
	if (!p || !media ||
	    !sb_music_source_equal(&p->media.source, &media->source) ||
	    p->media.generation != media->generation)
		return;
	p->media.online = 0;
	p->reset = 1;
	if (p->phase == SB_PLAY_ACTIVE)
		(void)control(p, 'q', np_monotonic_ms());
	sb_queue_revoke(p->queue, &media->source, media->generation);
	fail(p, "media_removed");
	publish(p);
}

static int
status_append(char *out, size_t cap, size_t *used, const char *format, ...)
{
	if (*used >= cap)
		return 0;
	va_list args;
	va_start(args, format);
	int n = vsnprintf(out + *used, cap - *used, format, args);
	va_end(args);
	if (n < 0 || (size_t)n >= cap - *used)
		return 0;
	*used += (size_t)n;
	return 1;
}

static int status_row(char *out,
		      size_t cap,
		      size_t *used,
		      const sb_queue_item *item,
		      uint32_t index,
		      int jive)
{
 char title[768], artist[768], album[768], artwork[2304];
	np_json_escape(title, sizeof title, item->title);
	np_json_escape(artist, sizeof artist, item->artist);
	np_json_escape(album, sizeof album, item->album);
	np_json_escape(artwork,sizeof artwork,item->artwork);
	if(item->identity.track.kind==SB_TRACK_NETWORK){
	 return status_append(out,cap,used,"{\"id\":\"network:%" PRIu64 "\",\"source_type\":\"network\",\"queue_entry_id\":\"%" PRIu64 "\",\"playlist index\":%u,\"title\":\"%s\",\"artist\":\"%s\",\"album\":\"%s\",\"duration\":%.3f,\"type\":\"mp3\",\"available\":%d,\"playable\":%d,\"text\":\"%s\",\"icon\":\"%s\",\"params\":{\"playlist_index\":%u}}",item->identity.track.network_id,item->identity.entry_id,index,title,artist,album,item->duration_ms/1000.0,item->available,item->eligible,title,artwork,index);
	}
	if (!status_append(out,
			   cap,
			   used,
			   "{\"id\":\"%" PRIu64 "\",\"track_id\":\"%" PRIu64
			   "\",\"queue_entry_id\":\"%" PRIu64 "\",\"playlist "
			   "index\":%u,\"title\":\"%s\",\"artist\":\"%s\","
			   "\"album\":\"%s\","
			   "\"duration\":%.3f,\"type\":\"%s\",\"available\":%d,"
			   "\"playable\":%d,"
			   "\"params\":{\"track_id\":\"%" PRIu64
			   "\",\"playlist_index\":%u}",
			   item->identity.track.track_id,
			   item->identity.track.track_id,
			   item->identity.entry_id,
			   index,
			   title,
			   artist,
			   album,
			   item->duration_ms / 1000.0,
			   item->codec == 'm' ? "mp3" : "flac",
			   item->available,
			   item->eligible,
			   item->identity.track.track_id,
			   index))
		return 0;
	if (jive &&
	    !status_append(out,
			   cap,
			   used,
			   ",\"text\":\"%s\",\"track\":\"%s\",\"icon\":\"%s\"",
			   title,
			   title, artwork))
		return 0;
	return status_append(out, cap, used, "}");
}

static int status_digits(const char *s, uint64_t limit, uint64_t *value)
{
	uint64_t n;
	if (!s || !*s)
		return 0;
	n = 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9' ||
		    n > (limit - (unsigned)(*s - '0')) / 10)
			return 0;
		n = n * 10 + (unsigned)(*s - '0');
	}
	*value = n;
	return 1;
}

int sb_playback_status_args(unsigned count,
			    const char *first,
			    const char *second,
			    uint32_t audible,
			    uint32_t intended,
			    uint32_t *offset,
			    uint32_t *quantity)
{
	uint64_t off = 0, qty = 1;
	if (!offset || !quantity || !count)
		return 0;
	if (count == 2)
		return 0;
	if (count >= 3) {
		if (first && !strcmp(first, "-")) {
			/* LMS "-" offset: from the current song. */
			off = audible == SB_QUEUE_NONE ?
				(intended == SB_QUEUE_NONE ? 0u : intended) :
				audible;
		} else if (!status_digits(first, UINT32_MAX, &off)) {
			return 0;
		}
		if (!status_digits(second, 200, &qty))
			return 0;
	}
	*offset = (uint32_t)off;
	*quantity = (uint32_t)qty;
	return 1;
}

static int playlist_row(char *out,size_t cap,size_t *used,const sb_queue_item *item,uint32_t index,const sb_queue_status *q)
{
 if(!status_row(out,cap,used,item,index,1))return 0;
 /* Extend the same typed metadata projection; no second playlist exists. */
 (*used)--;
 return status_append(out,cap,used,",\"actions\":{\"go\":{\"cmd\":[\"playlist\",\"index\",\"%u\"],\"params\":{\"queue_context\":\"%" PRIu64 "\",\"queue_revision\":\"%" PRIu64 "\",\"queue_entry_id\":\"%" PRIu64 "\"},\"nextWindow\":\"nowPlaying\"},\"more\":{\"cmd\":[\"playlist\",\"itemmenu\",\"%u\"],\"params\":{\"queue_context\":\"%" PRIu64 "\",\"queue_revision\":\"%" PRIu64 "\",\"queue_entry_id\":\"%" PRIu64 "\"},\"window\":{\"isContextMenu\":1}}}}",index,q->context,q->revision,item->identity.entry_id,index,q->context,q->revision,item->identity.entry_id);
}

static int playback_status_page(sb_music_playback *p,
			    uint32_t offset,
			    uint32_t quantity,
			    char *out,
			    size_t cap, int menu)
{
	if (!p || !out || !cap || quantity > 200 ||
	    offset > UINT32_MAX - quantity)
		return 0;
	sb_queue_status q = sb_queue_snapshot(p->queue);
	const sb_queue_item *item = sb_queue_at(p->queue, q.audible);
	uint32_t count = offset < q.count ? q.count - offset : 0;
	if (count > quantity)
		count = quantity;
	const char *mode = q.mode == SB_Q_PLAYING  ? "play"
			   : q.mode == SB_Q_PAUSED ? "pause"
						   : "stop";
	if(p->transport.state==SB_T_STOPPING&&!p->transport.stream.terminal_seen)mode=p->stop_previous_mode==SB_Q_PAUSED?"pause":"play";
	size_t used = 0;
	if (!status_append(
		    out,
		    cap,
		    &used,
		    "{\"player_name\":\"Standalone\",\"player_connected\":%d,"
		    "\"power\":%d,\"mixer volume\":%d,\"digital_volume_control\":1,"
		    "\"mode\":\"%s\",\"rate\":%d,\"time\":%.3f,\"duration\":%."
		    "3f,"
		    "\"playlist_tracks\":%u,\"playlist_cur_index\":%d,"
		    "\"playlist_timestamp\":%" PRIu64
		    ",\"queue_revision\":%" PRIu64
		    ",\"queue_context\":\"%" PRIu64
		    "\",\"intended_index\":%d,\"count\":%u,\"offset\":%u,"
		    "\"playlist repeat\":0,"
		    "\"playlist shuffle\":0,\"source\":\"standalone_music\"",
		    p->transport.attached,
		    np_player_power(p->np),np_player_volume(p->np),
		    mode,
		    q.mode == SB_Q_PLAYING ? 1 : 0,
		    q.elapsed_ms / 1000.0,
		    item ? item->duration_ms / 1000.0 : 0.0,
		    q.count,
		    q.audible != SB_QUEUE_NONE ? (int)q.audible : q.intended != SB_QUEUE_NONE ? (int)q.intended : -1,
		    q.revision,
		    q.revision,
		    q.context,
		    q.intended == SB_QUEUE_NONE ? -1 : (int)q.intended,
		    menu?q.count:count,
		    offset))
		return 0;
 if(!status_append(out,cap,&used,",\"playlist_provider\":\"standalone_music\",\"provider_generation\":\"%" PRIu64 "\",\"remoteMeta\":{\"shuffle\":true,\"repeat\":true,\"buttons\":{\"fwd\":%d,\"rew\":%d,\"shuffle\":{\"jiveStyle\":\"shuffleDisabled\",\"command\":[\"jiveblankcommand\"]},\"repeat\":{\"jiveStyle\":\"repeatDisabled\",\"command\":[\"jiveblankcommand\"]}}}",q.context,q.intended!=SB_QUEUE_NONE&&q.intended+1<q.count,q.intended!=SB_QUEUE_NONE&&q.intended>0))return 0;
	if(menu){
	 if(!status_append(out,cap,&used,",\"playlist_capabilities\":{\"select\":true,\"remove\":true,\"move\":true,\"clear\":true,\"shuffle\":false,\"repeat\":false},\"item_loop\":["))return 0;
	 for(uint32_t n=0;n<count;n++)if((n&&!status_append(out,cap,&used,","))||!playlist_row(out,cap,&used,sb_queue_at(p->queue,offset+n),offset+n,&q))return 0;
	 return status_append(out,cap,&used,"]}");
	}
	/* Stock Player._whatsPlaying indexes item_loop[1] when the key exists.
	 * An empty Lua array is truthy: omit the key until STMs confirms a
	 * track. */
	if (item) {
		if (!status_append(out, cap, &used, ",\"item_loop\":[") ||
		    !status_row(out, cap, &used, item, q.audible, 1) ||
		    !status_append(out, cap, &used, "]"))
			return 0;
	}
	if (!status_append(out, cap, &used, ",\"playlist_loop\":["))
		return 0;
	for (uint32_t n = 0; n < count; n++) {
		if ((n && !status_append(out, cap, &used, ",")) ||
		    !status_row(out,
				cap,
				&used,
				sb_queue_at(p->queue, offset + n),
				offset + n,
				0))
			return 0;
	}
	return status_append(out, cap, &used, "]}");
}

int sb_playback_status_page(sb_music_playback *p,uint32_t offset,uint32_t quantity,char *out,size_t cap)
{return playback_status_page(p,offset,quantity,out,cap,0);}
int sb_playback_playlist_page(sb_music_playback *p,uint32_t offset,uint32_t quantity,char *out,size_t cap)
{return playback_status_page(p,offset,quantity,out,cap,1);}

int sb_playback_status(sb_music_playback *p, char *out, size_t cap)
{
	sb_queue_status q = sb_queue_snapshot(p ? p->queue : NULL);
	return sb_playback_status_page(
		p, q.audible == SB_QUEUE_NONE ? 0 : q.audible, 1, out, cap);
}

int sb_playback_destroy(sb_music_playback *p)
{
	if (!p)
		return 1;
	if (!p->stopping) {
		if (sb_owner_controls(p->owner,
				      SB_OWNER_MUSIC,
				      p->stream.owner_generation,
				      p->stream.connection_generation) &&
		    p->transport.used_connection &&
		    !p->transport.stream.terminal_seen)
			(void)control(p, 'q', np_monotonic_ms());
		/* Destruction is not positive decoder proof. Quarantine the
		 * adapter rather than granting any successor if cleanup cannot
		 * be confirmed. */
		if (p->owner->registrations[SB_OWNER_MUSIC])
			(void)sb_owner_crashed(
				p->owner,
				SB_OWNER_MUSIC,
				p->owner->registrations[SB_OWNER_MUSIC]);
		p->stopping = 1;
		if (p->ipc >= 0)
			close(p->ipc);
		p->ipc = -1;
		np_native_music(p->np, NULL, 0);
	}
	if (p->load && !sb_music_load_destroy(p->load))
		return 0;
	p->load = NULL;
	if (p->detail_job) {
		if (sb_music_async_cancel(p->async, p->detail_job) ==
		    SB_ML_BUSY)
			return 0;
		p->detail_job = 0;
	}
	if (sb_music_file_stop(p->file) == SB_ML_BUSY ||
	    sb_music_file_destroy(p->file) == SB_ML_BUSY)
		return 0;
	p->file = NULL;
	if (p->message.fd >= 0)
		close(p->message.fd);
	sb_queue_destroy(p->queue);
	free(p);
	return 1;
}

int sb_playback_bridge_proof(void *context,uint64_t adapter,uint64_t connection,uint64_t generation,uint64_t now)
{
 return sb_playback_idle_proof((sb_music_playback *)context,adapter,connection,generation,now,1,1,1);
}

int sb_playback_bridge_begin(void *context,uint64_t now)
{
 sb_music_playback *p=context;
 if(!p||p->owner->value.current!=SB_OWNER_MUSIC)return 0;
 if(p->completed<p->operation)sb_playback_cancel(p,p->operation,p->context);
 if(p->transport.used_connection&&!p->transport.stream.terminal_seen&&p->transport.state!=SB_T_STOPPING)
  if(!control(p,'q',now))return 0;
 p->reset=1;
 return 1;
}
int sb_playback_bridge_ready(void *context)
{
 sb_music_playback *p=context;
 return p&&p->ipc>=0&&(!p->ipc_probe_required||p->ipc_verified)&&
        p->phase!=SB_PLAY_FAILED&&!p->resource_active&&!p->candidate_registered&&!p->reset&&!p->ipc_pending&&
        !p->retired_file&&!p->detail_job&&!p->file_request;
}

int sb_playback_probe_resources(sb_music_playback *p,uint64_t now)
{
 if(!p||p->phase!=SB_PLAY_IDLE||p->ipc_pending||p->resource_active||p->candidate_registered||
    p->owner->value.current!=SB_OWNER_NONE||p->ipc_probe_required)return 0;
 p->ipc_probe_required=1;p->ipc_verified=0;p->ipc_probe_deadline=now+5000;p->reset=1;
 return 1;
}

unsigned sb_playback_codecs(const sb_music_playback *p)
{
 /* WP4a.6 switch: play actions stay offered mid-play; the latch no
  * longer hides them while a replaceable stream is live. */
 return p&&p->transport.attached?
        p->transport.hello.codecs:0;
}

int sb_playback_single_mp3(sb_music_playback *p)
{
 if(!p||p->operation||p->phase!=SB_PLAY_IDLE)return 0;
 p->single_mp3=1;return 1;
}
