/* Fixture-only root setup and mock routes. */
static int playback_start(const char *directory, int ipc, unsigned port)
{
	char canonical[4096], marker[4096], proof[32] = {0};
	if (!realpath(directory, canonical) || strcmp(directory, canonical) ||
	    strncmp(directory, "/dev/shm/sb-music-playback-", 27))
		return 0;
	if (snprintf(marker, sizeof marker, "%s/WP4_TEST_FIXTURE", directory) >=
	    (int)sizeof marker)
		return 0;
	int fd = open(marker, O_RDONLY | O_NOFOLLOW);
	struct stat st;
	int regular = fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode);
	ssize_t bytes = regular ? read(fd, proof, sizeof proof - 1) : -1;
	if (fd >= 0)
		close(fd);
	if (bytes != 23 || strcmp(proof, "WP4 disposable fixture\n"))
		return 0;
	sb_storage_mounts *mounts = malloc(sizeof *mounts);
	if (!mounts)
		return 0;
	if (sb_storage_read_mounts(NULL, mounts) != SB_STORAGE_OK) {
		free(mounts);
		return 0;
	}
	sb_storage_mount mount = {0};
	for (unsigned i = 0; i < mounts->count; i++)
		if (!strcmp(mounts->rows[i].path, "/dev/shm"))
			mount = mounts->rows[i];
	free(mounts);
	sb_storage_root whole, root;
	if (!mount.mount_id || sb_storage_validate("/dev",
						   &mount,
						   &music_media_snapshot,
						   NULL,
						   NULL,
						   &whole) != SB_STORAGE_OK)
		return 0;
	sb_storage_status scoped = sb_storage_root_child(&whole,
							 &music_media_snapshot,
							 directory + 9,
							 NULL,
							 NULL,
							 &root);
	sb_storage_root_close(&whole);
	if (scoped != SB_STORAGE_OK)
		return 0;
	sb_owner_init(&local_owner, 1);
	for (unsigned who = SB_OWNER_RADIO; who <= SB_OWNER_MUSIC; who++)
		if (!sb_owner_register(
			    &local_owner, (sb_music_owner)who, 100 + who)) {
			sb_storage_root_close(&root);
			return 0;
		}
	local_music = sb_playback_create(1,
					 sb_browse_dispatcher(music_browse),
					 &root,
					 &local_owner,
					 nowplaying,
					 ipc,
					 port,
					 playback_send,
					 NULL);
	sb_storage_root_close(&root);
	return local_music != NULL;
}

static int playback_test_route(client *c, const char *path)
{
	if (strncmp(path, "/test/music/adapter/", 20) &&
	    strcmp(path, "/test/music/owner"))
		return 0;
	if (!local_mock_adapters)
		return 0;
	int ok = 1;
	sb_music_ownership *o = &local_owner.value;
	if (!strcmp(path, "/test/music/adapter/idle")) {
		ok = sb_playback_idle_proof(
			local_music,
			local_owner.registrations[SB_OWNER_MUSIC],
			o->connection_generation,
			o->generation,
			np_monotonic_ms(),
			1,
			1,
			1);
	} else if (!strcmp(path, "/test/music/adapter/yield/accept") ||
		   !strcmp(path, "/test/music/adapter/yield/refuse")) {
		int accept = strstr(path, "/accept") != NULL;
		ok = o->current != SB_OWNER_NONE &&
		     sb_owner_ack(&local_owner,
				  o->current,
				  local_owner.registrations[o->current],
				  o->request_id,
				  o->generation,
				  o->connection_generation,
				  np_monotonic_ms(),
				  accept,
				  accept,
				  accept,
				  accept);
	} else if (!strncmp(path, "/test/music/adapter/owner/", 26)) {
		const char *name = path + 26;
		sb_music_owner who = !strcmp(name, "radio") ? SB_OWNER_RADIO
				     : !strcmp(name, "spotify")
					     ? SB_OWNER_SPOTIFY
				     : !strcmp(name, "qobuz") ? SB_OWNER_QOBUZ
							      : SB_OWNER_NONE;
		ok = who != SB_OWNER_NONE &&
		     sb_owner_request(&local_owner,
				      who,
				      local_owner.last_request + 1,
				      np_monotonic_ms(),
				      10000);
	} else if (strcmp(path, "/test/music/owner"))
		ok = 0;
	sb_queue_status q = sb_queue_snapshot(sb_playback_queue(local_music));
	char data[768];
	snprintf(data,
		 sizeof data,
		 "{\"ok\":%s,\"owner\":%u,\"requested\":%u,\"generation\":"
		 "%" PRIu64 ",\"connection\":%" PRIu64
		 ",\"yield\":%u,\"context\":%" PRIu64
		 ",\"phase\":%u,\"count\":%u,"
		 "\"intended\":%u,\"audible\":%u,\"epoch\":%" PRIu64
		 ",\"error\":\"%s\"}",
		 ok ? "true" : "false",
		 o->current,
		 o->requested,
		 o->generation,
		 o->connection_generation,
		 o->yield,
		 q.context,
		 sb_playback_phase(local_music),
		 q.count,
		 q.intended,
		 q.audible,
		 q.stream_epoch,
		 sb_playback_error(local_music));
	http_reply(c, ok ? "200 OK" : "409 Conflict", data, 0);
	return 1;
}
