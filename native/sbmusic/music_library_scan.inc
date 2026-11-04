/* WP2 operations executed exclusively on the existing WP1 worker/connection. */
static int scan_current(sb_music_library *h, const sb_ml_request *r) {
    if (!r->scan_token || r->scan_token != h->scan_epoch ||
        !media_find(h, &h->scan_media.source, h->scan_media.generation)) {
        failure(h, SB_ML_STALE, "Scan token/media no longer current");
        return 0;
    }
    sqlite3_int64 epoch = 0;
    if (!scalar(h, "SELECT scan_counter FROM library_meta WHERE id=1", NULL, 0, &epoch))
        return 0;
    if ((uint64_t)epoch != r->scan_token) {
        failure(h, SB_ML_STALE, "Another scan superseded this epoch");
        return 0;
    }
    return 1;
}
static void scan_request(sb_music_library *h) {
    const sb_ml_request *r = &h->request;
    const sb_ml_track *t = &r->data.track;
    if (r->operation == SB_ML_SCAN_START) {
        if (h->options.read_only) {
            failure(h, SB_ML_READ_ONLY, "Scan ingestion needs an approved writable library");
            return;
        }
        if (h->transaction || h->scan_epoch) {
            failure(h, SB_ML_BUSY, "A scan or transaction already owns this handle");
            return;
        }
        media_slot *media = media_find(h, &t->identity.source, t->media_generation);
        if (!media)
            return;
        if (!text_valid(t->path, sizeof t->path)) {
            failure(h, SB_ML_INVALID, "Invalid scan root scope");
            return;
        }
        binding scope[] = {T(t->path), I(media->id), T(t->path)};
        sqlite3_int64 bound;
        if (!scalar(h,
                    "UPDATE media SET scan_root=? WHERE id=? AND (scan_root=? OR "
                    "(scan_root IS NULL AND NOT EXISTS(SELECT 1 FROM tracks WHERE "
                    "media_id=media.id))) RETURNING id",
                    scope, 3, &bound)) {
            if (h->result.status == SB_ML_NOT_FOUND)
                failure(h, SB_ML_CONFLICT, "Source already bound to another scan root");
            return;
        }
        sqlite3_int64 epoch;
        if (!scalar(h,
                    "UPDATE library_meta SET scan_counter=scan_counter+1 WHERE id=1 AND "
                    "scan_counter<9223372036854775807 RETURNING scan_counter",
                    NULL, 0, &epoch))
            return;
        h->scan_epoch = (uint64_t)epoch;
        h->scan_media = (sb_music_media){t->identity.source, t->media_generation, 1};
        h->result.record_id = h->scan_epoch;
        return;
    }
    if (r->operation == SB_ML_SCAN_ABORT) {
        if (r->scan_token != h->scan_epoch) {
            failure(h, SB_ML_STALE, "Stale scan abort");
            return;
        }
        if (h->transaction)
            rollback_preserving_error(h);
        h->scan_epoch = 0;
        return;
    }
    if (!scan_current(h, r))
        return;
    media_slot *media = media_find(h, &h->scan_media.source, h->scan_media.generation);
    if (!media)
        return;
    if (r->operation == SB_ML_SCAN_LOOKUP) {
        if (!text_valid(t->path, sizeof t->path) ||
            !text_valid(t->fingerprint, sizeof t->fingerprint)) {
            failure(h, SB_ML_INVALID, "Invalid scan lookup");
            return;
        }
        binding v[] = {I(media->id), T(t->path[0] ? t->path : t->fingerprint)};
        sqlite3_stmt *s =
            prepare(h,
                    t->path[0] ? "SELECT id FROM tracks WHERE media_id=? AND path=?"
                               : "SELECT id FROM tracks WHERE media_id=? AND fingerprint=? LIMIT 2",
                    v, 2);
        if (!s)
            return;
        int rc = sqlite3_step(s);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            sqlite3_finalize(s);
            sql_error(h, rc);
            return;
        }
        sqlite3_int64 id = rc == SQLITE_ROW ? sqlite3_column_int64(s, 0) : 0;
        int next = rc == SQLITE_ROW ? sqlite3_step(s) : SQLITE_DONE;
        if (next != SQLITE_ROW && next != SQLITE_DONE) {
            sqlite3_finalize(s);
            sql_error(h, next);
            return;
        }
        if (next == SQLITE_ROW) {
            sqlite3_finalize(s);
            failure(h, SB_ML_CONFLICT, "Fingerprint is ambiguous");
            return;
        }
        sqlite3_finalize(s);
        if (!id) {
            failure(h, SB_ML_NOT_FOUND, "File not indexed");
            return;
        }
        sb_music_track_id identity = {h->scan_media.source, (uint64_t)id};
        track_detail(h, &identity, h->scan_media.generation);
        h->result.record_id = (uint64_t)id;
        return;
    }
    if (h->options.read_only) {
        failure(h, SB_ML_READ_ONLY, "Library is read-only");
        return;
    }
    if (r->operation == SB_ML_SCAN_TOUCH) {
        if (h->transaction && h->transaction != r->transaction) {
            failure(h, SB_ML_CONFLICT, "Transaction owner mismatch");
            return;
        }
        binding v[] = {I(r->scan_token), I(media->id), I(t->identity.track_id)};
        if (execute(h, "UPDATE tracks SET scan_epoch=? WHERE media_id=? AND id=?", v, 3) &&
            !sqlite3_changes(h->db))
            failure(h, SB_ML_STALE, "Track disappeared before scan stamp");
        return;
    }
    if (r->operation == SB_ML_SCAN_FINISH) {
        if (h->transaction) {
            failure(h, SB_ML_CONFLICT, "Finish requires committed ingestion batches");
            return;
        }
        if (!execute(h, "BEGIN IMMEDIATE", NULL, 0))
            return;
        binding v[] = {I(media->id), I(r->scan_token)};
        int ok = scan_current(h, r) &&
                 execute(h, "DELETE FROM tracks WHERE media_id=? AND scan_epoch!=?", v, 2);
        uint64_t removed = ok ? (uint64_t)sqlite3_changes(h->db) : 0;
        if (ok && progress(h)) {
            failure(h, SB_ML_CANCELLED, "Scan finish cancelled before commit");
            ok = 0;
        }
        if (ok)
            ok = (!removed || bump_revision(h)) && execute(h, "COMMIT", NULL, 0);
        if (!ok) {
            rollback_preserving_error(h);
            return;
        }
        if (removed)
            revision_committed(h);
        h->result.record_id = removed;
        h->scan_epoch = 0;
        return;
    }
    failure(h, SB_ML_INVALID, "Unknown scan operation");
}
