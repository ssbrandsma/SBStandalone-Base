#!/usr/bin/env python3
"""Shared production Base paths used by Baby Radio; no audio or device access."""
import http.client,json,shlex,socket,subprocess,sys,time

def request(path,body=None):
    c=http.client.HTTPConnection('127.0.0.1',9000,timeout=3)
    c.request('GET' if body is None else 'POST',path,None if body is None else json.dumps(body))
    r=c.getresponse();raw=r.read();c.close();assert r.status==200,(r.status,raw)
    return json.loads(raw)

def start():
    p=subprocess.Popen(shlex.split(sys.argv[1]),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    for _ in range(80):
        try:request('/health');return p
        except OSError:time.sleep(.025)
    p.terminate();p.wait();raise AssertionError('Base did not start')

def pipeline():
    p=start()
    try:
        body=b'[{"channel":"/meta/handshake"}]'
        frame=b'POST /cometd HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body
        with socket.create_connection(('127.0.0.1',9000),timeout=3) as sock:
            sock.sendall(frame*200)
            with sock.makefile('rb') as response:
                for i in range(200):
                    line=response.readline();assert b' 200 ' in line,(i,line)
                    length=None
                    while (line:=response.readline()) not in (b'\r\n',b''):
                        if line.lower().startswith(b'content-length:'):length=int(line.split(b':',1)[1])
                    assert length is not None
                    assert json.loads(response.read(length))[0]['successful']
        print('200 pipelined requests drain without another client write')
    finally:p.terminate();p.wait(timeout=3)

def subscriptions():
    p=start();player='02:00:00:00:00:01'
    try:
        request('/cometd',[{'channel':'/meta/handshake','mac':player}])
        for i in range(20):
            cid=f'request-{i}'
            request('/cometd',[{'channel':'/slim/subscribe','clientId':cid,'id':i,
                'data':{'request':[player,['status','-',10,'subscribe:600']],
                        'response':f'/{cid}/slim/playerstatus/{player}'}}])
        data=request('/api/debug/comet')
        assert 'request-19' in data['subscribed_cids'],data
        assert len(data['subscribed_cids'])<=8,data
        print('Latest Jive subscription survives more than eight reconnect identities')
    finally:p.terminate();p.wait(timeout=3)

if len(sys.argv)<3 or sys.argv[2]=='pipeline':pipeline()
if len(sys.argv)<3 or sys.argv[2]=='subscriptions':subscriptions()
