/* Shared production control glue; no fixture admission/proof routes. */
#include <inttypes.h>

static int playback_send(void *unused,
			 const unsigned char *frame,
			 size_t length,
			 const sb_music_stream_id *stream)
{
	(void)unused;
#ifdef SB_MUSIC_RUNTIME
 /* strm-s alone starts a new stream; teardown commands remain available. */
 if(length>6&&!memcmp(frame+2,"strm",4)&&frame[6]=='s'&&
    !sb_ts_music_allowed(tinysc_lifecycle))return 0;
#endif
	for (unsigned i = 0; i < CLIENTS; i++) {
		client *c = &clients[i];
		if (c->fd < 0 || c->kind != 1 ||
		    c->music_connection != stream->connection_generation)
			continue;
		if (c->output.connection != stream->connection_generation ||
		    c->output.owner_generation != stream->owner_generation ||
		    c->output.stream_epoch != stream->epoch) {
			if (!sb_output_scope(&c->output,
					     stream->connection_generation,
					     stream->owner_generation,
					     stream->epoch)) {
				close_client(c);
				return 0;
			}
		}
		if (!sb_output_scoped(&c->output,
				      c->fd,
				      stream->connection_generation,
				      stream->owner_generation,
				      stream->epoch,
				      frame,
				      length,
				      np_monotonic_ms())) {
			close_client(c);
			return 0;
		}
		return 1;
	}
	return 0;
}

static void playback_service(void)
{
	if (!local_music)
		return;
#ifdef SB_MUSIC_RUNTIME
 music_runtime_tick();
#endif
	static uint64_t metadata, playback, queue_revision;
	int alarm = sb_alarms_active(alarms);
	if (alarm && !local_owner.suspended &&
	    local_owner.value.current == SB_OWNER_MUSIC) {
		sb_queue_status q =
			sb_queue_snapshot(sb_playback_queue(local_music));
		(void)sb_playback_control(
			local_music, 'q', q.context, np_monotonic_ms());
	}
	sb_owner_suspend(&local_owner, alarm);
	sb_playback_tick(local_music, np_monotonic_ms());
	sb_queue_status q = sb_queue_snapshot(sb_playback_queue(local_music));
	sb_browse_control_context(music_browse, q.context);
	const np_status *status = np_current(nowplaying);
	if (metadata != status->metadata_revision ||
	    playback != status->playback_revision ||
	    queue_revision != q.revision) {
		metadata = status->metadata_revision;
		playback = status->playback_revision;
		queue_revision = q.revision;
		publish_playerstatus();
	}
	for (unsigned i = 0; i < CLIENTS; i++) {
		client *c = &clients[i];
		if (c->fd < 0 || c->kind != 1 || !c->music_connection)
			continue;
		if (c->output.length && c->output.owner_generation &&
		    c->output.owner_generation !=
			    local_owner.value.generation) {
			close_client(c);
			continue;
		}
		if (sb_playback_phase(local_music) == SB_PLAY_NEED_CONNECTION &&
		    c->music_connection == sb_playback_stream(local_music)
						   ->connection_generation) {
			if (c->output.length)
				c->close_after_drain = 1;
			else
				close_client(c);
		}
	}
}

static int playback_number(const char *s, uint64_t limit, uint64_t *value)
{
	if (!s || !*s)
		return 0;
	uint64_t n = 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9' ||
		    n > (limit - (unsigned)(*s - '0')) / 10)
			return 0;
		n = n * 10 + (unsigned)(*s - '0');
	}
	*value = n;
	return 1;
}

