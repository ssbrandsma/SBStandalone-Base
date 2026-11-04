/* Shared asynchronous integration in the existing Base loop. No listener.
 * Included after legacy request_responses so mixed batches execute once. */
struct music_http {
	const char *body;
	size_t length, offset, used, sent, tail_used, tail_sent;
	uint64_t deadline, media_version;
	int sending, event, commands;
	sb_browse_call *call;
	sb_lms_command command;
	np_lms_envelope envelope;
	char output[SB_BROWSE_JSON_MAX], tail[SB_OUTPUT_CAPACITY];
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
	uint64_t playback_operation, playback_context;
#endif
};

static unsigned music_pending, music_pending_peak;

static void music_dispose(client *c)
{
	if (c->music_http) {
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
		sb_playback_cancel(local_music,
				   c->music_http->playback_operation,
				   c->music_http->playback_context);
#endif
		sb_browse_cancel(music_browse, c->music_http->call);
		free(c->music_http);
		c->music_http = NULL;
		if (music_pending)
			music_pending--;
	}
	sb_browse_cancel(music_browse, c->music_sub_call);
	c->music_sub_call = NULL;
	free(c->music_subscription);
	c->music_subscription = NULL;
	c->music_menu_path[0] = c->music_path[0] = 0;
	c->music_menu_version = c->music_view_version = c->music_output_epoch =
		0;
}

static int music_tail(client *c, const char *p, size_t n)
{
	struct music_http *h = c->music_http;
	if (!h || !h->sending)
		return -1;
	if (n > sizeof h->tail - h->tail_used)
		return 0;
	memcpy(h->tail + h->tail_used, p, n);
	h->tail_used += n;
	return 1;
}

static struct music_http *music_allocate(client *c)
{
	if (music_pending == 4)
		return NULL;
	c->music_http = calloc(1, sizeof *c->music_http);
	if (c->music_http) {
		music_pending++;
		if (music_pending > music_pending_peak)
			music_pending_peak = music_pending;
		c->music_http->media_version =
			sb_browse_media_version(music_browse);
		c->music_http->deadline = np_monotonic_ms() + 15000;
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
		c->music_http->playback_context =
			sb_queue_snapshot(sb_playback_queue(local_music))
				.context;
#endif
	}
	return c->music_http;
}

static int music_append(struct music_http *h, const char *fmt, ...)
{
	if (h->used >= sizeof h->output - 128)
		return 0;
	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(h->output + h->used,
			  sizeof h->output - 128 - h->used,
			  fmt,
			  args);
	va_end(args);
	if (n < 0 || (size_t)n >= sizeof h->output - 128 - h->used)
		return 0;
	h->used += (size_t)n;
	return 1;
}

static void music_overflow(struct music_http *h)
{
	h->used = 0;
	music_append(
		h, "[{\"successful\":false,\"error\":\"response_too_large\"}]");
	h->offset = h->length;
}

static void music_send_pump(client *c)
{
	struct music_http *h = c->music_http;
	if (!h || !h->sending)
		return;
	if (h->media_version != sb_browse_media_version(music_browse) ||
	    np_monotonic_ms() >= h->deadline) {
		close_client(c);
		return;
	}
	c->music_output_epoch = h->media_version;
	if (h->sent < h->used) {
		size_t n = h->used - h->sent;
		if (n > SB_OUTPUT_BUDGET)
			n = SB_OUTPUT_BUDGET;
		if (c->output.length + n > SB_OUTPUT_CAPACITY)
			return;
		if (!sb_output_enqueue(&c->output,
				       c->fd,
				       h->output + h->sent,
				       n,
				       np_monotonic_ms())) {
			close_client(c);
			return;
		}
		h->sent += n;
		return;
	}
	if (h->tail_sent < h->tail_used) {
		size_t n = h->tail_used - h->tail_sent;
		if (n > SB_OUTPUT_BUDGET)
			n = SB_OUTPUT_BUDGET;
		if (c->output.length + n > SB_OUTPUT_CAPACITY)
			return;
		if (!sb_output_enqueue(&c->output,
				       c->fd,
				       h->tail + h->tail_sent,
				       n,
				       np_monotonic_ms())) {
			close_client(c);
			return;
		}
		h->tail_sent += n;
		return;
	}
	free(h);
	c->music_http = NULL;
	if (music_pending)
		music_pending--;
	c->used = 0;
	c->keepalive = time(NULL);
}

