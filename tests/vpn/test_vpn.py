#!/usr/bin/env python3
"""Run security boundaries through the actual native parser and persistent manager."""
import json,os,socket,subprocess,sys,tempfile,time,unittest
from pathlib import Path
PARSER,MANAGER=sys.argv[1:3];sys.argv=sys.argv[:1]
BASE='client\ndev tun\nproto udp\nremote 198.51.100.7 1194\nremote-cert-tls server\n<ca>\n-----BEGIN CERTIFICATE-----\nZmFrZS10ZXN0LW9ubHk=\n-----END CERTIFICATE-----\n</ca>\n'
class Parser(unittest.TestCase):
 def check(self,text,ok=True):
  p=subprocess.run([PARSER],input=text.encode(),capture_output=True);self.assertEqual(p.returncode==0,ok,p.stdout);return p.stdout
 def test_valid(self):self.assertIn(b'1 0 198.51.100.7 1194 udp4',self.check(BASE))
 def test_credentials_inline_cert(self):self.check(BASE+'auth-user-pass\n<cert>\nCERT\n</cert>\n<key>\nKEY\n</key>\n')
 def test_quote_normalization(self):self.assertIn(b'"verify-x509-name" "server name" "name"',self.check(BASE+'verify-x509-name "server name" name\n'))
 def test_forbidden_options(self):
  for option in ['up /bin/evil','down /bin/evil','route-up /evil','route-pre-down /evil','ipchange /evil','tls-verify /evil','auth-user-pass-verify /evil','client-connect /evil','client-disconnect /evil','plugin /evil','script-security 3','management 0.0.0.0 9999','management-client','config nested.ovpn','log /tmp/leak','log-append /tmp/leak','status /tmp/leak','writepid /tmp/pid','daemon','cd /','chroot /','ca /etc/shadow','cert ../../secret','key /root/secret','auth-user-pass /etc/shadow','dev tap','dev-type tap','server 10.0.0.0 255.0.0.0','route 0.0.0.0 0.0.0.0','redirect-gateway def1','http-proxy localhost 80','compress lz4','comp-lzo no','setenv PATH /evil','iproute /evil','auth none','cipher none','tls-version-min 1.0']:
   with self.subTest(option=option):self.check(BASE+option+'\n',False)
 def test_malformed(self):
  for text in [BASE.replace('remote-cert-tls server','remote-cert-tls client'),BASE.replace('</ca>',''),BASE.replace('ZmFrZS10ZXN0LW9ubHk=','ENCRYPTED PRIVATE KEY'),BASE+'<key>\nKEY\n</key>\n',BASE+'x'*2049,BASE.replace('1194','70000'),BASE+'\x00evil',BASE+'verify-x509-name "unterminated\n',BASE+'<ca>\nx\n</ca>\n']:
   with self.subTest(text=text[-50:]):self.check(text,False)
 def test_requested_tls_minimum(self):self.assertIn(b'tls-min 1.3',self.check(BASE+'tls-version-min 1.3\n'))
 def test_limits(self):self.check('x'*49153,False)
 def test_json_boundaries(self):
  for data in ['{"x":1,"x":2}','{"x":"\\u0000"}','{"\\u0078":1}','{}garbage','['*18+']'*18]:self.assertNotEqual(subprocess.run([PARSER,'json'],input=data.encode(),capture_output=True).returncode,0)
