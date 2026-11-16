#!/usr/bin/env python3
"""Real async VPN IPC bridge, authentication, CSRF, limits and secret redaction."""
import json,os,socket,subprocess,sys,tempfile,time,urllib.request
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'monitor'))
from test_monitor_http import request,pair
MANAGER,WEB,ASSETS=sys.argv[1:4]
CONFIG='client\ndev tun\nremote 198.51.100.1 1194\nremote-cert-tls server\n<ca>\nTEST CA\n</ca>\n'
with tempfile.TemporaryDirectory(prefix='vpn-http-') as folder:
 root=Path(folder);config=root/'config';config.mkdir();(config/'config.json').write_text('{"applets":[]}');runtime=root/'run'
 manager=subprocess.Popen([MANAGER,'--config',str(config/'vpn'),'--runtime',str(runtime)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close();base=f'http://127.0.0.1:{port}'
 env=os.environ|{'SB_VPN_TEST_SOCKET':str(runtime/'control.sock'),'SB_CATALOG_SYNC_DISABLED':'1'}
 web=subprocess.Popen([WEB,'--port',str(port),'--web-root',ASSETS,'--config-dir',str(config)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 try:
  for _ in range(100):
   try:
    socket.create_connection(('127.0.0.1',port),.1).close()
    if (runtime/'control.sock').exists():break
   except OSError:pass
   time.sleep(.03)
  assert request(base,'/api/vpn/status')[0]==401
  assert request(base,'/api/internal/vpn/status')[0]==401
  cookie=pair(base,config)
  def call(path,method='GET',body=None,origin=base):return request(base,'/api/vpn'+path,cookie,method=method,body=body,headers={'Origin':origin})
  assert call('/status')[0]==200
  assert call('/connect','POST',{},'https://evil.example')[0]==403
  assert call('/profiles','POST',{'configuration':'x'*131072})[0]==413
  r=call('/profiles','POST',dict(name='HTTP test',country='United Kingdom',country_code='GB',configuration=CONFIG,username='test-user',password='never-return-this',password_operation='replace'))
  assert r[0]==201,r;ident=json.loads(r[1])['id']
  for path in ['/profiles','/profiles/'+ident,'/status','/logs','/capabilities']:
   code,body,_=call(path);assert code==200,(path,code,body);assert b'never-return-this' not in body and b'TEST CA' not in body
  assert call('/profiles/'+ident+'/validate','POST',{})[0]==200
  assert call('/event','POST',{'type':'up'})[0]==403
  cap=(config/'remote.capability').read_text().strip();assert request(base,'/api/internal/vpn/status',headers={'X-Remote-Capability':cap})[0]==200
  assert request(base,'/api/internal/vpn/profiles',method='POST',body={},headers={'X-Remote-Capability':cap})[0]==403
  (config/'settings.conf').write_text('port='+str(port)+'\nreadonly=1\n')
  assert call('/disconnect','POST',{})[0]==403
  (config/'settings.conf').write_text('port='+str(port)+'\nreadonly=0\n')
  assert call('/profiles/'+ident,'DELETE')[0]==200
  # Static source assets and package flattening both reference the new page.
  index=Path(ASSETS,'index.html').read_text();assert 'data-page="vpn"' in index and 'vpn.js?v=4' in index
  assert 'vpn:window.vpnPage' in Path(ASSETS,'js/app.js').read_text()
  print('VPN HTTP: auth, trusted Jive access, CSRF, bounds, async CRUD, validation, redaction and read-only policy passed')
 finally:
  web.terminate();web.wait(timeout=5);manager.terminate();manager.wait(timeout=12)