static int music_event(client *c, const char *data)
{
	if (c->music_http)
		return 0;
	size_t n = strlen(data);
	if (n > SB_BROWSE_JSON_MAX - 32)
		return 0;
	struct music_http *h = music_allocate(c);
	if (!h)
		return 0;
	h->event = 1;
	h->sending = 1;
	h->used = (size_t)snprintf(h->output,
				   sizeof h->output,
				   "%lx\r\n%s\r\n",
				   (unsigned long)n,
				   data);
	music_send_pump(c);
	return 1;
}

static int music_menu_data(int notification, char *out, size_t cap)
{
	char legacy[4096], nodes[4096];
	unsigned count = 0;
	sb_alarms_menu(legacy, sizeof legacy, player_id, 0);
	if (!sb_browse_nodes(music_browse, 0, nodes, sizeof nodes, &count))
		return 0;
	const char *first = strstr(legacy, "\"item_loop\":[");
	if (!first)
		return 0;
	first += 13;
	size_t length = strlen(first);
	if (length < 2)
		return 0;
	length -= 2;
	int n = notification
			? snprintf(out,
				   cap,
				   "[\"menustatus\",[%.*s%s%s],\"add\",\"%s\"]",
				   (int)length,
				   first,
				   count ? "," : "",
				   nodes,
				   player_id[0] ? player_id : "all")
			: snprintf(out,
				   cap,
				   "{\"count\":%u,\"offset\":0,\"item_loop\":[%"
				   ".*s%s%s]}",
				   2 + count,
				   (int)length,
				   first,
				   count ? "," : "",
				   nodes);
	return n >= 0 && (size_t)n < cap;
}

static void music_register(client *request,
			   const np_lms_envelope *e,
			   const sb_lms_command *cmd)
{
	(void)request;
	if (strcmp(e->channel, "/slim/subscribe") || !e->client[0] ||
	    !e->response[0])
		return;
	client *stream = stream_for_id(e->client);
	if (!stream)
		return;
	if (!strcmp(cmd->arguments[0], "menustatus")) {
		snprintf(stream->music_menu_path,
			 sizeof stream->music_menu_path,
			 "%s",
			 e->response);
		stream->music_menu_version = sb_browse_version(music_browse);
		stream->music_menu_visible = sb_browse_available(music_browse);
		return;
	}
	if (!sb_browse_native(cmd) ||
	    strcmp(cmd->arguments[0], "browselibrary"))
		return;
	sb_lms_command *copy = malloc(sizeof *copy);
	if (!copy)
		return;
	*copy = *cmd;
	sb_browse_cancel(music_browse, stream->music_sub_call);
	stream->music_sub_call = NULL;
	free(stream->music_subscription);
	stream->music_subscription = copy;
	snprintf(stream->music_path,
		 sizeof stream->music_path,
		 "%s",
		 e->response);
	stream->music_view_version = sb_browse_version(music_browse);
}

static int music_reply(struct music_http *h, int success, const char *data)
{
	char channel[640];
	np_json_escape(channel, sizeof channel, h->envelope.response);
	const char *id = h->envelope.id;
	return music_append(h,
			    "%s{%s%s%s\"channel\":\"%s\",\"successful\":%s},{%"
			    "s%s%s\"channel\":\"%s\",\"data\":%s}",
			    h->used > 1 ? "," : "",
			    id[0] ? "\"id\":" : "",
			    id,
			    id[0] ? "," : "",
			    h->envelope.channel,
			    success ? "true" : "false",
			    id[0] ? "\"id\":" : "",
			    id,
			    id[0] ? "," : "",
			    channel,
			    data);
}

