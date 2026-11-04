"""Backpressure, DB BUSY/deadlines, disconnect and source revocation."""
import http.client
import json
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time

with tempfile.TemporaryDirectory(prefix='sb-music-browse-') as directory:
    root=Path(directory);db=root/'library.db';(root/'WP3B_TEST_FIXTURE').write_text('WP3B disposable fixture\n')
    subprocess.run([sys.argv[2],'create',str(db),'1000'],check=True)
    with tempfile.TemporaryFile() as log:
        p=subprocess.Popen([sys.argv[1],'--test-music-db',str(db)],stderr=log)
        sockets=[]
        def get(path):
            c=http.client.HTTPConnection('127.0.0.1',9000,timeout=2);start=time.monotonic();c.request('GET',path);r=c.getresponse();data=r.read();c.close();assert r.status==200;return (time.monotonic()-start)*1000
        def pending(number):
            s=socket.create_connection(('127.0.0.1',9000),timeout=8);sockets.append(s)
            body=json.dumps([{'channel':'/slim/request','clientId':'fault','id':str(number),'data':{'request':['02:00:00:00:00:01',['browselibrary','items',0,200,'mode:albums']],'response':'/fault/reply'}}]).encode()
            s.sendall(b'POST /cometd HTTP/1.1\r\nHost: localhost\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body)
            return s
        def receive(s):
            r=http.client.HTTPResponse(s);r.begin();return r.status,json.loads(r.read())
        try:
            for _ in range(100):
                try:get('/health');break
                except OSError:time.sleep(.02)
            slim=socket.create_connection(('127.0.0.1',3483),timeout=2);sockets.append(slim)
            payload=bytes([7,0,2,0,0,0,0,1])+bytes(26)+b'ENModel=fab4,flc,mp3'
            slim.sendall(b'HELO'+struct.pack('!I',len(payload))+payload);assert b'strm' in slim.recv(100)
            sql=sqlite3.connect(db);sql.execute('BEGIN EXCLUSIVE')
            active=[pending(i) for i in range(4)];time.sleep(.05)
            status,_=receive(pending(99));assert status==503,status
            controls=[get('/health') for _ in range(20)];assert max(controls)<500,max(controls)
            active[0].close();time.sleep(.03)
            replacement=pending(200)
            sql.rollback()
            for s in active[1:]+[replacement]:
                status,messages=receive(s);assert status==200 and messages[0]['successful'],messages
            # A hard SQL deadline still yields a bounded compatible failure while health works.
            sql.execute('BEGIN EXCLUSIVE');start=time.monotonic();timed=pending(300)
            controls.extend(get('/health') for _ in range(10))
            status,messages=receive(timed);elapsed=time.monotonic()-start
            assert status==200 and not messages[0]['successful'],messages
            assert 4.5<elapsed<7.5,elapsed;sql.rollback()
            # Removal revokes pending work before results are put on the wire.
            sql.execute('BEGIN EXCLUSIVE');removed=pending(400);time.sleep(.03);get('/test/music/offline');sql.rollback()
            try:
                status,result=receive(removed)
                assert status==200 and not result[0]['successful'],result
            except (http.client.RemoteDisconnected,http.client.IncompleteRead,ConnectionResetError):pass
            sql.close()
            # Invalid UTF-8 is rejected before dispatch, with no database work.
            bad=socket.create_connection(('127.0.0.1',9000),timeout=2);sockets.append(bad)
            raw=b'[{"channel":"/slim/request","bad":"\xff"}]'
            bad.sendall(b'POST /cometd HTTP/1.1\r\nContent-Length: '+str(len(raw)).encode()+b'\r\n\r\n'+raw)
            r=http.client.HTTPResponse(bad);r.begin();assert r.status==400;r.read()
            print(json.dumps({'busy_control_max_ms':max(controls),'deadline_seconds':elapsed,'pending_limit':4,'async_limit':8}))
        finally:
            for s in sockets:s.close()
            p.terminate();p.wait(timeout=7);log.seek(0);errors=log.read()
            assert p.returncode==0,(p.returncode,errors[-3000:])
