#!/usr/bin/env python3
"""Actual Jive power/volume request shapes; no device or audible playback."""
import http.client,json,shlex,subprocess,sys,time

def request(path,payload=None):
 c=http.client.HTTPConnection('127.0.0.1',9000,timeout=3)
 c.request('GET' if payload is None else 'POST',path,None if payload is None else json.dumps(payload))
 r=c.getresponse();body=r.read();c.close();assert r.status in (200,201),(r.status,body)
 return json.loads(body)

p=subprocess.Popen(shlex.split(sys.argv[1]),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
try:
 for _ in range(100):
  try:request('/health');break
  except OSError:time.sleep(.03)
 else:raise AssertionError('Base failed to start')
 player='00:04:20:29:16:7f'
 cid=request('/cometd',[{'channel':'/meta/handshake','mac':player}])[0]['clientId']
 def command(args,success=True):
  data=request('/cometd',[{'channel':'/slim/request','clientId':cid,'id':1,'data':{'request':[player,args],'response':'/'+cid+'/reply'}}])
  assert data[0]['successful']==success,data
  return data[1]['data']
 token=request('/api/nowplaying/claim',{'source':'radio','name':'Radio'})['session_id']
 request('/api/nowplaying/update',{'session_id':token,'state':'playing','live':True,'track':{'id':'station:a','title':'Test'}})
 command(['power','0',False,'seq_no:78'])
 state=command(['status','-',10]);assert state['power']==0 and state['seq_no']==78,state
 assert command(['serverstatus',0,10])['players_loop'][0]['power']==0
 commands=request('/api/nowplaying/commands?session_id='+token)['commands']
 assert any(c['command']=='stop' for c in commands),commands
 command(['mixer','volume',19,'seq_no:79'])
 state=command(['status','-',10]);assert state['power']==0 and state['mixer volume']==19 and state['seq_no']==79,state
 # Metadata/heartbeat cannot switch power back on or reset local volume.
 request('/api/nowplaying/update',{'session_id':token,'track':{'title':'Later metadata'}})
 assert command(['status','-',10])['power']==0
 # Delayed older requests must not undo newer standby intent.
 command(['power','1',False,'seq_no:77']);assert command(['status','-',10])['power']==0
 command(['power','banana'],False);command(['mixer','volume',999999],False)
 command(['power','1',False,'seq_no:80']);assert command(['status','-',10])['power']==1
 request('/api/nowplaying/update',{'session_id':token,'state':'stopped'})
 state=command(['status','-',10]);assert state['power']==1 and state['seq_no']==80 and state['mixer volume']==19,state
 print('Standby power boolean, stop delivery, sequence echo, volume and stale-command regression passed')
finally:p.terminate();p.wait(timeout=5)
