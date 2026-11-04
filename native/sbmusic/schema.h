#ifndef SB_MUSIC_SCHEMA_H
#define SB_MUSIC_SCHEMA_H
#define SB_ML_APPLICATION_ID 1396854101
static const char sb_ml_schema[] =
"CREATE TABLE library_meta(id INTEGER PRIMARY KEY CHECK(id=1),revision INTEGER NOT NULL,scan_counter INTEGER NOT NULL DEFAULT 0);"
"INSERT INTO library_meta VALUES(1,1,0);"
"CREATE TABLE media(id INTEGER PRIMARY KEY,source BLOB NOT NULL UNIQUE CHECK(length(source)=16),generation INTEGER NOT NULL,online INTEGER NOT NULL,scan_root TEXT);"
"CREATE TABLE contributors(id INTEGER PRIMARY KEY,media_id INTEGER NOT NULL REFERENCES media(id),name TEXT NOT NULL,UNIQUE(media_id,name));"
"CREATE TABLE genres(id INTEGER PRIMARY KEY,media_id INTEGER NOT NULL REFERENCES media(id),name TEXT NOT NULL,UNIQUE(media_id,name));"
"CREATE TABLE folders(id INTEGER PRIMARY KEY,media_id INTEGER NOT NULL REFERENCES media(id),parent_id INTEGER REFERENCES folders(id),path TEXT NOT NULL,name TEXT NOT NULL,UNIQUE(media_id,path));"
"CREATE INDEX folder_parent ON folders(media_id,parent_id,name COLLATE NOCASE,id);"
"CREATE TABLE albums(id INTEGER PRIMARY KEY,media_id INTEGER NOT NULL REFERENCES media(id),album_key TEXT NOT NULL,title TEXT NOT NULL,artist_id INTEGER REFERENCES contributors(id),compilation INTEGER NOT NULL,year INTEGER NOT NULL,UNIQUE(media_id,album_key));"
"CREATE INDEX album_name ON albums(media_id,title COLLATE NOCASE,id);"
"CREATE TABLE artwork(id INTEGER PRIMARY KEY,media_id INTEGER NOT NULL REFERENCES media(id),reference TEXT NOT NULL,UNIQUE(media_id,reference));"
"CREATE TABLE tracks(id INTEGER PRIMARY KEY AUTOINCREMENT,media_id INTEGER NOT NULL REFERENCES media(id),path TEXT NOT NULL,title TEXT NOT NULL,album_id INTEGER NOT NULL REFERENCES albums(id),folder_id INTEGER NOT NULL REFERENCES folders(id),disc INTEGER NOT NULL,track INTEGER NOT NULL,year INTEGER NOT NULL,duration INTEGER NOT NULL,size INTEGER NOT NULL,mtime INTEGER NOT NULL,codec TEXT NOT NULL,sample_rate INTEGER NOT NULL,channels INTEGER NOT NULL,bitrate INTEGER NOT NULL,audio_offset INTEGER NOT NULL,audio_length INTEGER NOT NULL,artwork_id INTEGER REFERENCES artwork(id),fingerprint TEXT NOT NULL,scan_epoch INTEGER NOT NULL,bit_depth INTEGER NOT NULL DEFAULT 0,track_total INTEGER NOT NULL DEFAULT 0,disc_total INTEGER NOT NULL DEFAULT 0,release_date TEXT NOT NULL DEFAULT '',total_samples INTEGER NOT NULL DEFAULT 0,UNIQUE(media_id,path));"
"CREATE INDEX track_fingerprint ON tracks(media_id,fingerprint);"
"CREATE INDEX track_name ON tracks(media_id,title COLLATE NOCASE,id);"
"CREATE INDEX track_album ON tracks(media_id,album_id,disc,track,id);"
"CREATE INDEX track_folder ON tracks(media_id,folder_id,title COLLATE NOCASE,id);"
"CREATE INDEX track_recent ON tracks(media_id,mtime DESC,id);"
"CREATE TABLE track_contributors(track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,contributor_id INTEGER NOT NULL REFERENCES contributors(id),role INTEGER NOT NULL,ordinal INTEGER NOT NULL,PRIMARY KEY(track_id,contributor_id,role)) WITHOUT ROWID;"
"CREATE INDEX contributor_tracks ON track_contributors(contributor_id,track_id);"
"CREATE TABLE track_genres(track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,genre_id INTEGER NOT NULL REFERENCES genres(id),PRIMARY KEY(track_id,genre_id)) WITHOUT ROWID;"
"CREATE INDEX genre_tracks ON track_genres(genre_id,track_id);";
#endif
