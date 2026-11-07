"""Real native worker/scanner/snapshot tests, isolated host media only."""
import hashlib
import json
import os
import signal
import time
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from wp2_fixtures import mp3, compact_flac

binary, helper = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix='sb-lifecycle-', dir='/dev/shm') as temporary:
    root=Path(temporary);store=root/'.standalone-music'
    def run(mode, success=True):
        p=subprocess.run([binary,mode,str(root),'.standalone-music',helper,'4'],capture_output=True,text=True,timeout=30)
        assert (p.returncode==0)==success,(mode,p.returncode,p.stdout,p.stderr)
        return json.loads(p.stdout) if success else p
    def slots():
        return {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in store.glob('*.snapshot')}
    def database():
        candidates=[]
        for p in store.glob('*.snapshot'):
            raw=p.read_bytes();header=raw[:192].split(b'\0',1)[0].decode().split()
            if len(header)==5 and hashlib.sha256(raw[192:]).hexdigest()==header[3]:candidates.append((int(header[1]),raw[192:]))
        raw=max(candidates)[1]
        with tempfile.NamedTemporaryFile() as f:
            f.write(raw);f.flush()
            with sqlite3.connect('file:'+f.name+'?mode=ro',uri=True) as db:
                assert db.execute('PRAGMA integrity_check').fetchone()==('ok',)
                return dict(rows=db.execute('SELECT id,path,title FROM tracks ORDER BY id').fetchall(),
                    rejected=db.execute('SELECT path,code,previous_track_id FROM rejected_files ORDER BY path').fetchall(),
                    albums=db.execute('SELECT id,album_key FROM albums ORDER BY id').fetchall(),
                    artists=db.execute('SELECT id,name FROM contributors ORDER BY id').fetchall(),
                    genres=db.execute('SELECT id,name FROM genres ORDER BY id').fetchall(),
                    folders=db.execute('SELECT id,path FROM folders ORDER BY id').fetchall(),
                    revision=db.execute('SELECT revision FROM library_meta').fetchone()[0],schema=db.execute('PRAGMA user_version').fetchone()[0])
    run('open',False)
    first=run('init');assert first['tracks']==0 and first['snapshot_written']
    backed=run('backup');assert backed['snapshot_written'] and backed['tracks']==0
    unchanged=slots();run('init',False);assert unchanged==slots()
    (root/'nested').mkdir();(root/'nested'/'één.mp3').write_bytes(mp3(4))
    (root/'second.flac').write_bytes(compact_flac(True));(root/'escape').symlink_to('/etc',target_is_directory=True)
    full=run('scan');assert full['tracks']==2 and full['added']==2,full
    before=database();snapshots=slots()
    reopened=run('open');assert not reopened['scan'] and not reopened['snapshot_written']
    assert snapshots==slots() and before==database()
    nochange=run('scan');assert nochange['unchanged']==2 and nochange['updated']==0 and not nochange['snapshot_written'],nochange
    assert nochange['metadata_parses']==0 and nochange['track_updates']==0
    # Two zero-effect reconciliation statements (tracks, rejected_files); no
    # row churn. The second DELETE arrived with durable rejection records.
    assert nochange['sql_inserts']==0 and nochange['sql_updates']==2 and nochange['sql_deletes']==2
    assert nochange['row_inserts']==0 and nochange['row_deletes']==0
    assert nochange['snapshot_bytes_written']==0
    assert snapshots==slots() and before==database()
    job=Path('/dev/shm/sbmusic-library-job');assert not job.exists()
    detached=run('scan-detached');os.kill(detached['pid'],signal.SIGHUP)
    try:
        until=time.monotonic()+15
        while time.monotonic()<until:
            status=(job/'exit').read_text() if (job/'exit').exists() else ''
            if status.strip():break
            time.sleep(.02)
        assert status.strip()=='0',(status,(job/'scan.log').read_text())
        assert json.loads((job/'result.json').read_text())['unchanged']==2
        assert snapshots==slots()
    finally:
        for name in ('exit','scan.log','result.json'):(job/name).unlink()
        job.rmdir()
    old=before['rows'][0];source=root/old[1];renamed=source.with_name('renamed'+source.suffix);source.rename(renamed)
    rename=run('scan');assert rename['tracks']==2 and rename['updated']==1,rename
    assert any(r[0]==old[0] and r[1].endswith(renamed.name) for r in database()['rows'])
    renamed.unlink();removed=run('scan');assert removed['removed']==1 and removed['tracks']==1,removed
    if old[1].startswith('nested/'):
        assert not any(row[1]=='nested' for row in database()['folders'])
    restored=run('restore');assert restored['tracks']==2 and restored['snapshot_written']
    # The real process holds an exclusive directory flock while scanning. An
    # overlapping operation refuses, and SIGTERM cancels without publishing.
    cancellation=root/'cancellation';cancellation.mkdir()
    for i in range(30):(cancellation/f'{i}.mp3').write_bytes(mp3(4))
    saved=slots()
    process=subprocess.Popen([binary,'scan',str(root),'.standalone-music',helper,'4'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    assert process.stderr.readline().startswith('SCAN ')
    run('open',False)
    process.terminate();stdout,stderr=process.communicate(timeout=10)
    assert process.returncode==2 and saved==slots(),(stdout,stderr)
    assert not job.exists();detached=run('scan-detached')
    try:
        (job/'cancel').write_bytes(b'');(job/'cancel').chmod(0o600)
        until=time.monotonic()+10;status=''
        while time.monotonic()<until:
            status=(job/'exit').read_text()
            if status.strip():break
            time.sleep(.02)
        assert status.strip()=='2' and saved==slots(),status
    finally:
        for name in ('cancel','exit','scan.log','result.json'):(job/name).unlink()
        job.rmdir()
    for p in cancellation.iterdir():p.unlink()
    cancellation.rmdir()
    # One malformed file is a recoverable per-file rejection: the scan
    # completes, publishes the valid library, and records the rejection
    # durably without fabricating a track.
    (root/'broken.mp3').write_bytes(b'ID3\xff\xff')
    broken=run('scan')
    assert broken['scan_state']==4 and broken['errors']==1 and broken['recoverable']==1 and broken['fatal']==0,broken
    assert broken['snapshot_written']
    after=database()
    # Complete traversal also reconciles renamed.flac, whose file has been
    # missing since the removal step and only survived via snapshot restore.
    assert [row[1] for row in after['rows']]==['nested/één.mp3']
    assert len(after['rejected'])==1
    assert after['rejected'][0][:2]==('broken.mp3',2),after['rejected']
    (root/'broken.mp3').unlink()
    valid=slots()
    (store/'library.pending').write_bytes(b'partial');run('open');assert valid==slots()
    (root/'new.mp3').write_bytes(mp3(3));recovered=run('scan')
    assert recovered['snapshot_written'] and (store/'library.interrupted.0').read_bytes()==b'partial'
    assert not (store/'library.pending').exists()
    (root/'new.mp3').unlink();run('scan')
    paths=sorted(store.glob('*.snapshot'),key=lambda p:int(p.read_bytes()[:192].split()[1]))
    latest=paths[-1];original_latest=latest.read_bytes();raw=bytearray(original_latest);raw[-1]^=255;latest.write_bytes(raw)
    corrupt=slots();degraded=run('open');assert degraded['snapshot_degraded'] and corrupt==slots()
    (root/'new.mp3').write_bytes(mp3(3));run('scan',False);assert corrupt==slots()
    recovered=run('recover');assert recovered['snapshot_written'] and not recovered['snapshot_degraded']
    assert (store/'library.interrupted.1').read_bytes()==bytes(raw)
    preserved=root/'saved';store.rename(preserved);store.symlink_to(preserved,target_is_directory=True)
    run('open',False);store.unlink();preserved.rename(store)
    (store/'unrelated.txt').write_text('preserve');run('open',False)
    assert (store/'unrelated.txt').read_text()=='preserve'
    (store/'unrelated.txt').unlink()
    # A checksum-valid envelope is insufficient: the worker must also reject
    # another medium's SQL rows, without registering a second source.
    for snapshot in store.glob('*.snapshot'):
        raw=snapshot.read_bytes();header=raw[:192].split(b'\0',1)[0].decode().split()
        with tempfile.NamedTemporaryFile() as f:
            f.write(raw[192:]);f.flush()
            with sqlite3.connect(f.name) as database_file:
                database_file.execute('UPDATE media SET source=?',(bytes([99])*16,))
            payload=Path(f.name).read_bytes()
        header[2]=str(len(payload));header[3]=hashlib.sha256(payload).hexdigest()
        text=(' '.join(header)+'\n').encode();snapshot.write_bytes(text+bytes(192-len(text))+payload)
    foreign=slots();run('open',False);run('scan',False);assert foreign==slots()
    print(json.dumps(dict(lifecycle='passed',full=full,reopen=reopened,unchanged=nochange)))
