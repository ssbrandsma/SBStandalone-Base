#!/usr/bin/env python3
"""Exercise the persisted XML projection through the real installer protocol."""
import http.client,json,signal,subprocess,sys,tempfile,time,shlex
from pathlib import Path

def post(payload):
 c=http.client.HTTPConnection('127.0.0.1',9000,timeout=2);c.request('POST','/cometd',json.dumps([payload]),{'Content-Type':'application/json'});r=c.getresponse();data=json.loads(r.read());c.close();assert r.status==200;return data

def main():
 with tempfile.TemporaryDirectory() as folder:
  root=Path(folder);config=root/'config.json';config.write_text(json.dumps({'applets':[{'name':'BaseOnly','target':'baby'}]}))
  st=config.stat();revision=f'{st.st_ino}:{st.st_size}:{int(st.st_mtime)}:{int(st.st_ctime)}'
  items=[{'name':f'Extra{i}','target':'baby','version':'1.0'} for i in range(12)]
  items += [dict(name='Versioned',target='baby',version='1.9',min_target_version='7.7.2',max_target_version='9.*'),dict(name='Versioned',target='baby',version='1.10',min_target_version='9.0.0'),dict(name='Blocked',target='baby',blocked=True),dict(name='TooNew',target='baby',min_target_version='10.0'),dict(name='Shared',target='baby|fab4',max_target_version='9.*')]
  (root/'applet-sources.json').write_text(json.dumps({'schema':1,'base_revision':revision,'applets':items,'repositories':[{'xml':' '*200000}]}))
  p=subprocess.Popen(shlex.split(sys.argv[1])+['--config',str(config)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  try:
   for _ in range(80):
    try:client=post({'channel':'/meta/handshake'})[0]['clientId'];break
    except OSError:time.sleep(.05)
   else:raise AssertionError('startup')
   def catalog(target,version):
    events=post({'channel':'/slim/request','clientId':client,'data':{'response':'/'+client+'/slim/request','request':['',['jiveapplets','target:'+target,'version:'+version]]}})
    result=next(x['data'] for x in events if 'data' in x);assert result['count']==len(result['item_loop']);return result['item_loop']
   for firmware,version in [('7.7.3','1.9'),('9.0.1.17084','1.10')]:
    found=catalog('baby',firmware);assert len(found)==14,found;assert next(x['version'] for x in found if x['name']=='Versioned')==version
   assert [x['name'] for x in catalog('fab4','9.0.1')]==['Shared']
   config.write_text(json.dumps({'applets':[{'name':'ChangedBase','target':'baby'}]}));p.send_signal(signal.SIGHUP);time.sleep(.1)
   assert [x['name'] for x in catalog('baby','9.0.1')]==['ChangedBase']
  finally:p.terminate();p.communicate(timeout=3)
 print('XML projection, >10 entries, numeric versions, bounds, conflicts and stale snapshot fallback passed')
if __name__=='__main__':main()
