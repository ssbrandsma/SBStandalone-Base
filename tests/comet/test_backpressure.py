"""Disposable loopback Base only. Slow and fast Comet sessions share one loop."""
import http.client
import json
import os
from pathlib import Path
import socket
import statistics
import struct
import subprocess
import sys
import tempfile
import threading
import time

player = '02:00:00:00:00:01'
measure = '--measure-only' in sys.argv
partial = '--partial' in sys.argv
env = os.environ.copy()
if len(sys.argv)>2 and not sys.argv[2].startswith('--'):
    env['LD_PRELOAD'] = (os.environ.get('SB_TEST_ASAN_PRELOAD','')+' '+os.path.abspath(sys.argv[2])).strip()
if partial:
    env['SB_TEST_PARTIAL_SEND'] = '1'

def request(method, path, payload=None):
    c=http.client.HTTPConnection('127.0.0.1',9000,timeout=6)
    c.request(method,path,None if payload is None else json.dumps(payload),{'Content-Type':'application/json'})
    r=c.getresponse();data=r.read();status=r.status;c.close()
    return status,json.loads(data)

def comet(payload):
    status, data=request('POST','/cometd',[payload]);assert status==200;return data

def open_stream(slow=False):
    cid=comet({'channel':'/meta/handshake'})[0]['clientId']
    s=socket.socket();s.settimeout(5)
    if slow:s.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,1024)
    s.connect(('127.0.0.1',9000))
    body=json.dumps([{'channel':'/meta/connect','clientId':cid}]).encode()
    s.sendall(b'POST /cometd HTTP/1.1\r\nHost: localhost\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body)
    reader=s.makefile('rb');r=http.client.HTTPResponse(s);r.begin()
    assert r.status==200
    # Header receipt proves stream allocation; no initial chunk read for slow peers.
    response=f'/{cid}/slim/playerstatus/{player}'
    comet({'channel':'/slim/subscribe','clientId':cid,'id':1,
           'data':{'request':[player,['status','-',1,'subscribe:600']], 'response':response}})
    return s,r,cid,reader

def process_info(pid):
    status=Path(f'/proc/{pid}/status').read_text()
    ticks=Path(f'/proc/{pid}/stat').read_text().split()
    return {'fds':len(list(Path(f'/proc/{pid}/fd').iterdir())),
            'rss_kib':int(next(x.split()[1] for x in status.splitlines() if x.startswith('VmRSS:'))),
            'cpu_ticks':int(ticks[13])+int(ticks[14])}

with tempfile.TemporaryFile() as log:
    p=subprocess.Popen([sys.argv[1]],stderr=log,env=env)
    streams=[];errors=[];seen=[];latencies=[]
    try:
        for _ in range(100):
            try:
                if request('GET','/health')[0]==200:break
            except OSError:time.sleep(.02)
        else:raise AssertionError('Base did not start')
        before=process_info(p.pid);time.sleep(.3);idle=process_info(p.pid)
        slim=socket.create_connection(('127.0.0.1',3483),timeout=3)
        hello=bytes([7,0])+bytes.fromhex(player.replace(':',''))+bytes(26)+b'ENModel=fab4,mp3,flc,ogg'
        slim.sendall(b'HELO'+struct.pack('!I',len(hello))+hello)
        data=b''
        while len(data)<30:data+=slim.recv(128)
        assert data[2:6]==b'strm' and data[6:7]==b't'
        slow=[open_stream(True),open_stream(True)];streams.extend(slow)
        fast=open_stream();streams.append(fast)
        done=threading.Event()
        def drain():
            try:
                while not done.is_set():
                    # Read one entire HTTP chunk, including artificial 37-byte writes.
                    r=fast[1]
                    size=int(r.fp.readline().split(b';',1)[0],16)
                    if not size:break
                    data=r._safe_read(size)
                    assert r._safe_read(2)==b'\r\n'
                    for event in json.loads(data):
                        rows=event.get('data',{}).get('item_loop',[])
                        if rows and rows[0].get('track','').startswith('seq'):
                            seen.append(int(rows[0]['track'][3:7]))
            except (OSError,ValueError,http.client.HTTPException) as e:
                if not done.is_set():errors.append(str(e))
        thread=threading.Thread(target=drain,daemon=True);thread.start()
        _,claim=request('POST','/api/nowplaying/claim',{'source':'radio','name':'test'})
        token=claim['session_id'];base=process_info(p.pid)
        for i in range(160):
            start=time.monotonic()
            status,_=request('POST','/api/nowplaying/update',{
                'session_id':token,'state':'playing','track':{'id':f'track:{i}',
                'title':f'seq{i:04d}'+('T'*220),'artist':'A'*240,'album':'B'*240,
                'album_artist':'C'*240,'genre':'D'*240,'comment':'E'*240}})
            elapsed=(time.monotonic()-start)*1000;latencies.append(elapsed);assert status==200
            if measure and elapsed>1000:break
            if not measure:
                assert elapsed<1500,elapsed
                deadline=time.monotonic()+2
                while i not in seen and not errors and time.monotonic()<deadline:time.sleep(.001)
                assert i in seen,(i,seen[-5:],errors)
        if not measure:
            deadline=time.monotonic()+4
            while len(set(seen))<160 and time.monotonic()<deadline:time.sleep(.01)
            assert sorted(set(seen))==list(range(160)),(len(seen),seen[-5:],errors)
            assert seen==sorted(seen),seen
        # Closing saturated clients must be observable, never silent message dropping.
        closed=0
        for s,r,cid,reader in ([] if measure else slow):
            try:
                while r.read1(32768):pass
                closed+=1
            except (ConnectionResetError,http.client.IncompleteRead):
                # Mid-chunk EOF is the documented overflow disconnect.
                closed+=1
        if not measure:assert closed==2
        assert request('GET','/health')[0]==200
        _,current=request('GET','/api/nowplaying')
        assert current['track']['album']=='B'*240
        for _ in range(40):
            cid=fast[2];channel=f'/{cid}/slim/playerstatus/{player}'
            comet({'channel':'/slim/unsubscribe','clientId':cid,'data':{'unsubscribe':channel}})
            comet({'channel':'/slim/subscribe','clientId':cid,'data':{'request':[player,['status','-',1]],'response':channel}})
        # Malformed HELO and rapid reconnects never start audio.
        for _ in range(0 if measure else 100):
            s=socket.create_connection(('127.0.0.1',3483),timeout=2);s.sendall(b'HELO'+struct.pack('!I',1)+b'\0');assert s.recv(1)==b'';s.close()
        peak=process_info(p.pid)
        done.set()
        for s,r,cid,reader in streams:s.shutdown(socket.SHUT_RDWR);s.close();r.close();reader.close()
        slim.close();time.sleep(.1);after=process_info(p.pid)
        assert after['fds']==before['fds'],(before,after)
        if not measure:assert not errors,errors
        print(json.dumps({'mode':'partial' if partial else 'normal','updates':len(latencies),
            'update_ms_p50':statistics.median(latencies),'update_ms_p95':sorted(latencies)[int(.95*(len(latencies)-1))],
            'update_ms_max':max(latencies),'slow_closed':closed,'before':before,'active':base,'peak_sample':peak,'after':after,
            'idle_cpu_ticks_300ms':idle['cpu_ticks']-before['cpu_ticks'],'observed_ordered_updates':len(set(seen))},sort_keys=True))
    finally:
        p.terminate()
        try:p.wait(timeout=4)
        except subprocess.TimeoutExpired:p.kill();p.wait()
        log.seek(0);errors_text=log.read()
        assert p.returncode==0,(p.returncode,errors_text[-4000:])
        assert b'ERROR: AddressSanitizer' not in errors_text and b'runtime error:' not in errors_text,errors_text[-4000:]