static void music_http_pump(client *c)
{
	struct music_http *h = c->music_http;
	if (!h)
		return;
	if (h->sending) {
		music_send_pump(c);
		return;
	}
	if (h->media_version != sb_browse_media_version(music_browse)) {
		sb_browse_cancel(music_browse, h->call);
		h->call = NULL;
		h->used = 0;
		h->offset = h->length;
		h->media_version = sb_browse_media_version(music_browse);
		music_append(
			h,
			"[{\"successful\":false,\"error\":\"media_changed\"}]");
	}
	if (h->call) {
		if (!sb_browse_poll(music_browse, h->call))
			return;
		if (!music_reply(h,
				 sb_browse_success(h->call),
				 sb_browse_json(h->call)))
			music_overflow(h);
		if (sb_browse_valid(h->call))
			music_register(c, &h->envelope, &h->command);
		sb_browse_cancel(music_browse, h->call);
		h->call = NULL;
	}
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
	if (h->playback_operation) {
		int result = sb_playback_result(local_music,
						h->playback_operation,
						h->playback_context);
		if (!result)
			return;
		char data[128];
		if (result > 0)
			snprintf(data,
				 sizeof data,
				 "{\"count\":%u}",
				 sb_playback_selection_count(local_music));
		else
			snprintf(data,
				 sizeof data,
				 "{\"error\":\"%s\"}",
				 sb_playback_error(local_music));
		if (!music_reply(h, result > 0, data))
			music_overflow(h);
		h->playback_operation = 0;
	}
#endif
	while (h->offset < h->length) {
		const char *p =
			memchr(h->body + h->offset, '{', h->length - h->offset);
		if (!p) {
			h->offset = h->length;
			break;
		}
		const char *end = message_end(p);
		if (!end || end > h->body + h->length ||
		    (size_t)(end - p) > SB_LMS_MESSAGE_MAX ||
		    ++h->commands > 32) {
			music_overflow(h);
			break;
		}
		h->offset = (size_t)(end - h->body);
		memset(&h->envelope, 0, sizeof h->envelope);
		if (!np_lms_envelope_read(p, (size_t)(end - p), &h->envelope)) {
			music_overflow(h);
			break;
		}
		if (!strcmp(h->envelope.channel, "/slim/unsubscribe")) {
			client *stream = stream_for_id(h->envelope.client);
			if (stream) {
				if (!strcmp(h->envelope.unsubscribe,
					    stream->music_menu_path))
					stream->music_menu_path[0] = 0;
				if (!strcmp(h->envelope.unsubscribe,
					    stream->music_path)) {
					free(stream->music_subscription);
					stream->music_subscription = NULL;
					stream->music_path[0] = 0;
					sb_browse_cancel(
						music_browse,
						stream->music_sub_call);
					stream->music_sub_call = NULL;
				}
			}
		}
		sb_command_result parsed =
			sb_lms_command_parse(p, (size_t)(end - p), &h->command);
#ifdef SB_MUSIC_RUNTIME
  if(music_browse_only && !strcmp(h->command.arguments[0],"playlistcontrol")){
   if(!music_reply(h,0,"{\"error\":\"music_playback_disabled\"}"))music_overflow(h);
   continue;
  }
#endif
#ifdef SB_MUSIC_PLAYBACK_INTEGRATION
		if (local_music && (parsed == SB_COMMAND_OK ||
				    parsed == SB_COMMAND_UNSUPPORTED)) {
			int handled = playback_command(c, h);
			if (handled == 1)
				return;
			if (handled == 2)
				continue;
		}
#endif
		if ((parsed == SB_COMMAND_OK ||
		     parsed == SB_COMMAND_UNSUPPORTED) &&
		    sb_browse_native(&h->command)) {
#ifdef SB_MUSIC_RUNTIME
   if(music_browse_only)fprintf(stderr,"MUSIC_BROWSE %.64s %.64s %.32s %.32s\n",
    h->command.arguments[0],h->command.arguments[1],h->command.arguments[2],h->command.arguments[3]);
#endif
			h->call = sb_browse_begin(
				music_browse, &h->command, h->deadline);
			if (!h->call) {
				if (!music_reply(h,
						 0,
						 "{\"error\":\"browse_busy\"}"))
					music_overflow(h);
				continue;
			}
			if (!sb_browse_poll(music_browse, h->call))
				return;
			if (!music_reply(h,
					 sb_browse_success(h->call),
					 sb_browse_json(h->call)))
				music_overflow(h);
			if (sb_browse_valid(h->call))
				music_register(c, &h->envelope, &h->command);
			sb_browse_cancel(music_browse, h->call);
			h->call = NULL;
			continue;
		}
		if (parsed == SB_COMMAND_OK &&
		    (!strcmp(h->command.arguments[0], "menu") ||
		     !strcmp(h->command.arguments[0], "menustatus"))) {
			char menu[8192];
			if (!music_menu_data(!strcmp(h->command.arguments[0],
						     "menustatus"),
					     menu,
					     sizeof menu) ||
			    !music_reply(h, 1, menu))
				music_overflow(h);
			music_register(c, &h->envelope, &h->command);
			continue;
		}
		/* All unrelated command behavior remains on the original
		 * dispatcher. */
		char message[SB_LMS_MESSAGE_MAX + 1], result[24000];
		size_t n = (size_t)(end - p);
		memcpy(message, p, n);
		message[n] = 0;
		request_responses(
			message, active_config, result, sizeof result);
		n = strlen(result);
		if (n > 2 && !music_append(h,
					   "%s%.*s",
					   h->used > 1 ? "," : "",
					   (int)n - 2,
					   result + 1))
			music_overflow(h);
	}
	if (h->used == 0)
		music_overflow(h);
	else if (h->output[h->used - 1] != ']' || h->used == 1) {
		if (!music_append(h, "]"))
			music_overflow(h);
	}
	/* A short bounded failure-response grace avoids a truncated HTTP error
	 * after the SQL deadline; it never permits more database work. */
	if (np_monotonic_ms() >= h->deadline)
		h->deadline = np_monotonic_ms() + 250;
	char header[256];
	int n = snprintf(header,
			 sizeof header,
			 "HTTP/1.1 200 OK\r\nContent-Type: "
			 "application/json\r\nContent-Length: "
			 "%lu\r\nConnection: keep-alive\r\n\r\n",
			 (unsigned long)h->used);
	/* Header precedes body; subsequent sends to this client are deferred.
	 */
	if (!send_data(c, header, (size_t)n))
		return;
	h->sending = 1;
	music_send_pump(c);
}

