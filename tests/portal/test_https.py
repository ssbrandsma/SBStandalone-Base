#!/usr/bin/env python3
"""Real TLS/socket tests. Private synthetic profiles only; no device secrets."""
import http.client,json,os,socket,ssl,subprocess,sys,tempfile,time,hashlib
from pathlib import Path
WEB,MANAGER,ASSETS=sys.argv[1:4]
CONFIG='client\ndev tun\nremote 198.51.100.1 1194\nremote-cert-tls server\n<ca>\nTEST CA\n</ca>\n'
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def request(p,path,method='GET',body=None,cookie='',tls=False,extra=None):
 context=ssl._create_unverified_context() # Test pins the independently read certificate below.
 c=http.client.HTTPSConnection('127.0.0.1',p,context=context,timeout=5) if tls else http.client.HTTPConnection('127.0.0.1',p,timeout=5)
 headers={'Content-Type':'application/json','Origin':('https' if tls else 'http')+'://127.0.0.1:'+str(p),'Cookie':cookie}|(extra or {})
 c.request(method,path,json.dumps(body) if body is not None else None,headers);r=c.getresponse();data=r.read();h=dict(r.getheaders());c.close();return r.status,data,h
with tempfile.TemporaryDirectory(prefix='portal-test-') as tmp:
 root=Path(tmp);config=root/'config';config.mkdir(mode=0o700);runtime=Path('/tmp/standalonebase/vpn') if os.environ.get('SB_PORTAL_ARM_ABI') else root/'run';hp,tp=port(),port()
 manager=subprocess.Popen([MANAGER,'--config',str(config/'vpn'),'--runtime',str(runtime)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 env=os.environ|{'SB_VPN_TEST_SOCKET':str(runtime/'control.sock'),'SB_CATALOG_SYNC_DISABLED':'1'}
 def start():
  p=subprocess.Popen([WEB,'--port',str(hp),'--https-port',str(tp),'--config-dir',str(config),'--web-root',ASSETS],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  for _ in range(100):
   try:
    if request(hp,'/api/secure-portal')[0]==200:return p
   except OSError:pass
   time.sleep(.05)
  raise AssertionError('web startup failed')
 web=start()
 try:
  info=json.loads(request(hp,'/api/secure-portal')[1]);assert info['available'],info
  cert=(config/'tls/device.crt').read_text();der=ssl.PEM_cert_to_DER_cert(cert);fingerprint=':'.join(f'{b:02X}' for b in hashlib.sha256(der).digest());assert fingerprint==info['fingerprint']
  assert (config/'tls').stat().st_mode&0o777==0o700
  for name in ['device.key','device.crt']:assert (config/'tls'/name).stat().st_mode&0o777==0o600
  key_digest=hashlib.sha256((config/'tls/device.key').read_bytes()).digest()
  assert json.loads(request(tp,'/api/secure-portal',tls=True)[1])['secure']
  assert request(tp,'/api/vpn/profiles','POST',{},tls=True)[0]==401
  cap=(config/'remote.capability').read_text().strip()
  def pair(tls):
   path='/api/internal/web-access/'+('secure-start' if tls else 'start')
   code=json.loads(request(hp,path,'POST',{},extra={'X-Remote-Capability':cap})[1])['pin']
   r=request(tp if tls else hp,'/api/auth/pair','POST',{'pin':code},tls=tls);assert r[0]==200,r
   cookie=r[2]['Set-Cookie'];assert 'HttpOnly' in cookie and 'SameSite=Strict' in cookie
   assert ('; Secure' in cookie)==tls
   return cookie.split(';')[0]
  plain=pair(False)
  assert request(tp,'/api/vpn/status',cookie=plain,tls=True)[0]==401
  # Even a renamed HTTP token cannot become a secure session.
  forged=plain.replace('sb_session=','__Host-sb_secure=')
  assert request(tp,'/api/vpn/status',cookie=forged,tls=True)[0]==401
  # HTTP PIN cannot be consumed on HTTPS.
  code=json.loads(request(hp,'/api/internal/web-access/start','POST',{},extra={'X-Remote-Capability':cap})[1])['pin']
  assert request(tp,'/api/auth/pair','POST',{'pin':code},tls=True)[0]==401
  secure=pair(True)
  assert request(hp,'/api/vpn/status',cookie=secure)[0]==401
  assert request(tp,'/api/vpn/status',cookie=secure,tls=True)[0]==200
  assert request(tp,'/api/vpn/profiles','POST',{},cookie=secure,tls=True,extra={'Origin':f'http://127.0.0.1:{tp}'})[0]==403
  assert request(tp,'/api/vpn/status',cookie=secure,tls=True,extra={'Host':'evil.example'})[0]==400
  # Test a LAN source in an isolated namespace; loopback recovery is deliberately allowed.
  with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
   try:s.connect(('192.0.2.1',9));lan=s.getsockname()[0]
   except OSError:lan='127.0.0.1'
  if not lan.startswith('127.'):
   c=http.client.HTTPConnection(lan,hp,timeout=5);c.request('POST','/api/vpn/profiles','{}',{'Cookie':plain,'Content-Type':'application/json','X-Forwarded-Proto':'https'});r=c.getresponse();assert r.status==426,r.read();c.close()
  body=dict(name='TLS fixture',country='Netherlands',country_code='NL',configuration=CONFIG,username='fixture-user',password='fixture-private-only',password_operation='replace')
  r=request(tp,'/api/vpn/profiles','POST',body,cookie=secure,tls=True);assert r[0]==201,r
  ident=json.loads(r[1])['id']
  for operation in ['keep','replace','clear']:
   change={'name':'TLS fixture','password_operation':operation}
   if operation=='replace':change['password']='replacement-fixture-only'
   r=request(tp,'/api/vpn/profiles/'+ident,'PUT',change,cookie=secure,tls=True);assert r[0]==200,r
  r=request(tp,'/api/vpn/profiles',cookie=secure,tls=True);assert b'fixture-private-only' not in r[1] and b'replacement-fixture-only' not in r[1] and b'TEST CA' not in r[1]
  assert request(tp,'/api/vpn/profiles/'+ident,'DELETE',cookie=secure,tls=True)[0]==200
  # Pin actual leaf against independently read protected storage, then require TLS 1.2/1.3.
  for version in [ssl.TLSVersion.TLSv1_2,ssl.TLSVersion.TLSv1_3]:
   ctx=ssl._create_unverified_context();ctx.minimum_version=ctx.maximum_version=version
   with socket.create_connection(('127.0.0.1',tp)) as raw:
    with ctx.wrap_socket(raw,server_hostname='localhost') as peer:assert peer.getpeercert(binary_form=True)==der
  for version in [ssl.TLSVersion.TLSv1,ssl.TLSVersion.TLSv1_1]:
   ctx=ssl._create_unverified_context();ctx.minimum_version=ctx.maximum_version=version;ctx.set_ciphers('ALL:@SECLEVEL=0')
   try:
    with socket.create_connection(('127.0.0.1',tp)) as raw:ctx.wrap_socket(raw,server_hostname='localhost')
   except ssl.SSLError:pass
   else:raise AssertionError('obsolete TLS accepted')
  if os.environ.get('SB_PORTAL_ARM_ABI'):
   # Embedded server bounds group negotiation to its accelerated P-256 path.
   ctx=ssl._create_unverified_context();ctx.set_ecdh_curve('secp384r1')
   try:
    with socket.create_connection(('127.0.0.1',tp)) as raw:ctx.wrap_socket(raw,server_hostname='localhost')
   except ssl.SSLError:pass
   else:raise AssertionError('ARM server accepted a non-P-256-only peer')
  web.terminate();web.wait(timeout=5);web=start()
  again=json.loads(request(hp,'/api/secure-portal')[1]);assert again['fingerprint']==fingerprint
  assert hashlib.sha256((config/'tls/device.key').read_bytes()).digest()==key_digest
  assert request(hp,'/api/internal/web-access/certificate-reset','POST',{})[0]==403
  assert request(hp,'/api/internal/web-access/certificate-reset','POST',{},extra={'X-Remote-Capability':cap})[0]==200
  rotated=json.loads(request(hp,'/api/secure-portal')[1]);assert rotated['fingerprint']!=fingerprint
  assert request(tp,'/api/vpn/status',cookie=secure,tls=True)[0]==401
  assert hashlib.sha256((config/'tls/device.key').read_bytes()).digest()!=key_digest
  assert not (config/'.tls-rotation').exists()
  web.terminate();web.wait(timeout=5);web=start();assert json.loads(request(hp,'/api/secure-portal')[1])['fingerprint']==rotated['fingerprint']
  # Invalid and incomplete handshakes cannot monopolize TLS or HTTP indefinitely.
  with socket.create_connection(('127.0.0.1',tp)) as bad:bad.sendall(b'not a TLS handshake')
  pending=[socket.create_connection(('127.0.0.1',tp)) for _ in range(14)]
  time.sleep(.2);closed=0
  for peer in pending:
   peer.settimeout(.02)
   try:closed+=peer.recv(1)==b''
   except TimeoutError:pass
   except ConnectionResetError:closed+=1
  assert closed>=2,closed
  assert request(hp,'/api/secure-portal')[0]==200
  time.sleep(10.2)
  for peer in pending:
   try:assert peer.recv(1)==b''
   except ConnectionResetError:pass
   finally:peer.close()
  assert request(tp,'/api/secure-portal',tls=True)[0]==200
  valid_certificate=(config/'tls/device.crt').read_bytes()
  # Expiry is a failure, not permission to generate a replacement identity.
  web.terminate();web.wait(timeout=5)
  subprocess.run(['openssl','x509','-in',str(config/'tls/device.crt'),'-signkey',str(config/'tls/device.key'),'-days','-1','-out',str(config/'tls/device.crt')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  expired=(config/'tls/device.crt').read_bytes();web=start();assert not json.loads(request(hp,'/api/secure-portal')[1])['available'];assert (config/'tls/device.crt').read_bytes()==expired
  # A delayed valid clock/storage state recovers without restarting the server.
  (config/'tls/device.crt').write_bytes(valid_certificate)
  for _ in range(140):
   recovered=json.loads(request(hp,'/api/secure-portal')[1])
   if recovered['available']:break
   time.sleep(.05)
  assert recovered['available'] and recovered['fingerprint']==rotated['fingerprint']
  assert (config/'tls/device.crt').read_bytes()==valid_certificate
  # Corrupt identity is retained, never silently replaced. HTTP survives.
  web.terminate();web.wait(timeout=5);(config/'tls/device.crt').write_text('corrupt');web=start()
  assert not json.loads(request(hp,'/api/secure-portal')[1])['available'];assert (config/'tls/device.crt').read_text()=='corrupt'
  web.terminate();web.wait(timeout=5);(config/'tls/device.crt').unlink();(config/'tls/device.crt').symlink_to(root/'outside')
  (root/'outside').write_text('preserve');web=start();assert not json.loads(request(hp,'/api/secure-portal')[1])['available'];assert (root/'outside').read_text()=='preserve'
  print('HTTPS: identity/fingerprint/permissions/persistence, real TLS 1.2/1.3, obsolete TLS rejection, separate PINs/cookies, Host/Origin/forwarding policy, encrypted VPN CRUD/keep/replace/clear/redaction and safe corrupt-identity fallback passed')
 finally:
  web.terminate();web.wait(timeout=5);manager.terminate();manager.wait(timeout=12)