static int playback_command(client *connection, struct music_http *h)
{
	const sb_lms_command *cmd = &h->command;
	const char *name = cmd->arguments[0];
	int playlistcontrol = !strcmp(name, "playlistcontrol");
	int control_command = !strcmp(name, "play") || !strcmp(name, "pause") ||
			      !strcmp(name, "stop") ||
			      !strcmp(name, "playlist");
	/* A quarantined Music context must fail closed, not enqueue an external
	 * application command merely because physical admission is suspended.
	 */
	int owns = local_owner.value.current == SB_OWNER_MUSIC;
	if (!owns && !strcmp(name, "play") &&
	    local_owner.value.current == SB_OWNER_NONE &&
	    local_owner.baseline_idle &&
	    sb_queue_snapshot(sb_playback_queue(local_music)).count)
		owns = 1;
	if (!playlistcontrol &&
	    (!owns || (!control_command && strcmp(name, "status"))))
		return 0;
	char data[8192];
	int success = 0;
#ifdef SB_MUSIC_RUNTIME
 if(!sb_ts_music_allowed(tinysc_lifecycle)){
  music_runtime_tick();
  strcpy(data,"{\"error\":\"tinysc_lifecycle_unavailable\"}");goto reply;
 }
#endif
	sb_queue_status q = sb_queue_snapshot(sb_playback_queue(local_music));
	uint64_t context = h->playback_context;
	client *stream = stream_for_id(h->envelope.client);
	if (!player_id[0] || strcmp(cmd->player, player_id) ||
	    context != q.context ||
	    (stream && stream->music_control_context &&
	     stream->music_control_context != q.context && !playlistcontrol)) {
		strcpy(data, "{\"error\":\"stale_playback_context\"}");
		goto reply;
	}
	if (playlistcontrol) {
		const char *action = sb_lms_tag(cmd, "cmd"),
			   *track = sb_lms_tag(cmd, "track_id"),
			   *album = sb_lms_tag(cmd, "album_id"),
			   *tag = sb_lms_tag(cmd, "queue_context");
		uint64_t id, tagged_context = context;
		if (!action || (!!track == !!album) ||
		    !playback_number(track ? track : album, INT64_MAX, &id) ||
		    !id ||
		    (tag &&
		     (!playback_number(tag, UINT64_MAX, &tagged_context) ||
		      tagged_context != context)) ||
		    (strcmp(action, "load") && strcmp(action, "add") &&
		     strcmp(action, "insert"))) {
			strcpy(data, "{\"error\":\"invalid_playlistcontrol\"}");
			goto reply;
		}
		for (unsigned i = 1; i < cmd->count; i++)
			if (strncmp(cmd->arguments[i], "cmd:", 4) &&
			    strncmp(cmd->arguments[i], "track_id:", 9) &&
			    strncmp(cmd->arguments[i], "album_id:", 9) &&
			    strncmp(cmd->arguments[i], "queue_context:", 14)) {
				strcpy(data,
				       "{\"error\":\"unsupported_"
				       "playlistcontrol_tag\"}");
				goto reply;
			}
		sb_queue_edit edit = !strcmp(action, "load")  ? SB_Q_LOAD
				     : !strcmp(action, "add") ? SB_Q_ADD
							      : SB_Q_INSERT;
		sb_queue_result result = sb_playback_load(local_music,
							  edit,
							  id,
							  album != NULL,
							  context,
							  np_monotonic_ms());
		if (result != SB_Q_OK) {
			strcpy(data,
			       "{\"error\":\"music_action_busy_or_stale\"}");
			goto reply;
		}
		if (stream)
			stream->music_control_context = context;
		h->playback_operation = sb_playback_operation(local_music);
		h->deadline = np_monotonic_ms() + 20000;
		return 1;
	}
	if (!strcmp(name, "status")) {
		uint64_t offset = 0, quantity = 1;
		if (cmd->count == 2 ||
		    (cmd->count >= 3 &&
		     (!playback_number(
			      cmd->arguments[1], UINT32_MAX, &offset) ||
		      !playback_number(cmd->arguments[2], 200, &quantity)))) {
			strcpy(data, "{\"error\":\"invalid_status_page\"}");
			goto reply;
		}
		for (unsigned n = 3; n < cmd->count; n++)
			if (strncmp(cmd->arguments[n], "tags:", 5) &&
			    strncmp(cmd->arguments[n], "menu:", 5) &&
			    strncmp(cmd->arguments[n], "subscribe:", 10)) {
				strcpy(data,
				       "{\"error\":\"unsupported_status_"
				       "tag\"}");
				goto reply;
			}
		char *page = malloc(SB_BROWSE_JSON_MAX - 2048);
		if (!page) {
			strcpy(data, "{\"error\":\"status_busy\"}");
			goto reply;
		}
		success = sb_playback_status_page(local_music,
						  (uint32_t)offset,
						  (uint32_t)quantity,
						  page,
						  SB_BROWSE_JSON_MAX - 2048);
		if (!music_reply(h,
				 success,
				 success ? page
					 : "{\"error\":\"status_too_large\"}"))
			music_overflow(h);
		free(page);
		register_playerstatus(h->envelope.response);
		return 2;
	}
	if (!strcmp(name, "playlist") && cmd->count >= 2) {
		const char *operation = cmd->arguments[1];
		uint64_t index;
		if (!strcmp(operation, "clear") && cmd->count == 2) {
			(void)sb_playback_control(
				local_music, 'q', context, np_monotonic_ms());
			success = sb_queue_clear(sb_playback_queue(local_music),
						 q.revision,
						 context) == SB_Q_OK;
		} else if (!strcmp(operation, "delete") && cmd->count == 3 &&
			   playback_number(
				   cmd->arguments[2], UINT32_MAX, &index)) {
			if (index == q.audible || index == q.intended)
				(void)sb_playback_control(local_music,
							  'q',
							  context,
							  np_monotonic_ms());
			success =
				sb_queue_remove(sb_playback_queue(local_music),
						(uint32_t)index,
						q.revision,
						context) == SB_Q_OK;
		} else if (!strcmp(operation, "index") && cmd->count == 3) {
			if (!strcmp(cmd->arguments[2], "+1"))
				index = q.intended == SB_QUEUE_NONE
						? UINT64_MAX
						: (uint64_t)q.intended + 1;
			else if (!strcmp(cmd->arguments[2], "-1"))
				index = !q.intended || q.intended ==
								SB_QUEUE_NONE
						? UINT64_MAX
						: q.intended - 1;
			else if (!playback_number(
					 cmd->arguments[2], UINT32_MAX, &index))
				index = UINT64_MAX;
			if (index < q.count)
				success = sb_playback_select(local_music,
							     (uint32_t)index,
							     context,
							     np_monotonic_ms());
		}
	} else if (!strcmp(name, "pause") &&
		   (cmd->count == 1 ||
		    (cmd->count == 2 && (!strcmp(cmd->arguments[1], "0") ||
					 !strcmp(cmd->arguments[1], "1"))))) {
		success = sb_playback_control(
			local_music,
			(cmd->count == 2 && !strcmp(cmd->arguments[1], "0")) ||
					(cmd->count == 1 &&
					 q.mode == SB_Q_PAUSED)
				? 'u'
				: 'p',
			context,
			np_monotonic_ms());
	} else if (!strcmp(name, "stop") && cmd->count == 1)
		success = sb_playback_control(
			local_music, 'q', context, np_monotonic_ms());
	else if (!strcmp(name, "play") && cmd->count == 1) {
		success = q.mode == SB_Q_PAUSED
				  ? sb_playback_control(local_music,
							'u',
							context,
							np_monotonic_ms())
				  : sb_playback_select(local_music,
						       q.intended,
						       context,
						       np_monotonic_ms());
	}
	snprintf(data,
		 sizeof data,
		 success ? "{\"count\":0}"
			 : "{\"error\":\"unsupported_or_unavailable_music_"
			   "control\"}");
reply:
	if (!music_reply(h, success, data))
		music_overflow(h);
	(void)connection;
	return 2;
}