class Manager(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.store=self.root/'vpn';self.run=self.root/'run';self.start()
 def start(self):
  self.p=subprocess.Popen([MANAGER,'--config',str(self.store),'--runtime',str(self.run)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  for _ in range(100):
   if (self.run/'control.sock').exists():return
   if self.p.poll() is not None:raise AssertionError(self.p.stderr.read())
   time.sleep(.03)
  raise AssertionError('manager startup')
 def stop(self):self.p.terminate();self.p.communicate(timeout=12)
 def tearDown(self):self.stop();self.tmp.cleanup()
 def rpc(self,path,method='GET',body=None):
  s=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET);s.settimeout(4);s.connect(str(self.run/'control.sock'));s.send(json.dumps({'path':path,'method':method,'body':body or {}}).encode());r=json.loads(s.recv(131073));s.close();return r
 def add(self,**extra):
  body=dict(name='Test profile',country='United Kingdom',country_code='GB',configuration=BASE+'auth-user-pass\n',username='service-user',password='TOP-SECRET',password_operation='replace');body.update(extra);r=self.rpc('/profiles','POST',body);self.assertEqual(r['status'],201,r);return r['data']['id']
 def test_private_crud_redaction_restart(self):
  ident=self.add();public=self.rpc('/profiles');self.assertNotIn('TOP-SECRET',json.dumps(public));self.assertNotIn('CERTIFICATE',json.dumps(public));self.assertTrue(public['data'][0]['credentials_configured']);path=self.store/(ident+'.json');self.assertEqual(path.stat().st_mode&0o777,0o600);self.assertEqual(self.store.stat().st_mode&0o777,0o700)
  r=self.rpc('/profiles/'+ident,'PUT',{'name':'Renamed','password':'','password_operation':'keep'});self.assertEqual(r['status'],200,r);self.assertEqual(json.loads(path.read_text())['password'],'TOP-SECRET')
  self.stop();self.start();self.assertEqual(self.rpc('/profiles')['data'][0]['name'],'Renamed');self.assertEqual(self.rpc('/profiles/'+ident,'PUT',{'password_operation':'clear'})['status'],200);self.assertFalse(self.rpc('/profiles/'+ident)['data']['credentials_configured']);self.assertEqual(self.rpc('/profiles/'+ident,'DELETE')['status'],200)
 def test_traversal_symlink(self):
  self.assertEqual(self.rpc('/profiles/../../etc/passwd','DELETE')['status'],400)
  ident='a'*32;outside=self.root/'secret';outside.write_text('DO NOT READ');(self.store/(ident+'.json')).symlink_to(outside);self.assertEqual(self.rpc('/profiles/'+ident)['status'],404);self.assertEqual(outside.read_text(),'DO NOT READ')
 def test_unsupported_safe(self):
  c=self.rpc('/capabilities')['data'];self.assertEqual(self.rpc('/connect','POST')['status'],400 if c['available'] else 503);self.assertFalse((self.run/'client.ovpn').exists());self.assertFalse(self.rpc('/status')['data']['enabled']);self.assertEqual(self.rpc('/disconnect','POST')['status'],202)
 def test_no_forged_hooks(self):self.assertEqual(self.rpc('/event','POST',{'type':'up'})['status'],403)
 def test_request_types_and_unknown_fields(self):
  self.assertEqual(self.rpc('/settings','PUT',{'startup':'yes'})['status'],400)
  self.assertEqual(self.rpc('/settings','PUT',{'kill':1})['status'],400)
  self.assertEqual(self.rpc('/profiles','POST',{'arbitrary':'ignored'})['status'],400)
 def test_unsafe_parent_rejected(self):
  unsafe=self.root/'unsafe';unsafe.mkdir();unsafe.chmod(0o777)
  p=subprocess.run([MANAGER,'--config',str(unsafe/'vpn'),'--runtime',str(self.root/'other-run')],capture_output=True,timeout=5)
  self.assertNotEqual(p.returncode,0);self.assertFalse((unsafe/'vpn').exists())
 def test_future_profile_schema_retained(self):
  ident=self.add();path=self.store/(ident+'.json');data=json.loads(path.read_text());data['schema']=999;path.write_text(json.dumps(data));path.chmod(0o600)
  self.assertEqual(self.rpc('/profiles/'+ident)['status'],404);self.assertEqual(json.loads(path.read_text())['schema'],999)
 def test_corrupt_setting_type_retained(self):
  self.stop();p=self.store/'settings.json';p.write_text('{"schema":1,"kill":"true"}');p.chmod(0o600);self.start()
  self.assertFalse(self.rpc('/capabilities')['data']['available']);self.assertEqual(json.loads(p.read_text())['kill'],'true')
 def test_unreadable_network_record_retained(self):
  self.stop();p=self.run/'network.json';p.write_text('{"schema":1}');p.chmod(0o644);self.start()
  self.assertFalse(self.rpc('/capabilities')['data']['available']);self.assertEqual(self.rpc('/disconnect','POST')['status'],500);self.assertTrue(p.exists())
  self.stop();self.assertEqual(p.read_text(),'{"schema":1}');self.assertEqual(p.stat().st_mode&0o777,0o644);self.start()
 def test_invalid_and_schema(self):
  self.assertEqual(self.rpc('/profiles','POST',{'name':'evil','country':'GB','country_code':'GB','configuration':BASE+'up /evil'})['status'],400)
  self.assertEqual(self.rpc('/settings','PUT',{'selected':'../evil'})['status'],400)
  self.stop();(self.store/'settings.json').write_text('{"schema":999}');(self.store/'settings.json').chmod(0o600);self.start();self.assertFalse(self.rpc('/capabilities')['data']['available']);self.assertIn('schema',self.rpc('/status')['data']['last_error'])
if __name__=='__main__':unittest.main()
