# WP4 native queue and asynchronous selection

Implementation: [music_queue.h](../native/sbbase/include/music_queue.h), [music_queue.c](../native/sbbase/src/music_queue.c), [music_load.h](../native/sbbase/include/music_load.h), [music_load.c](../native/sbbase/src/music_load.c), [coordinator](../native/sbbase/src/music_playback.c).

## Model and bounds

The configured queue limit is256, deliberately below WP0's10000-entry contract ceiling. One allocation holds256 committed and256 staged records. Each record contains stable entry ID, source-qualified indexed track ID, mount generation, creation/availability revision, bounded128-byte title/artist/album projections, duration, codec and available/eligible flags. The snapshot contains queue revision, current controller context, physical owner generation, stream epoch, count, intended index, confirmed audible index, playback mode and confirmed elapsed time. UINT32_MAX means no index.

A process-incarnation context is derived from32 bytes of OS entropy at construction and incremented on unrelated physical-owner transitions. Row IDs and action context fields are serialized as strings. A new instance does not inherit old request/capability authority. IDs/epochs/revisions never deliberately wrap. Stable entry IDs distinguish repeated copies of the same track in the queue.

Queue mutations are single-control-thread operations without SQL, filesystem access or decoder calls. Load/add/insert first capture context and queue revision; at most one edit is pending. Each stage contains at most50 records. Commit validates the captured context/revision, complete selection, eligibility and bounds, then changes the committed array and revision once. Any failed stage poisons that edit; abort leaves the old array unchanged. Oversized albums fail before committing partial contents. Selection/remove/clear/revocation are bounded O(256). Insert means after the intended current index; additions never start playback.

## Database ingestion

The loader shares WP3b's existing async dispatcher, WP1 broker and single SQLite connection. DETAIL is added to the dispatcher's allowed read operations; no second consumer attaches to the raw completion pipe. Albums use50-row BROWSE pages in disc/track/stable-ID order, then bounded DETAIL requests. The first result pins library revision; every subsequent result must match. Generation/source/track identity are checked before staging. A revision change, cancellation, changed context, stale source or five-second read deadline rejects the staged operation.

The first complete track record is retained for the asynchronous secure open; subsequent queue entries retain bounded presentation data and indexed identity. Metadata remains authoritative in WP1. Queue artist presentation currently uses the first contributor; the library retains the complete contributor list. MP3/FLAC are eligible; indexed Ogg is rejected for queue ingestion and never advertised as playable.

## Playback interaction

Selecting an entry changes intent, not audible metadata. Only matching STMs confirmation binds the active stream's stable entry ID to the audible index. Changing intent cannot re-label an earlier stream's STMs. Pause/stop/disconnect never extrapolate elapsed time. Successful drain selects the next intent once and enters WAIT_CONNECTION; a fresh connection and explicit physical proof are still required. Unsupported/error/duplicate/stale terminal events do not advance.

Media revocation cancels pending edits, retains committed entries with availability/eligibility cleared, and invalidates the active stream. Reinsertion does not restore eligibility or auto-play. A controller must explicitly obtain a newly qualified root before a new instance/selection can use it; WP4 does not implement WP6b eject/rebinding orchestration or persistent queue restoration.

## Jive commands

The test integration supports `playlistcontrol cmd:load|add|insert track_id:N|album_id:N`, `playlist index +1|-1|N`, `playlist clear`, `playlist delete N`, `play`, `pause [0|1]`, and `stop`. Absolute numeric index remains absolute for Music; external-owner commands keep the original dispatcher. Load/add/insert return the number of selected tracks, following LMS7.7.3 `Commands::playlistcontrolCommand` (1746 and its final count result), rather than always returning1.

Play/Add/Insert actions are emitted only by the explicitly constructed test stack and only when indexed format masks fit negotiated MP3/FLAC capabilities. Artist/genre/folder navigation remains browse-only. Actions carry a controller context; delayed old actions fail after a physical-owner change. HTTP disconnect cancels an uncommitted operation; it does not undo an already accepted start. Repeated presses during an outstanding action receive a bounded busy/unavailable result.
