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
                    "SELECT id FROM media WHERE ? IS NOT NULL AND id=? AND (scan_root=? OR "
                    "(scan_root IS NULL AND NOT EXISTS(SELECT 1 FROM tracks WHERE "
                    "media_id=media.id)))",
                    scope, 3, &bound)) {
            if (h->result.status == SB_ML_NOT_FOUND)
                failure(h, SB_ML_CONFLICT, "Source already bound to another scan root");
            return;
        }
        if (!execute(h, "UPDATE media SET scan_root=? WHERE id=? AND scan_root IS NULL", scope, 2) ||
            !script(h, "CREATE TEMP TABLE IF NOT EXISTS scan_seen(id INTEGER PRIMARY KEY);DELETE FROM temp.scan_seen;CREATE TEMP TABLE IF NOT EXISTS reject_seen(path TEXT PRIMARY KEY) WITHOUT ROWID;DELETE FROM temp.reject_seen;"))
            return;
        sqlite3_int64 epoch;
        if (!scalar(h,
                    "UPDATE library_meta SET scan_counter=scan_counter+1 WHERE id=1 AND "
                    "scan_counter<9223372036854775807 RETURNING scan_counter",
                    NULL, 0, &epoch))
            return;
        h->scan_epoch = (uint64_t)epoch;
        h->scan_start_revision=h->revision;
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
        /* Scan membership is connection-local; never stamp every persistent row. */
        binding seen[] = {I(id)};
        sqlite3_int64 present = 0;
        if (h->result.status == SB_ML_OK &&
            scalar(h, "SELECT count(*) FROM temp.scan_seen WHERE id=?", seen, 1, &present))
            h->result.data.track.scan_epoch = present ? h->scan_epoch : 0;
        h->result.record_id = (uint64_t)id;
        return;
    }
    if (h->options.read_only) {
        failure(h, SB_ML_READ_ONLY, "Library is read-only");
        return;
    }
    if(r->operation==SB_ML_SCAN_REJECT_LOOKUP||r->operation==SB_ML_SCAN_REJECT){
        const sb_ml_rejection *e=&r->data.rejection;
        if(!sb_music_source_equal(&e->media.source,&h->scan_media.source)||e->media.generation!=h->scan_media.generation||
           !text_valid(e->path,sizeof e->path)||!e->path[0]||!text_valid(e->fingerprint,sizeof e->fingerprint)){
            failure(h,SB_ML_INVALID,"Invalid rejected-file identity");return;
        }
        binding key[]={I(media->id),T(e->path)};
        if(r->operation==SB_ML_SCAN_REJECT_LOOKUP){
            sqlite3_stmt *s=prepare(h,"SELECT fingerprint,size,mtime,code,message,previous_track_id FROM rejected_files WHERE media_id=? AND path=?",key,2);
            if(!s)return;int rc=sqlite3_step(s);
            if(rc==SQLITE_ROW){
                sb_ml_rejection *out=&h->result.data.rejection;*out=*e;
                column_text(s,0,out->fingerprint,sizeof out->fingerprint);out->file_size=(uint64_t)sqlite3_column_int64(s,1);
                out->mtime=sqlite3_column_int64(s,2);out->code=(unsigned)sqlite3_column_int(s,3);
                column_text(s,4,out->message,sizeof out->message);out->previous_track_id=(uint64_t)sqlite3_column_int64(s,5);
            }
            sqlite3_finalize(s);
            if(rc==SQLITE_DONE){failure(h,SB_ML_NOT_FOUND,"No rejected-file record");return;}
            if(rc!=SQLITE_ROW){sql_error(h,rc);return;}
            binding seen[]={T(e->path)};execute(h,"INSERT OR IGNORE INTO temp.reject_seen VALUES(?)",seen,1);return;
        }
        if(h->transaction||e->code<1||e->code>3||e->file_size>INT64_MAX||e->previous_track_id>INT64_MAX||
           !text_valid(e->message,sizeof e->message)){failure(h,SB_ML_INVALID,"Invalid rejected-file diagnostic");return;}
        if(!execute(h,"BEGIN IMMEDIATE",NULL,0))return;
        binding v[]={I(media->id),T(e->path),T(e->fingerprint),I(e->file_size),I(e->mtime),I(e->code),T(e->message),I(e->previous_track_id)};
        int ok=execute(h,"INSERT INTO rejected_files VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(media_id,path) DO UPDATE SET fingerprint=excluded.fingerprint,size=excluded.size,mtime=excluded.mtime,code=excluded.code,message=excluded.message,previous_track_id=excluded.previous_track_id WHERE fingerprint!=excluded.fingerprint OR size!=excluded.size OR mtime!=excluded.mtime OR code!=excluded.code OR message!=excluded.message OR previous_track_id!=excluded.previous_track_id",v,8);
        int changed=ok&&sqlite3_changes(h->db);
        binding seen[]={T(e->path)};
        if(ok)ok=execute(h,"INSERT OR IGNORE INTO temp.reject_seen VALUES(?)",seen,1);
        if(ok)ok=(!changed||bump_revision(h))&&execute(h,"COMMIT",NULL,0);
        if(!ok)rollback_preserving_error(h);else if(changed)revision_committed(h);
        return;
    }
    if (r->operation == SB_ML_SCAN_TOUCH) {
        if (h->transaction && h->transaction != r->transaction) {
            failure(h, SB_ML_CONFLICT, "Transaction owner mismatch");
            return;
        }
        binding v[] = {I(media->id), I(t->identity.track_id)};
        if (execute(h, "INSERT OR REPLACE INTO temp.scan_seen SELECT id FROM tracks WHERE media_id=? AND id=?", v, 2) &&
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
        binding v[] = {I(media->id)};
        int ok = scan_current(h, r) &&
                 execute(h, "DELETE FROM tracks WHERE media_id=? AND id NOT IN (SELECT id FROM temp.scan_seen)", v, 1);
        uint64_t removed = ok ? (uint64_t)sqlite3_changes(h->db) : 0;
        uint64_t pruned=0,rejected_removed=0;
        if(ok){ok=execute(h,"DELETE FROM rejected_files WHERE media_id=? AND path NOT IN (SELECT path FROM temp.reject_seen)",v,1);if(ok)rejected_removed=(uint64_t)sqlite3_changes(h->db);}
        if(ok&&(removed||h->revision!=h->scan_start_revision)) {
            /* Only a complete traversal may retire empty music-folder nodes.
             * Bottom-up bounded passes preserve occupied ancestors and root. */
            for(unsigned depth=0;depth<16;depth++) {
                ok=execute(h,"DELETE FROM folders WHERE media_id=? AND path!='' AND NOT EXISTS(SELECT 1 FROM tracks t WHERE t.media_id=folders.media_id AND t.folder_id=folders.id) AND NOT EXISTS(SELECT 1 FROM folders c WHERE c.media_id=folders.media_id AND c.parent_id=folders.id)",v,1);
                if(!ok)break;
                unsigned changed=(unsigned)sqlite3_changes(h->db);pruned+=changed;
                if(!changed)break;
            }
        }
        if (ok && progress(h)) {
            failure(h, SB_ML_CANCELLED, "Scan finish cancelled before commit");
            ok = 0;
        }
        if (ok)
            ok = (!(removed||pruned||rejected_removed) || bump_revision(h)) && execute(h, "COMMIT", NULL, 0);
        if (!ok) {
            rollback_preserving_error(h);
            return;
        }
        if (removed||pruned||rejected_removed)
            revision_committed(h);
        h->result.record_id = removed;
        h->scan_epoch = 0;
        return;
    }
    failure(h, SB_ML_INVALID, "Unknown scan operation");
}