static int music_http_begin(client *c, const char *body, size_t length)
{
	if (!music_browse)
		return 0;
	const char *p = body, *end;
	unsigned messages = 0;
	while ((p = memchr(p, '{', length - (size_t)(p - body))) != NULL) {
		end = message_end(p);
		if (!end || end > body + length)
			return 0;
		np_lms_envelope e;
		if (!np_lms_envelope_read(p, (size_t)(end - p), &e))
			return 0;
		if (strcmp(e.channel, "/slim/request") &&
		    strcmp(e.channel, "/slim/subscribe") &&
		    strcmp(e.channel, "/slim/unsubscribe"))
			return 0;
		if (strcmp(e.channel, "/slim/unsubscribe") &&
		    (!e.response[0] || e.response[0] != '/')) {
			http_reply(c,
				   "400 Bad Request",
				   "{\"error\":\"invalid_response_channel\"}",
				   0);
			return 1;
		}
		if (++messages > 32) {
			http_reply(c,
				   "413 Payload Too Large",
				   "{\"error\":\"too_many_commands\"}",
				   0);
			return 1;
		}
		p = end;
		if (p == body + length)
			break;
	}
	if (!messages)
		return 0;
	struct music_http *h = music_allocate(c);
	if (!h) {
		http_reply(c,
			   "503 Service Unavailable",
			   "{\"error\":\"browse_busy\"}",
			   0);
		return 1;
	}
	h->body = body;
	h->length = length;
	h->deadline = np_monotonic_ms() + 5000;
	h->output[0] = '[';
	h->used = 1;
	music_http_pump(c);
	return 1;
}

