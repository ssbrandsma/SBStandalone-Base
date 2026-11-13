"""Native Jive wire integration with disposable fixtures; no device/audio."""
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import select
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
import threading
import math

PLAYER='02:00:00:00:00:01'

def run(base,fixture,count=3000,benchmark=False,shim=None,long_names=False):
    with tempfile.TemporaryDirectory(prefix='sb-music-browse-') as directory:
        root=Path(directory);db=root/'library.db'
        (root/'WP3B_TEST_FIXTURE').write_text('WP3B disposable fixture\n')
        subprocess.run([fixture,'create',str(db),str(count)]+(['long'] if long_names else []),check=True,timeout=180)
        native_metrics=json.loads(subprocess.check_output([fixture,'bench',str(db)],text=True)) if benchmark else None
        with tempfile.TemporaryFile() as log:
            env=os.environ.copy()
            if shim:
                env['LD_PRELOAD']=(env.get('SB_TEST_ASAN_PRELOAD','')+' '+str(Path(shim).resolve())).strip()
                env['SB_TEST_PARTIAL_SEND']='1'
            process=subprocess.Popen([base,'--test-music-db',str(db)],stderr=log,env=env)
            sockets=[]
            def req(method,path,payload=None,timeout=8):
                c=http.client.HTTPConnection('127.0.0.1',9000,timeout=timeout)
                raw=None if payload is None else json.dumps(payload,ensure_ascii=False).encode()
                c.request(method,path,raw,{'Content-Type':'application/json'})
                r=c.getresponse();data=r.read();status=r.status;c.close()
                return status,json.loads(data),len(data)
            def message(command,number=1,cid='browse',channel='/slim/request',response=None):
                return {'channel':channel,'id':str(number),'clientId':cid,'data':{'request':[PLAYER,command],'response':response or f'/{cid}/reply'}}
            def query(command):
                start=time.monotonic();status,messages,size=req('POST','/cometd',[message(command)])
                assert status==200 and len(messages)==2,messages
                assert messages[0]['id']=='1' and messages[1]['id']=='1'
                return messages[0]['successful'],messages[1]['data'],(time.monotonic()-start)*1000,size
            def browse(mode,offset=0,quantity=200,*tags):
                # Stock SlimBrowser adds this presentation flag whenever an
                # action has params; replay its real request shape.
                return query(['browselibrary','items',offset,quantity,f'mode:{mode}','menu:1','useContextMenu:1',*tags])
            def stream(cid, reconnect=False):
                s=socket.create_connection(('127.0.0.1',9000),timeout=8);sockets.append(s)
                body=json.dumps([{'channel':'/meta/reconnect' if reconnect else '/meta/connect','clientId':cid}]).encode()
                s.sendall(b'POST /cometd HTTP/1.1\r\nHost: localhost\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body)
                r=http.client.HTTPResponse(s);r.begin();assert r.status==200
                return r
            def chunk(r):
                length=int(r.fp.readline().split(b';',1)[0],16)
                data=r._safe_read(length);assert r._safe_read(2)==b'\r\n';return json.loads(data)
            try:
                for _ in range(150):
                    try:
                        if req('GET','/health',timeout=.2)[0]==200:break
                    except OSError:
                        if process.poll() is not None:
                            log.seek(0);raise AssertionError(log.read().decode(errors='replace'))
                        time.sleep(.02)
                else:raise AssertionError('fixture Base did not start')
                slim=socket.create_connection(('127.0.0.1',3483),timeout=3);sockets.append(slim)
                hello=bytes([7,0])+bytes.fromhex(PLAYER.replace(':',''))+bytes(26)+b'ENModel=fab4,mp3,flc,ogg'
                slim.sendall(b'HELO'+struct.pack('!I',len(hello))+hello)
                raw=b''
                while len(raw)<30:raw+=slim.recv(128)
                assert raw[6:7]==b't'
                sql=sqlite3.connect(db)
                ok,menu,_,_=query(['menu',0,100,'direct:1']);assert ok
                golden=json.loads((Path(__file__).parent/'fixtures/browse.json').read_text())
                assert menu['item_loop'][2:]==golden['music_nodes']
                ids=[x['id'] for x in menu['item_loop']]
                assert ids==['settingsAlarm','settingsSleep','myMusicArtists','myMusicAlbums','myMusicGenres','myMusicMusicFolder','myMusicTracks'],ids
                for flag in ('0','1'):
                    assert query(['browselibrary','items',0,200,'mode:artists','menu:1','useContextMenu:'+flag])[0]
                for tag in ('useContextMenu:2','useContextMenu:yes','unknownPresentation:1'):
                    ok,rejected,_,_=query(['browselibrary','items',0,200,'mode:artists','menu:1',tag])
                    assert not ok and rejected['error']=='unsupported_or_invalid_browse',rejected
                # WP4a.6: Jive replays its whole sink after any error, so
                # rejecting an oversized backlog wedges CometD in a
                # permanent resend loop (Fab4 UI flap at 68
                # handshakes/s; backlogs do exceed 256). Oversized
                # batches are truncated to their head instead: the
                # first 32 are answered, the rest noted, and the sink
                # drains progressively. Per-request work stays bounded
                # by the 32-command pump cap + keep-partial overflow.
                batch=[message(['menu',0,10,'direct:1'],number=n) for n in range(1,41)]
                status,_,_=req('POST','/cometd',batch);assert status==200,status
                batch=[message(['menu',0,10,'direct:1'],number=n) for n in range(1,258)]
                status,messages,_=req('POST','/cometd',batch);assert status==200,status
                assert messages[0]['id']=='1' and messages[0]['successful'],messages[:2]
                assert any(m.get('error')=='response_too_large' for m in messages),messages[-3:]
                latencies=[];sizes=[]
                performance=None
                if benchmark:
                    def stats():
                        fields=Path(f'/proc/{process.pid}/stat').read_text().split()
                        text=Path(f'/proc/{process.pid}/status').read_text()
                        return {'ticks':int(fields[13])+int(fields[14]),'rss_kib':int(next(x.split()[1] for x in text.splitlines() if x.startswith('VmHWM:'))),'fds':len(list(Path(f'/proc/{process.pid}/fd').iterdir()))}
                    before=stats();controls=[];stop_probe=threading.Event();probe_errors=[]
                    def probe():
                        while not stop_probe.is_set():
                            start=time.monotonic()
                            try:assert req('GET','/health',timeout=2)[0]==200
                            except Exception as error:probe_errors.append(str(error));break
                            controls.append((time.monotonic()-start)*1000);stop_probe.wait(.005)
                    thread=threading.Thread(target=probe);thread.start();start_wall=time.monotonic()
                    measured=[];responses=[]
                    for iteration in range(120):
                        mode='albums' if iteration%2 else 'artists'
                        ok,page,elapsed,size=browse(mode,(iteration%3)*50)
                        assert ok,(mode,iteration,page,native_metrics['query_ms'][-12:]);measured.append(elapsed);responses.append(size)
                    stop_probe.set();thread.join();after=stats();assert not probe_errors,probe_errors
                    def percentiles(values):
                        ordered=sorted(values)
                        return {key:ordered[min(len(ordered)-1,math.ceil(p*len(ordered))-1)] for key,p in [('p50',.5),('p95',.95),('p99',.99)]}
                    performance={'tracks':count,'end_to_end_ms':percentiles(measured),'query_submit_complete_ms':percentiles(native_metrics['query_ms']),
                        'control_health_ms':percentiles(controls),'control_samples':len(controls),'peak_rss_kib':after['rss_kib'],'fds_sample':after['fds'],
                        'cpu_seconds':(after['ticks']-before['ticks'])/os.sysconf('SC_CLK_TCK'),'wall_seconds':time.monotonic()-start_wall,
                        'max_response_bytes':max(responses),'native':native_metrics,'pending':req('GET','/test/music/metrics')[1]}

                for mode,table in [('artists','contributors'),('albums','albums'),('tracks','tracks'),('genres','genres')]:
                    ok,data,lat,size=browse(mode)
                    assert ok,(mode,data)
                    total=sql.execute(f'SELECT count(*) FROM {table}').fetchone()[0]
                    assert data['count']==total,(mode,data['count'],total)
                    assert len(data['item_loop'])==min(200,total)
                    if mode=='artists':assert data['base']['actions']['go']==golden['artist_go']
                    if mode in ('artists','albums','genres'):
                        assert all(next(iter(x['commonParams'].values()))==x['id'] for x in data['item_loop'])
                    assert not any(k in json.dumps(data) for k in ('playlistcontrol','file://','"play"','"add"'))
                    latencies.append(lat);sizes.append(size)
                if count==0:
                    assert browse('bmf')[1]['count']==0
                    assert query(['info','total','songs','?'])[1]['_songs']==0
                    sql.close();print('Empty library: all browse kinds and counts passed');return
                total=sql.execute('SELECT count(*) FROM albums').fetchone()[0]
                expected=[str(x[0]) for x in sql.execute('SELECT id FROM albums ORDER BY title COLLATE NOCASE,id')]
                for offset in [0,50,200,total-1,total,total+100]:
                    ok,data,lat,size=browse('albums',offset);assert ok
                    assert data['offset']==offset and data['count']==total
                    assert [x['id'] for x in data['item_loop']]==expected[offset:offset+200]
                    latencies.append(lat);sizes.append(size)
                album=sql.execute('SELECT id FROM albums ORDER BY id LIMIT 1').fetchone()[0]
                ok,data,_,_=browse('tracks',0,200,f'album_id:{album}');assert ok
                expected_tracks=list(sql.execute('SELECT id,disc,track,title FROM tracks WHERE album_id=? ORDER BY disc,track,id',(album,)))
                assert [int(x['id']) for x in data['item_loop']]==[x[0] for x in expected_tracks]
                assert all(x['action']=='none' and x['duration']>0 for x in data['item_loop'])
                assert any('Caf\u00e9' in x['text'] for x in data['item_loop'])
                assert any('; Guest' in x['artist'] for x in data['item_loop'])
                artist=sql.execute('SELECT contributor_id FROM track_contributors WHERE role=1 LIMIT 1').fetchone()[0]
                ok,data,_,_=browse('albums',0,200,f'artist_id:{artist}');assert ok and data['count']
                genre=sql.execute('SELECT id FROM genres LIMIT 1').fetchone()[0]
                ok,data,_,_=browse('artists',0,200,f'genre_id:{genre}');assert ok and data['count']
                ok,data,_,_=browse('bmf');assert ok and data['count']
                folder=data['item_loop'][0]['actions']['go']['params']['folder_id']
                ok,data,_,_=browse('bmf',0,200,f'folder_id:{folder}');assert ok and data['count']
                for mode,offset,quantity,tags in [('albums',-1,50,()),('albums',0,201,()),('search',0,50,()),('bmf',0,50,(f'folder_id:../../etc',)),('albums',0,50,('artist_id:1','artist_id:2'))]:
                    ok,data,_,_=browse(mode,offset,quantity,*tags);assert not ok,(mode,data)
                for action in [['playlistcontrol','cmd:load','track_id:1'],['playlistcontrol','cmd:add','track_id:1'],['rescan']]:assert not query(action)[0]
                ok,info,_,_=query(['info','total','songs','?']);assert ok and info['_songs']==count
                # Mixed batches preserve input command/result association and existing date behavior.
                commands=[message(['date'],number=7),message(['browselibrary','items',0,50,'mode:albums'],number=8),message(['info','total','songs','?'],number=9)]
                status,batch,_=req('POST','/cometd',commands);assert status==200
                assert [x['id'] for x in batch]==['7','7','8','8','9','9']
                with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
                    for result in executor.map(lambda _:browse('albums'),range(12)):
                        assert result[0],result[1]
                # A stale pre-restart ID must cause stock Comet to handshake,
                # which replays subscriptions rather than retaining empty menus.
                _,expired,_=req('POST','/cometd',[
                    {'channel':'/meta/reconnect','clientId':'expired-before-restart'},
                    {'channel':'/meta/subscribe','clientId':'expired-before-restart','subscription':'/expired-before-restart/**'}])
                assert expired[0]['successful'] is False,expired
                assert expired[0]['advice']['reconnect']=='handshake',expired
                assert expired[0]['channel']=='/meta/reconnect',expired
                # Register stock menustatus subscription and verify stable IDs/reconnect.
                _,handshake,_=req('POST','/cometd',[{'channel':'/meta/handshake'}]);cid=handshake[0]['clientId']
                r=stream(cid);chunk(r)
                channel=f'/{cid}/slim/menustatus/{PLAYER}'
                payload=message(['menustatus'],cid=cid,channel='/slim/subscribe',response=channel)
                _,registered,_=req('POST','/cometd',[payload]);assert registered[1]['data'][0]=='menustatus'
                req('POST','/cometd',[payload]) # duplicate registration replaces, never duplicates IDs
                view_channel=f'/{cid}/browse/albums'
                view_payload=message(['browselibrary','items',0,200,'mode:albums','subscribe:600'],cid=cid,channel='/slim/subscribe',response=view_channel)
                assert req('POST','/cometd',[view_payload])[1][0]['successful']
                assert req('GET','/test/music/offline')[0]==200
                removed=None
                for _ in range(5):
                    events=chunk(r)
                    for event in events:
                        if event.get('channel')==channel:removed=event['data']
                    if removed:break
                assert removed[2]=='remove' and len(removed[1])==5,removed
                assert not browse('albums')[0]
                assert req('GET','/test/music/online')[0]==200
                added=None
                for _ in range(8):
                    events=chunk(r)
                    for event in events:
                        if event.get('channel')==channel and event['data'][2]=='add':added=event['data']
                    if added:break
                assert added and len(added[1])==5
                assert browse('albums')[0]
                # Reconnect replays the logical subscriptions without duplicate IDs.
                r2=stream(cid,reconnect=True);assert chunk(r2)[0]['successful'];r=r2
                replay=None;view=None
                for _ in range(10):
                    events=chunk(r)
                    for event in events:
                        if event.get('channel')==channel:replay=event['data']
                        if event.get('channel')==view_channel:view=event['data']
                    if replay and view:break
                assert replay[2]=='add' and len({x['id'] for x in replay[1]})==5
                assert view['count']==total and view['media_generation']==2
                # Commit a controlled external fixture edit, then reject the old page revision.
                ok,first,_,_=browse('albums',0,50);assert ok
                old_revision=first['library_revision']
                sql.execute('UPDATE albums SET title=? WHERE id=?',('Changed Album',album))
                sql.execute('UPDATE library_meta SET revision=revision+1 WHERE id=1');sql.commit()
                assert not browse('albums',50,50,f'library_revision:{old_revision}')[0]
                status_channel=f'/{cid}/slim/playerstatus/{PLAYER}'
                req('POST','/cometd',[message(['status','-',1,'subscribe:600'],cid=cid,channel='/slim/subscribe',response=status_channel)])
                _,claim,_=req('POST','/api/nowplaying/claim',{'source':'radio','name':'Browse compatibility fixture'})
                req('POST','/api/nowplaying/update',{'session_id':claim['session_id'],'state':'playing','track':{'id':'fixture-radio','title':'Metadata during browsing'}})
                refreshed=None;metadata_event=None
                for _ in range(12):
                    for event in chunk(r):
                        if event.get('channel')==view_channel and event['data'].get('library_revision',0)>old_revision:refreshed=event['data']
                        if event.get('channel')==status_channel and event['data'].get('item_loop',[{}])[0].get('track')=='Metadata during browsing':metadata_event=event['data']
                    if refreshed and metadata_event:break
                assert refreshed and refreshed['playlist_timestamp']>old_revision
                assert metadata_event is not None
                req('POST','/cometd',[{'channel':'/slim/unsubscribe','clientId':cid,'data':{'unsubscribe':channel}}])
                req('GET','/test/music/offline')
                # No menu removal after cancellation; only ordinary heartbeats may arrive.
                deadline=time.monotonic()+.15
                while time.monotonic()<deadline:
                    if select.select([r.fp],[],[],.03)[0]:
                        assert all(e.get('channel')!=channel for e in chunk(r))
                # There are no strm-s commands or physical ownership actions.
                slim.setblocking(False);later=b''
                try:later=slim.recv(4096)
                except BlockingIOError:pass
                assert b's1' not in later
                sql.close()
                print(json.dumps(performance or {'tracks':count,'samples':len(latencies),'max_response_bytes':max(sizes),'max_latency_ms':max(latencies),'partial':bool(shim)}))
            finally:
                for s in sockets:s.close()
                process.terminate()
                try:process.wait(timeout=7)
                except subprocess.TimeoutExpired:process.kill();process.wait()
                log.seek(0);errors=log.read()
                if sys.exc_info()[0] is not None:
                    error_path=Path('.local/wp3b/last-server-error.log')
                    error_path.parent.mkdir(parents=True,exist_ok=True)
                    error_path.write_bytes(errors)
                assert process.returncode==0,(process.returncode,errors[-5000:].decode(errors='replace'))
                assert b'ERROR: AddressSanitizer' not in errors and b'runtime error:' not in errors,errors[-5000:]

if __name__=='__main__':run(sys.argv[1],sys.argv[2],shim=sys.argv[3] if len(sys.argv)>3 and sys.argv[3]!='long' else None,long_names='long' in sys.argv)
