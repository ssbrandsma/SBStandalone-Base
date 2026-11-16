#!/usr/bin/env python3
"""Privileged integration, entirely inside private mount/network namespaces.
Requires host OpenVPN, OpenSSL, iproute2 and iptables; never runs on a Radio.
"""
import json,os,shutil,signal,socket,subprocess,sys,tempfile,time,urllib.request,threading,http.server
from pathlib import Path
HERE=Path(__file__).resolve().parent
MANAGER=Path(sys.argv[1]).resolve()
if '--inside' not in sys.argv:
 if os.geteuid()!=0:raise SystemExit('Run host integration as root; no device network is touched')
 raise SystemExit(subprocess.call(['unshare','--mount','--net',sys.executable,str(Path(__file__).resolve()),str(MANAGER),'--inside']))
os.umask(0o077)
def run(*args,**kwargs):return subprocess.run(list(map(str,args)),check=True,capture_output=True,**kwargs)
run('mount','--make-rprivate','/')
checks=[];processes=[]
with tempfile.TemporaryDirectory(prefix='sb-vpn-netns-') as folder:
 root=Path(folder);etc=root/'etc';etc.mkdir();(etc/'ssl').mkdir();(etc/'alternatives').mkdir()
 for name in ['iptables','ip6tables']:
  p=Path('/etc/alternatives')/name
  if p.is_symlink():(etc/'alternatives'/name).symlink_to(os.readlink(p))
 for name in ['nsswitch.conf','hosts','protocols','services']:
  p=Path('/etc')/name
  if p.exists():shutil.copyfile(p,etc/name)
 if Path('/etc/ssl/openssl.cnf').exists():shutil.copyfile('/etc/ssl/openssl.cnf',etc/'ssl/openssl.cnf')
 (etc/'squeezeos.version').write_text('9.0.2 r17146\n');(etc/'resolv.conf').write_text('nameserver 192.0.2.1\n');(etc/'resolv.conf').chmod(0o644)
 run('mount','--bind',etc,'/etc');run('ip','link','set','lo','up')
 run('mount','-t','tmpfs','tmpfs','/mnt');Path('/mnt/storage').mkdir();(etc/'standalonebase').mkdir(mode=0o700);Path('/mnt/storage/standalonebase').symlink_to('/etc/standalonebase')
 alias=Path('/mnt/storage/standalonebase');alias.unlink();alias.symlink_to('/etc')
 invalid=subprocess.run([str(MANAGER),'--config','/mnt/storage/standalonebase/vpn','--runtime',str(root/'alias-probe')],capture_output=True,timeout=5)
 assert invalid.returncode!=0 and not Path('/etc/vpn').exists();checks.append('unexpected firmware alias target rejected')
 alias.unlink();alias.symlink_to('/etc/standalonebase')
 ns=subprocess.Popen(['unshare','--net','sleep','300']);processes.append(ns);time.sleep(.15)
 def server(*args):return run('nsenter','-t',ns.pid,'-n',*args)
 def spawn_server(*args,**kwargs):
  p=subprocess.Popen(['nsenter','-t',str(ns.pid),'-n',*map(str,args)],**kwargs);processes.append(p);return p
 try:
  run('ip','link','add','vpnc','type','veth','peer','name','vpns');run('ip','link','set','vpns','netns',ns.pid)
  run('ip','addr','add','192.0.2.2/24','dev','vpnc');run('ip','addr','add','192.0.3.2/24','dev','vpnc');run('ip','-6','addr','add','2001:db8:100::2/64','dev','vpnc','nodad');run('ip','link','set','vpnc','up');run('ip','-6','route','add','default','via','2001:db8:100::1');run('ip','route','add','default','via','192.0.2.1')
  server('ip','link','set','lo','up');server('ip','addr','add','192.0.2.1/24','dev','vpns');server('ip','addr','add','198.51.100.1/32','dev','vpns');server('ip','addr','add','203.0.113.8/32','dev','lo');server('ip','addr','add','9.9.9.9/32','dev','lo');server('ip','addr','add','192.0.3.1/24','dev','vpns');server('ip','-6','addr','add','2001:db8:100::1/64','dev','vpns','nodad');server('ip','-6','addr','add','2001:db8:200::1/128','dev','lo','nodad');server('ip','link','set','vpns','up')
  for role in ['ca','server','client']:
   if role=='ca':run('openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',root/'ca.key','-out',root/'ca.crt','-days','1','-subj','/CN=StandaloneVPNTestCA')
   else:
    run('openssl','req','-newkey','rsa:2048','-nodes','-keyout',root/(role+'.key'),'-out',root/(role+'.csr'),'-subj','/CN=StandaloneVPNTest'+role.title())
    ext=root/(role+'.ext');ext.write_text('basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage='+('serverAuth' if role=='server' else 'clientAuth')+'\n')
    run('openssl','x509','-req','-in',root/(role+'.csr'),'-CA',root/'ca.crt','-CAkey',root/'ca.key','-CAcreateserial','-out',root/(role+'.crt'),'-days','1','-extfile',ext)
  auth=root/'auth.py';auth.write_text('import sys\nfrom pathlib import Path\nlines=Path(sys.argv[-1]).read_text().splitlines()\nsys.exit(0 if lines==["testuser","fixture-password-only"] else 1)\n')
  config=root/'server.ovpn';config.write_text(f'local 198.51.100.1\nport 1194\nproto udp4\ndev vpnsrv\ndev-type tun\ntopology subnet\nserver 10.80.0.0 255.255.255.0\nca {root}/ca.crt\ncert {root}/server.crt\nkey {root}/server.key\ndh none\ndata-ciphers AES-128-GCM\ntls-version-min 1.2\npush "dhcp-option DNS 9.9.9.9"\nkeepalive 2 10\nscript-security 2\nauth-user-pass-verify "/usr/bin/python3 {auth}" via-file\nverb 3\n')
  log=(root/'server.log').open('w');server_process=spawn_server('openvpn','--config',config,stdout=log,stderr=subprocess.STDOUT)
  services=spawn_server(sys.executable,HERE/'ns_services.py',root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
  def fetch(host='203.0.113.8',timeout=2):
   with opener.open('http://'+host+':19090/',timeout=timeout) as r:return json.loads(r.read())['peer']
  for _ in range(80):
   try:assert fetch()=='192.0.2.2';break
   except Exception:time.sleep(.1)
  else:raise AssertionError('controlled external HTTP service not available')
  def fetch6(host='2001:db8:200::1'):
   with opener.open('http://['+host+']:19091/',timeout=1) as r:return json.loads(r.read())['peer']
  assert fetch6()=='2001:db8:100::2'
  run('iptables','-N','KEEP_TEST');run('iptables','-A','KEEP_TEST','-j','RETURN');run('iptables','-A','OUTPUT','-j','KEEP_TEST');run('ip','route','add','blackhole','203.0.114.0/24','proto','99')
  proxy_count=[0];proxy_status=[200];country_count=[0];country_body=[b'{"success":true,"ip":"8.8.4.4","country_code":"US","country":"United States","region":"New York","city":"New York"}']
  class Proxy(http.server.BaseHTTPRequestHandler):
   def log_message(self,*a):pass
   def do_GET(self):
    assert self.headers.get('X-SB-Public-Fetch')=='1'
    if self.path=='/https/api.ipify.org/':proxy_count[0]+=1;body=b'8.8.4.4'
    else:
     assert self.path=='/https/ipwho.is/8.8.4.4?fields=success,ip,country_code,country,region,city';country_count[0]+=1;body=country_body[0]
    self.send_response(proxy_status[0] if self.path=='/https/api.ipify.org/' else 200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
  proxy=http.server.ThreadingHTTPServer(('127.0.0.1',8765),Proxy);threading.Thread(target=proxy.serve_forever,daemon=True).start()
  store=Path('/mnt/storage/standalonebase/vpn');runtime=root/'run';fault_file=root/'fault';manager_env=os.environ.copy();fault_library=os.environ.get('SB_VPN_FAULT_LIBRARY')
  if fault_library:manager_env.update(LD_PRELOAD=fault_library,SB_VPN_FAULT_FILE=str(fault_file))
  manager_log=(root/'manager.log').open('w')
  def start():
   p=subprocess.Popen([str(MANAGER),'--config',str(store),'--runtime',str(runtime)],stdout=manager_log,stderr=subprocess.STDOUT,env=manager_env);processes.append(p)
   for _ in range(100):
    if (runtime/'control.sock').exists():
     try:rpc('/status');return p
     except OSError:pass
    if p.poll() is not None:raise AssertionError((root/'manager.log').read_text())
    time.sleep(.05)
   raise AssertionError('VPN manager startup')
  def rpc(path,method='GET',body=None):
   s=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET);s.settimeout(5);s.connect(str(runtime/'control.sock'));s.send(json.dumps({'path':path,'method':method,'body':body or {}}).encode());r=json.loads(s.recv(131073));s.close();return r
  def expect(path,method='GET',body=None,status=200):
   r=rpc(path,method,body);assert r['status']==status,r;return r['data']
  def wait(state,timeout=20):
   deadline=time.monotonic()+timeout
   while time.monotonic()<deadline:
    s=expect('/status')
    if s['state']==state:return s
    if s['state']=='ERROR' and state!='ERROR':raise AssertionError(s)
    time.sleep(.1)
   raise AssertionError({'status':s,'logs':expect('/logs')})
  manager=start();assert expect('/capabilities')['available'],expect('/capabilities');checks+=['runtime capability detection','trusted firmware configuration-root alias supported']
  profile='client\ndev tun\nproto udp\nremote 198.51.100.1 1194\nremote-cert-tls server\ndata-ciphers AES-128-GCM\ntls-version-min 1.3\nauth-user-pass\n'
  for key,file in [('ca','ca.crt'),('cert','client.crt'),('key','client.key')]:profile+=f'<{key}>\n'+(root/file).read_text()+f'</{key}>\n'
  def add(name):return expect('/profiles','POST',dict(name=name,country='Test country',country_code='GB',configuration=profile,username='testuser',password='fixture-password-only',password_operation='replace'),201)['id']
  ident=add('Controlled server 1');expect('/connect','POST',{'profile_id':ident},202);
  for _ in range(100):
   if (runtime/'client.ovpn').exists():
    (root/'debug-client.ovpn').write_bytes((runtime/'client.ovpn').read_bytes());break
   time.sleep(.03)
  s=wait('CONNECTED',30);assert s['tunnel_interface']=='sbvpn0',s
  assert 'tls-version-min 1.3' in (root/'debug-client.ovpn').read_text() and 'tls-version-min 1.2' not in (root/'debug-client.ovpn').read_text();checks.append('requested TLS minimum preserved')
  assert fetch().startswith('10.80.0.'),s;assert fetch('192.0.2.1')=='192.0.2.2';checks+=['real TLS and service-password authentication','VPN egress through controlled server','LAN access preserved']
  assert fetch('192.0.3.1')=='192.0.3.2';checks.append('secondary connected LAN preserved')
  try:fetch6();raise AssertionError('public IPv6 bypassed VPN protection')
  except OSError:pass
  assert fetch6('2001:db8:100::1')=='2001:db8:100::2';checks.append('public IPv6 blocked while connected local IPv6 remains available')
  try:socket.create_connection(('2001:db8:100::1',53),timeout=1).close();raise AssertionError('IPv6 LAN DNS leak')
  except OSError:pass
  checks.append('IPv6 physical DNS blocked')
  for _ in range(30):
   s=expect('/status')
   if s['public_ip_verified']:break
   time.sleep(.1)
  assert s['public_ip']=='8.8.4.4' and s['public_ip_verified'];count=proxy_count[0];expect('/test','POST',status=202);time.sleep(.2);assert proxy_count[0]==count;checks.append('public IP proxy contract and cached verification')
  assert 'verification' in s and 'traffic' in s and s['public_ip_checked_at']>0
  for _ in range(40):
   telemetry=expect('/status')['traffic']
   if telemetry['available'] and telemetry['rx_bytes']>0 and telemetry['tx_bytes']>0:break
   fetch();time.sleep(.1)
  assert telemetry['interface']=='sbvpn0' and telemetry['source'] in ['sysfs','proc_net_dev'] and telemetry['rx_bytes']>0 and telemetry['tx_bytes']>0,telemetry
  checks.append('owned tunnel native counters and additive telemetry API')
  old_session=telemetry['session_id'];proxy_status[0]=503;expect('/reconnect','POST',status=202);pending=expect('/status');assert not pending['public_ip_verified'] and not pending['verified_country'];wait('CONNECTED',30)
  for _ in range(40):
   failed_verify=expect('/status')
   if failed_verify['verification']['error']:break
   time.sleep(.1)
  assert failed_verify['state']=='CONNECTED' and not failed_verify['public_ip_verified'] and '503' in failed_verify['verification']['error'],failed_verify
  count=proxy_count[0]
  for _ in range(10):expect('/status')
  expect('/test','POST',status=202);time.sleep(.1);assert proxy_count[0]==count
  checks.append('failed external verification is sanitized rate limited and never disconnects VPN')
  proxy_status[0]=200;expect('/reconnect','POST',status=202);wait('CONNECTED',30)
  for _ in range(40):
   s=expect('/status')
   if s['public_ip_verified']:break
   time.sleep(.1)
  assert s['public_ip']=='8.8.4.4' and s['traffic']['session_id']>old_session
  checks.append('explicit reconnect resets accounting and re-verifies without stale IP')
  assert country_count[0]==0 and not s['verified_country'];expect('/settings','PUT',{'country_verify':True})
  # Enabling opt-in uses the independently checked IP without requiring reconnect.
  for _ in range(40):
   s=expect('/status')
   if s['verified_country']:break
   time.sleep(.1)
  assert s['verified_country']=='US' and s['verified_country_name']=='United States' and s['verified_city']=='New York' and not s['configured_country_verified'];assert country_count[0]==1
  expect('/reconnect','POST',status=202);wait('CONNECTED',30)
  for _ in range(40):
   if expect('/status')['verified_country']=='US':break
   time.sleep(.1)
  assert expect('/status')['verified_country']=='US' and country_count[0]==1
  expect('/settings','PUT',{'country_verify':False});assert not expect('/status')['verified_country'];checks.append('country lookup opt-in IP binding cache and explicit disable')
  assert '9.9.9.9' in Path('/etc/resolv.conf').read_text();assert socket.gethostbyname('vpn.test')=='203.0.113.8';assert '10.80.0.' in (root/'dns-peers').read_text();checks.append('DNS routed through tunnel')
  Path('/etc/resolv.conf').write_text('nameserver 192.0.2.53\n')
  for _ in range(50):
   if '9.9.9.9' in Path('/etc/resolv.conf').read_text():break
   time.sleep(.1)
  else:raise AssertionError('DHCP replacement not handled')
  expect('/disconnect','POST',status=202);wait('DISCONNECTED');frozen=expect('/status')['traffic'];assert frozen['completed'] and not frozen['collecting'] and frozen['rx_bytes_per_second']==0 and frozen['tx_bytes_per_second']==0;checks.append('disconnect freezes completed session totals with zero throughput');assert fetch()=='192.0.2.2';assert '192.0.2.53' in Path('/etc/resolv.conf').read_text();assert not run('ip','route','show','proto','186').stdout.strip();checks+=['disconnect restores owned routes','latest DHCP DNS restored']
  assert fetch6()=='2001:db8:100::2';assert 'KEEP_TEST' in run('iptables','-S','OUTPUT').stdout.decode() and run('ip','route','show','proto','99').stdout.strip();checks.append('unrelated firewall and route ownership preserved')
  expect('/settings','PUT',{'kill':True});expect('/connect','POST',status=202);wait('CONNECTED');assert expect('/status')['kill_switch_active'];checks.append('kill switch installation')
  second=add('Controlled server 2');expect('/settings','PUT',{'reconnect':False});peers=[];sampling=True
  def sample():
   while sampling:
    try:peers.append(fetch(timeout=.4))
    except Exception:pass
    time.sleep(.03)
  t=threading.Thread(target=sample);t.start();before_switch=expect('/status')['traffic']['session_id'];expect('/connect','POST',{'profile_id':second},202);switching=expect('/status');assert switching['traffic']['session_id']>before_switch and switching['traffic']['profile_id']==second and not switching['public_ip_verified'];wait('CONNECTED');sampling=False;t.join();assert all(p.startswith('10.80.0.') for p in peers),peers;checks.append('guarded explicit profile switch with auto-reconnect disabled');expect('/settings','PUT',{'reconnect':True})
  if fault_library:
   for fault in ['4insert','6append']:
    fault_file.write_text(fault);expect('/reconnect','POST',status=202);bad=wait('ERROR');assert bad['kill_switch_active'],bad
    try:fetch(timeout=.5);raise AssertionError('protection lost during injected firewall failure')
    except OSError:pass
    assert fetch('192.0.2.1')=='192.0.2.2';expect('/reconnect','POST',status=202);wait('CONNECTED',30)
   checks.append('partial firewall installation failures retain prior fail-closed protection')
  previous=expect('/status')['network_ready_generation'];run('ip','route','del','128.0.0.0/1','proto','186','metric','32760')
  deadline=time.monotonic()+30
  while time.monotonic()<deadline:
   current=expect('/status')
   if current['state']=='CONNECTED' and current['network_ready_generation']>previous:break
   try:peer=fetch(timeout=.3);assert peer.startswith('10.80.0.'),peer
   except OSError:pass
   time.sleep(.1)
  else:raise AssertionError({'missing_route_recovery':current})
  checks.append('deleted owned route detected and restored without physical egress')
  previous=expect('/status')['network_ready_generation'];server('ip','-6','addr','add','2001:db8:101::1/64','dev','vpns','nodad');run('ip','-6','addr','add','2001:db8:101::2/64','dev','vpnc','nodad')
  deadline=time.monotonic()+30
  while time.monotonic()<deadline:
   current=expect('/status')
   if current['state']=='CONNECTED' and current['network_ready_generation']>previous:break
   time.sleep(.1)
  else:raise AssertionError({'ipv6_prefix_recovery':current})
  assert fetch6('2001:db8:101::1')=='2001:db8:101::2'
  try:fetch6();raise AssertionError('public IPv6 after prefix change')
  except OSError:pass
  checks.append('new connected IPv6 prefix triggers guarded recovery and local access')
  before_retry=expect('/status')['traffic'];child=json.loads((runtime/'process.json').read_text())['pid'];os.kill(child,signal.SIGKILL)
  for _ in range(30):
   s=expect('/status')
   if s['state']!='CONNECTED':break
   time.sleep(.05)
  assert s['kill_switch_active'],s
  try:fetch(timeout=.5);raise AssertionError('kill switch leaked after OpenVPN crash')
  except (OSError,urllib.error.URLError):pass
  assert fetch('192.0.2.1')=='192.0.2.2';checks.append('OpenVPN crash fails closed with LAN access')
  wait('CONNECTED',20);after_retry=expect('/status')['traffic'];assert after_retry['session_id']==before_retry['session_id'] and after_retry['rx_bytes']>=before_retry['rx_bytes'];checks.append('automatic reconnect preserves in-memory traffic totals')
  manager.kill();manager.wait();time.sleep(.5);assert 'SBVPN4' in run('iptables','-S','OUTPUT').stdout.decode();manager=start();wait('CONNECTED',30);assert expect('/status')['traffic']['interval_reason']=='manager_restart';checks.append('manager crash recovery starts a disclosed new accounting interval with protection retained')
  country_body[0]=b'{"success":true,"ip":"1.1.1.1","country_code":"NL"}';expect('/settings','PUT',{'country_verify':True})
  manager.kill();manager.wait();manager=start();wait('CONNECTED',30)
  for _ in range(40):
   if country_count[0]>=2:break
   time.sleep(.1)
  assert country_count[0]==2 and not expect('/status')['verified_country'];assert expect('/status')['state']=='CONNECTED';expect('/settings','PUT',{'country_verify':False});checks.append('country response for a different IP rejected without affecting connectivity')

  expect('/disconnect','POST',status=202);wait('DISCONNECTED');assert fetch()=='192.0.2.2';assert 'SBVPN4' not in run('iptables','-S','OUTPUT').stdout.decode();checks.append('explicit disconnect removes protection')
  expect('/settings','PUT',{'reconnect':False})
  bad=expect('/profiles','POST',dict(name='Bad password',country='Test',country_code='GB',configuration=profile,username='testuser',password='fixture-wrong-password',password_operation='replace'),201)['id'];expect('/connect','POST',{'profile_id':bad},202);failed=wait('ERROR',20);assert failed['error_category']=='VPN_AUTHENTICATION_FAILED',failed;assert failed['kill_switch_active'];assert 'fixture-wrong-password' not in json.dumps(expect('/logs'));expect('/disconnect','POST',status=202);wait('DISCONNECTED');checks.append('invalid service credentials fail closed without secret disclosure')
  run('openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',root/'wrong.key','-out',root/'wrong.crt','-days','1','-subj','/CN=WrongTestCA')
  wrong=profile.replace((root/'ca.crt').read_text(),(root/'wrong.crt').read_text());bad=expect('/profiles','POST',dict(name='Bad certificate',country='Test',country_code='GB',configuration=wrong,username='testuser',password='fixture-password-only',password_operation='replace'),201)['id'];expect('/connect','POST',{'profile_id':bad},202);failed=wait('ERROR',20);assert failed['error_category']=='VPN_TLS_ERROR',failed;expect('/disconnect','POST',status=202);wait('DISCONNECTED');checks.append('untrusted server certificate rejected')
  run('ip','route','add','203.0.115.0/24','via','192.0.2.1','proto','99');expect('/connect','POST',{'profile_id':second},202);failed=wait('ERROR');assert failed['error_category']=='VPN_LOCAL_ROUTE_CONFLICT';assert '203.0.115.0/24' in run('ip','route','show','proto','99').stdout.decode();expect('/disconnect','POST',status=202);wait('DISCONNECTED');run('ip','route','del','203.0.115.0/24','via','192.0.2.1','proto','99');checks.append('pre-existing public-specific routes rejected without deletion')
  run('ip','rule','add','priority','100','table','100');expect('/connect','POST',{'profile_id':second},202);failed=wait('ERROR');assert failed['error_category']=='VPN_LOCAL_ROUTE_CONFLICT';assert b'100' in run('ip','rule','show').stdout;expect('/disconnect','POST',status=202);wait('DISCONNECTED');run('ip','rule','del','priority','100','table','100');checks.append('nonstandard policy routing rejected without deleting foreign rules')
  server_process.terminate();server_process.wait(timeout=12);config.write_text(config.read_text().replace('push "dhcp-option DNS 9.9.9.9"\n',''));server_process=spawn_server('openvpn','--config',config,stdout=log,stderr=subprocess.STDOUT);time.sleep(.3)
  expect('/connect','POST',{'profile_id':second},202);failed=wait('ERROR',30);assert failed['error_category']=='VPN_DNS_ERROR',failed;expect('/disconnect','POST',status=202);wait('DISCONNECTED');checks.append('absent pushed DNS fails without silent physical resolver fallback')
  expect('/settings','PUT',{'dns_server':'9.9.9.9'});expect('/connect','POST',{'profile_id':second},202);wait('CONNECTED',30);assert socket.gethostbyname('vpn.test')=='203.0.113.8';expect('/disconnect','POST',status=202);wait('DISCONNECTED');assert '192.0.2.53' in Path('/etc/resolv.conf').read_text();checks.append('explicit fallback DNS uses tunnel and restores physical resolver')
  expect('/settings','PUT',{'reconnect':True,'selected':second})
  expect('/settings','PUT',{'startup':True});manager.terminate();manager.wait(timeout=12);(runtime/'session.json').unlink();manager=start();wait('CONNECTED',30);checks.append('connect on simulated fresh startup')
  run(MANAGER,'--config',store,'--runtime',runtime,'--recover');wait('DISCONNECTED');checks.append('online local recovery command')
  expect('/connect','POST',{'profile_id':second},202);wait('CONNECTED',30);manager.terminate();manager.wait(timeout=12);assert 'SBVPN4' in run('iptables','-S','OUTPUT').stdout.decode();run(MANAGER,'--config',store,'--runtime',runtime,'--recover');assert 'SBVPN4' not in run('iptables','-S','OUTPUT').stdout.decode();assert 'KEEP_TEST' in run('iptables','-S','OUTPUT').stdout.decode();assert not json.loads((store/'settings.json').read_text())['startup'];checks.append('offline recovery removes only owned restrictions and disables startup')
  print(json.dumps({'passed':len(checks),'checks':checks,'scope':'isolated Linux host OpenVPN; no device or public Internet acceptance claimed'},indent=2),flush=True)
 except Exception:
  if (root/'debug-client.ovpn').exists():
   if manager.poll() is None:manager.terminate();manager.wait(timeout=12)
   text=(root/'debug-client.ovpn').read_text();lines=text.splitlines();safe=[];inside=False
   for line in lines:
    if line.startswith('<'):inside=not line.startswith('</');continue
    if not inside and not line.startswith(('management','up ','down ','down-pre','up-restart')):safe.append(line)
   # All key material is synthetic and private to this throwaway test namespace.
   sanitized=[];inside=False
   for line in lines:
    if line.startswith('<'):inside=not line.startswith('</')
    if not inside and not line.startswith('</'):sanitized.append(line)
   print('runtime options', '\n'.join(sanitized),file=sys.stderr)
   live='\n'.join(line for line in lines if not line.startswith(('management','up ','down ','down-pre','up-restart')))+'\n';(root/'debug-live.ovpn').write_text(live)
   debug=subprocess.run(['timeout','8','openvpn','--config',str(root/'debug-live.ovpn')],capture_output=True,text=True)
   print('controlled fixture OpenVPN diagnostics',debug.stdout[-6000:]+debug.stderr[-1000:],file=sys.stderr)
  for file in ['server.log','manager.log']:
   if (root/file).exists():print(file,(root/file).read_text()[-7000:],file=sys.stderr)
  raise
 finally:
  for p in reversed(processes):
   if p.poll() is None:
    p.terminate()
    try:p.wait(timeout=12)
    except subprocess.TimeoutExpired:p.kill();p.wait()