static void music_inherit(client *c, client *old)
{
	memcpy(c->music_menu_path,
	       old->music_menu_path,
	       sizeof c->music_menu_path);
	memcpy(c->music_path, old->music_path, sizeof c->music_path);
	c->music_subscription = old->music_subscription;
	old->music_subscription = NULL;
	c->music_menu_version = c->music_view_version = c->music_output_epoch =
		0;
	c->music_menu_visible = old->music_menu_visible;
}

static void music_events(void)
{
	if (!music_browse)
		return;
	sb_browse_tick(music_browse, np_monotonic_ms());
	uint64_t version = sb_browse_version(music_browse);
	for (unsigned i = 0; i < CLIENTS; i++) {
		client *c = &clients[i];
		if (c->fd < 0)
			continue;
		if (c->output.length && c->music_output_epoch &&
		    c->music_output_epoch !=
			    sb_browse_media_version(music_browse)) {
			close_client(c);
			continue;
		}
		if (c->music_http) {
			music_http_pump(c);
			continue;
		}
		if (c->kind != 3)
			continue;
		if (c->music_menu_path[0] && c->music_menu_version != version) {
			char nodes[4096], event[8192], path[640];
			unsigned count;
			int visible = sb_browse_available(music_browse);
			if (visible != c->music_menu_visible ||
			    c->music_menu_version == 0) {
				sb_browse_nodes(music_browse,
						!visible,
						nodes,
						sizeof nodes,
						&count);
				np_json_escape(
					path, sizeof path, c->music_menu_path);
				snprintf(event,
					 sizeof event,
					 "[{\"channel\":\"%s\",\"data\":["
					 "\"menustatus\",[%s],\"%s\",\"%s\"]}]",
					 path,
					 nodes,
					 visible ? "add" : "remove",
					 player_id[0] ? player_id : "all");
				if (!music_event(c, event))
					continue;
			}
			c->music_menu_version = version;
			c->music_menu_visible = visible;
		}
		if (c->music_http || !c->music_subscription)
			continue;
		if (c->music_view_version != version && !c->music_sub_call) {
			if (music_pending >= 4)
				continue;
			c->music_sub_call =
				sb_browse_begin(music_browse,
						c->music_subscription,
						np_monotonic_ms() + 5000);
		}
		if (c->music_sub_call &&
		    sb_browse_poll(music_browse, c->music_sub_call)) {
			const char *data = sb_browse_json(c->music_sub_call);
			char path[640];
			np_json_escape(path, sizeof path, c->music_path);
			struct music_http *h = music_allocate(c);
			if (!h)
				continue;
			h->used = 0;
			if (!music_append(h,
					  "[{\"channel\":\"%s\",\"data\":%s}]",
					  path,
					  data)) {
				music_dispose(c);
				close_client(c);
				continue;
			}
			size_t n = h->used;
			char prefix[32];
			int prefix_n = snprintf(prefix,
						sizeof prefix,
						"%lx\r\n",
						(unsigned long)n);
			memmove(h->output + prefix_n, h->output, n);
			memcpy(h->output, prefix, (size_t)prefix_n);
			h->used = n + (size_t)prefix_n;
			memcpy(h->output + h->used, "\r\n", 2);
			h->used += 2;
			h->sending = h->event = 1;
			sb_browse_cancel(music_browse, c->music_sub_call);
			c->music_sub_call = NULL;
			c->music_view_version = version;
			music_send_pump(c);
		}
	}
}
