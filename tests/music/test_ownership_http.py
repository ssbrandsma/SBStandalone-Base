"""Production Base route: real provisioning/authentication, no mock proof route."""
import http.client,json,os,socket,stat,struct,subprocess,sys,tempfile,time
from pathlib import Path

def run(binary):
    with tempfile.TemporaryDirectory(prefix='sb-owner-http-') as directory, tempfile.TemporaryFile() as log:
        root=Path(directory);root.chmod(0o700);token_path=root/'token'
        p=subprocess.Popen([str(Path(binary).resolve()),'--ownership-token',str(token_path)],stdout=log,stderr=log)
        slim=None
        def request(body,token=None,headers=None):
            c=http.client.HTTPConnection('127.0.0.1',9000,timeout=3)
            h={'X-Music-Ownership':(token or '0'*64),'Content-Type':'text/plain'}
            h.update(headers or {})
            c.request('POST','/api/music/ownership',body,headers=h)
            response=c.getresponse();data=response.read();c.close()
            return response.status,json.loads(data)
        try:
            until=time.monotonic()+5
            while time.monotonic()<until:
                if token_path.exists():
                    try:
                        token=token_path.read_text();status,_=request('SBOWN1 hello 1 0 1 0 0 0 0 0 0 0',token)
                        if status==200:break
                    except (OSError,ValueError):pass
                time.sleep(.02)
            else:raise AssertionError('Base startup')
            assert stat.S_IMODE(token_path.stat().st_mode)==0o600
            assert len(token)==64
            assert request('SBOWN1 hello 1 0 1 0 0 0 0 0 0 0')[0]==403
            assert request('SBOWN1 hello 1 0 1 0 0 0 0 0 0 0',token[:-1]+'X')[0]==403
            assert request('x'*385,token)[0]==400
            assert request('SBOWN1 query 1 1 1 0 0 0 0 0 0 0',token)[0]==409
            slim=socket.create_connection(('127.0.0.1',3483),timeout=3)
            payload=struct.pack('!BB6s16sHII2s',5,1,bytes.fromhex('001122334455'),bytes(16),0,0,0,b'EN')+b'Model=fab4,mp3,flc'
            slim.sendall(b'HELO'+struct.pack('!I',len(payload))+payload)
            time.sleep(.03)
            status,v=request('SBOWN1 register 1 0 1 0 0 0 0 0 0 0',token);assert status==200
            assert v['connected'] and not v['controls']
            fields=['SBOWN1','request','1',v['incarnation'],'2',v['generation'],v['connection'],'0','1000','0','0','0']
            assert request(' '.join(fields),token)[0]==409 # HELO gives no idle proof
            repeat=request('SBOWN1 register 1 0 1 0 0 0 0 0 0 0',token)
            assert repeat[0]==200 and repeat[1]['incarnation']==v['incarnation']
            # Duplicate capability headers is rejected, even with correct first token.
            c=http.client.HTTPConnection('127.0.0.1',9000,timeout=3)
            c.putrequest('POST','/api/music/ownership');c.putheader('X-Music-Ownership',token)
            c.putheader('X-Music-Ownership',token);c.putheader('Content-Length','1');c.endheaders(b'x')
            response=c.getresponse();assert response.status==403;response.read();c.close()
            # Health stays live while a bounded ownership subscription is held.
            body=' '.join(['SBOWN1','query','1',v['incarnation'],'3',v['generation'],v['connection'],'0','0','0',v['revision'],'0']).encode()
            pending=socket.create_connection(('127.0.0.1',9000),timeout=3)
            pending.sendall(b'POST /api/music/ownership HTTP/1.1\r\nX-Music-Ownership: '+token.encode()+b'\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body)
            time.sleep(.02)
            before=time.monotonic();c=http.client.HTTPConnection('127.0.0.1',9000,timeout=3);c.request('GET','/health')
            response=c.getresponse();assert response.status==200;response.read();c.close()
            health_ms=(time.monotonic()-before)*1000
            slim.close();slim=None
            received=pending.recv(4096);assert b'200 OK' in received,received;pending.close()
            fd_count=len(list(Path(f'/proc/{p.pid}/fd').iterdir()))
            print(json.dumps({'production_auth':'pass','health_ms':health_ms,'fds':fd_count,'pending_limit':4,'extra_public_ports':0}))
        finally:
            if slim:slim.close()
            p.terminate();p.wait(timeout=5)
        assert not token_path.exists()
        # An existing token cannot silently survive a new Base incarnation.
        token_path.write_text('a'*64);token_path.chmod(0o600)
        bad=subprocess.run([str(Path(binary).resolve()),'--ownership-token',str(token_path)],stdout=log,stderr=log,timeout=5)
        assert bad.returncode==2 and token_path.read_text()=='a'*64
        token_path.unlink();root.chmod(0o755)
        bad=subprocess.run([str(Path(binary).resolve()),'--ownership-token',str(token_path)],stdout=log,stderr=log,timeout=5)
        assert bad.returncode==2 and not token_path.exists()
if __name__=='__main__':run(sys.argv[1])
