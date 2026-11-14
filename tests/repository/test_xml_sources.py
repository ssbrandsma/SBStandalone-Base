#!/usr/bin/env python3
"""Exercise XML persistence, trust boundaries, conflicts and proxy refresh."""
import http.server,json,os,socket,subprocess,sys,tempfile,threading,time,io,zipfile,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'monitor'))
from test_monitor_http import request,pair

def xml(name='Example',sha='a'*40,target='baby',version='1.0'):
 return ('<extensions><applets><applet name="'+name+'" version="'+version+'" target="'+target+'" minTarget="7.7.2" maxTarget="9.*"><title>Example &amp; friends</title><url>packages/example.zip</url><sha>'+sha+'</sha></applet></applets></extensions>').encode()

def main():
 executable,web=sys.argv[1:3]
 package=io.BytesIO()
 with zipfile.ZipFile(package,'w') as z:z.writestr('RemoteMeta.lua','return {}');z.writestr('RemoteApplet.lua','return {}')
 package=package.getvalue();digest=hashlib.sha1(package).hexdigest()
 control={'body':xml('Remote',sha=digest),'guard':True,'status':200}
 class Proxy(http.server.BaseHTTPRequestHandler):
  def log_message(self,*a):pass
  def do_GET(self):
   health=self.path=='/health';self.send_response(200 if health else control['status'])
   if health:
    self.send_header('X-SB-Public-Fetch','1')
    if control['guard']:self.send_header('X-SB-Repository-Fetch','1')
   else:
    assert self.headers.get('X-SB-Repository-Fetch')=='1'
   body=b'ok' if health else package if self.path.endswith('.zip') else control['body'];self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
 proxy=http.server.ThreadingHTTPServer(('127.0.0.1',0),Proxy);threading.Thread(target=proxy.serve_forever,daemon=True).start()
 with tempfile.TemporaryDirectory() as folder:
  root=Path(folder);config=root/'config.json';original=json.dumps({'server':{'name':'Preserve'},'applets':[]}).encode();config.write_bytes(original)
  sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close();base=f'http://127.0.0.1:{port}'
  env=os.environ|{'SB_CATALOG_SYNC_DISABLED':'1','SB_XML_TEST_PROXY':f'http://127.0.0.1:{proxy.server_port}','SB_XML_TEST_FIRMWARE':'9.0.1.17084','SB_REPOSITORY_INTERNAL_ROOT':str(root/'repo'),'SBBASE_PID_FILE':str(root/'absent.pid')}
  def start():
   p=subprocess.Popen([executable,'--port',str(port),'--web-root',web,'--config-dir',str(root)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
   for _ in range(100):
    try:socket.create_connection(('127.0.0.1',port),.1).close();return p
    except OSError:time.sleep(.05)
   raise AssertionError('server startup')
  process=start()
  try:
   endpoint='/api/applets/repositories';assert request(base,endpoint)[0]==401;cookie=pair(base,root)
   def call(path='',method='GET',body=None):return request(base,endpoint+path,cookie,method=method,body=body,headers={'Origin':base})
   def upload(body):return call('/upload?name=Static&filename=extensions.xml&base_url=https%3A%2F%2Fexample.com%2Fextensions.xml','POST',body)
   code,body,_=upload(xml());assert code==201,(code,body);ident=json.loads(body)['id']
   assert config.read_bytes()==original
   view=json.loads(request(base,'/api/applets',cookie)[1]);assert len(view['applets'])==1;assert view['applets'][0]['repository_id']==ident
   snapshot=root/'applet-sources.json';stored=json.loads(snapshot.read_text());assert stored['repositories'][0]['items'][0]['url']=='https://example.com/packages/example.zip'
   for bad in [b'<!DOCTYPE extensions [<!ENTITY x SYSTEM "file:///etc/passwd">]><extensions>&x;</extensions>',b'<extensions>',b'\xff',b'x'*262145]:
    assert upload(bad)[0] in (400,413)
   assert snapshot.read_text()==json.dumps(stored,separators=(',',':')) or json.loads(snapshot.read_text())==stored
   assert call('', 'POST', {'name':'Unsafe','url':'http://127.0.0.1/repo.xml'})[0]==400
   code,body,_=upload(xml(sha='b'*40,target='*'));assert code==201
   conflict=json.loads(request(base,'/api/applets',cookie)[1])['applets'];assert all(x['blocked'] and x['conflict'] for x in conflict)
   second=json.loads(body)['id'];assert call('/'+second,'DELETE')[0]==200
   assert call('/'+ident,'PUT',{'name':'Renamed'})[0]==200
   code,body,_=call('', 'POST',{'name':'Remote','url':'https://example.com/extensions.xml'});assert code==201;remote=json.loads(body)['id']
   def wait(state):
    for _ in range(100):
     rows=json.loads(call()[1])['repositories'];r=next(x for x in rows if x['id']==remote)
     if not r['busy'] and state(r):return r
     time.sleep(.05)
    raise AssertionError(rows)
   wait(lambda r:r['imported']==1)
   code,download,_=request(base,'/applets/remote/'+digest+'.zip',cookie);assert code==200 and download==package
   code,download,_=request(base,'/applets/remote/'+digest+'.zip',cookie);assert code==200 and download==package
   control['body']=b'invalid';assert call('/'+remote+'/refresh','POST',{})[0]==202
   r=wait(lambda r:bool(r['last_error']));assert r['imported']==1 and 'cached' in r['status']
   control['guard']=False;assert call('/'+remote+'/refresh','POST',{})[0]==202
   r=wait(lambda r:'repository-fetch' in r['last_error']);assert r['imported']==1
   process.terminate();process.wait(timeout=3);process=start();cookie=pair(base,root)
   rows=json.loads(call()[1])['repositories'];assert len(rows)==2 and rows[0]['name']=='Renamed';assert config.read_bytes()==original
   assert call('/'+ident,'DELETE')[0]==200;assert call('/'+remote,'DELETE')[0]==200
   assert json.loads(request(base,'/api/applets',cookie)[1])['applets']==[]
  finally:process.terminate();process.wait(timeout=3)
 proxy.shutdown();print('XML repository API, proxy, persistence and conflict checks passed')
if __name__=='__main__':main()
