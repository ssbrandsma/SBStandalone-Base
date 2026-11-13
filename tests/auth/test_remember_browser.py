#!/usr/bin/env python3
"""Real HTTP pairing, restart persistence and durable revocation, host-only."""
import hashlib,json,os,pathlib,socket,stat,subprocess,sys,tempfile,time
from test_web_pairing import request

binary,web=sys.argv[1:3]
with tempfile.TemporaryDirectory(prefix='sb-remember-') as directory:
    root=pathlib.Path(directory);store=root/'remembered-browsers.conf'
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));port=listener.getsockname()[1]
    base=f'http://127.0.0.1:{port}';process=None;configured=directory
    def start():
        global process
        process=subprocess.Popen([binary,'--port',str(port),'--web-root',web,'--config-dir',str(configured)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                with socket.create_connection(('127.0.0.1',port),.1):return
            except OSError:time.sleep(.03)
        raise AssertionError('server did not start')
    def stop():
        global process
        if process:process.terminate();process.wait(timeout=5);process=None
    def restart():stop();start()
    def internal():return {'X-Remote-Capability':(root/'remote.capability').read_text().strip()}
    def pair(remember=False,expected=200):
        status,body,_=request(base,'/api/internal/web-access/start','POST',b'',internal());assert status==200
        pin=json.loads(body)['pin'];status,body,headers=request(base,'/api/auth/pair','POST',{'pin':pin,'remember':remember})
        assert status==expected,(status,body)
        if expected!=200:assert 'Set-Cookie' not in headers;return None
        assert json.loads(body)['remembered']==remember
        assert f"Max-Age={31536000 if remember else 86400}" in headers['Set-Cookie']
        return headers['Set-Cookie'].split(';',1)[0]
    def authenticated(cookie):return json.loads(request(base,'/api/auth/status',cookie=cookie)[1])['authenticated']
    try:
        start();short=pair();persistent=pair(True)
        token=persistent.split('=',1)[1];saved=store.read_text()
        assert token not in saved and hashlib.sha256(bytes.fromhex(token)).hexdigest() in saved
        assert stat.S_IMODE(store.stat().st_mode)==0o600
        assert authenticated(short) and authenticated(persistent)
        restart();assert not authenticated(short) and authenticated(persistent)
        status,body,headers=request(base,'/api/auth/status',cookie=persistent)
        assert json.loads(body)['remembered'] and 'Max-Age=31536000' in headers['Set-Cookie']
        # Read-only/status requests renew the cookie without rewriting flash.
        before=(store.read_bytes(),store.stat().st_mtime_ns)
        for _ in range(3):assert authenticated(persistent)
        assert before==(store.read_bytes(),store.stat().st_mtime_ns)
        assert request(base,'/api/auth/logout','POST',b'',cookie=persistent)[0]==200
        restart();assert not authenticated(persistent)
        first=pair(True);second=pair(True);restart();assert authenticated(first) and authenticated(second)
        assert request(base,'/api/internal/web-access/revoke-all','POST',b'',internal())[0]==200
        restart();assert not authenticated(first) and not authenticated(second)
        # Browser-initiated revoke also survives a real process restart.
        current=pair(True)
        # A failed durable logout/revocation must not claim success or forget
        # only in memory and resurrect the credential after restart.
        root.chmod(0o777)
        assert request(base,'/api/auth/logout','POST',b'',cookie=current)[0]==503
        assert request(base,'/api/auth/revoke-all','POST',b'',cookie=current)[0]==503
        assert authenticated(current)
        root.chmod(0o700);restart();assert authenticated(current)
        assert request(base,'/api/auth/revoke-all','POST',b'',cookie=current)[0]==200
        restart();assert not authenticated(current)
        # Persistence failure must not issue a successful remembered cookie.
        short=pair();store.unlink();store.mkdir();pair(True,503)
        assert authenticated(short);store.rmdir()
        current=pair(True);saved=store.read_bytes()
        store.write_bytes(b'broken credential store');restart();assert not authenticated(current)
        assert request(base,'/api/internal/web-access/revoke-all','POST',b'',internal())[0]==200
        assert store.read_bytes()!=b'broken credential store';restart();assert not authenticated(current)
        # Never load a symlink or a group/world-readable credential file.
        external=root/'external';external.write_bytes(saved);external.chmod(0o600)
        store.unlink();store.symlink_to(external);restart();assert not authenticated(current)
        new=pair(True);assert external.read_bytes()==saved and not store.is_symlink()
        store.chmod(0o644);restart();assert not authenticated(new)
        request(base,'/api/internal/web-access/revoke-all','POST',b'',internal())
        restart();assert not authenticated(new)
        # Eight bounded credentials; oldest pairing is evicted durably.
        cookies=[pair(True) for _ in range(9)];restart()
        assert not authenticated(cookies[0]) and all(authenticated(cookie) for cookie in cookies[1:])
        # Firmware-managed config directory alias on real Baby installations.
        configured=root/'config-link';configured.symlink_to(root,target_is_directory=True)
        restart();assert authenticated(cookies[-1])
        remembered=pair(True);restart();assert authenticated(remembered)
        print('Remembered browser restart, renewal, logout/revoke, eviction and storage faults passed')
    finally:stop()
