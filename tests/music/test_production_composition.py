"""Production Lua composition plus real native bridge; decoder is instrumented.

This tests software integration, never claims physical decoder qualification.
"""
import heapq,json,sys
from pathlib import Path
from lupa.lua51 import LuaRuntime
from test_ownership_bridge import Driver

def run(binary,root,mode):
 lua=LuaRuntime(unpack_returned_tuples=True);driver=Driver(binary)
 events=[];pending=[];now=0;serial=0;messages=[]
 timer=lua.eval('function(f) return {stop=function(self) f() end} end')
 def table(v):
  return lua.table_from({k:table(value) for k,value in v.items()}) if isinstance(v,dict) else v
 def after(ms,fn):
  nonlocal serial
  serial+=1;item=[now+int(ms),serial,fn,False];heapq.heappush(events,item)
  def cancel():item[3]=True
  return timer(cancel)
 def deliver(body,done):
  if mode=='bridge':
   messages.append(body);after(0,lambda:done(None,'transport_error'));return
  messages.append(body);r=driver.raw(body)
  if r['status']==202:pending.append((body,done))
  else:after(0,lambda:done(table(r.get('reply')),None if r['status']==200 else 'rejected'))
 def pump(ms):
  nonlocal now
  target=now+ms;steps=0
  while events and events[0][0]<=target:
   at,_,fn,cancelled=heapq.heappop(events);now=at;driver.raw('!time '+str(now+1))
   for body,done in list(pending):
    r=driver.raw(body)
    if r['status']!=202:
     pending.remove((body,done));after(0,lambda r=r,done=done:done(table(r.get('reply')),None if r['status']==200 else 'rejected'))
   if not cancelled:fn()
   steps+=1;assert steps<10000,'busy callback loop'
  now=target;driver.raw('!time '+str(now+1))
  for body,done in list(pending):
   r=driver.raw(body)
   if r['status']!=202:
    pending.remove((body,done));after(0,lambda r=r,done=done:done(table(r.get('reply')),None if r['status']==200 else 'rejected'))
 def until(expr,limit=4000):
  began=now
  while not lua.eval(expr):
   assert now-began<limit,(mode,expr,messages[-3:],lua.eval('production.error'),lua.eval('production.binding.client.active'))
   pump(25)
 lua.globals().py_after=after;lua.globals().py_transport=deliver;lua.globals().py_clock=lambda:now
 lua.execute('after=function(...) return py_after(...) end; transport=function(...) return py_transport(...) end; clock=function() return py_clock() end')
 for name in ('MusicOwnership','NativeMusicOwnership','OwnershipRuntime','OwnershipClient','OwnershipBinding','OwnershipDecoder','MusicProduction'):
  lua.globals().path=str(Path(root,'applet',name+'.lua')).replace('\\','/')
  lua.globals().name='applets.StandaloneBase.'+name
  lua.execute('local p=path;package.preload[name]=function() return dofile(p) end')
 lua.globals().mode=mode
 try:
  driver.raw('!idle-policy '+('0' if mode=='producer' else '12'))
  driver.raw('!ready '+('0' if mode in ('ipc','ipc_timeout') else '1'))
  lua.execute('''
   local nop=function() end
   local stock={_audg=nop,_strm=nop,_cont=nop,_aude=nop,_streamHttpHeaders=nop,_streamDisconnect=nop}
   status={decodeState=0,audioState=0,outputFull=0}
   if mode=='decoder' then status.outputFull=1 end
   stops=0
   stock.stopInternal=function(self) stops=stops+1;self.stream=nil end
   playback=setmetatable({source='off',slimproto={send=nop}},{__index=stock})
   if mode=='hooks' then playback._strm=function() end end
   fence={quiet=function() return mode~='fence' end}
   package.loaded['jive.ui.Framework']={}
   package.loaded['jive.ui.Timer']={}
   package.loaded['jive.slim.Player']={getLocalPlayer=function() return {playback=playback} end}
   package.loaded['squeezeplay.decode']={status=function() return status end}
   options={enabled=true,qualifiedFirmware=true,fence=fence,clock=clock,after=after,transport=transport,
    getPlayback=function() return playback end,stockPlayback=stock,stockSend=nop,
    decode=package.loaded['squeezeplay.decode']}
   production=assert(require('applets.StandaloneBase.MusicProduction').new(options))
  ''')
  if mode in ('hooks','fence','producer','decoder','bridge','ipc_timeout'):
   until('production.error~=nil',12000)
   assert not lua.eval('production.admitted'),mode
   assert not any(' admit-idle ' in m for m in messages),messages
  elif mode=='ipc':
   pump(500);assert not lua.eval('production.admitted')
   assert lua.eval('stops')==0
   driver.raw('!ready 1');until('production.admitted')
  else:
   until('production.admitted');assert lua.eval('stops')==1
  if mode in ('good','ipc'):
   assert sum(' admit-idle ' in m for m in messages)==1
   driver.raw('!ready 0');assert driver.raw('!native-request 4')['status']==200
   until('production.binding.client.granted')
   pump(150);assert lua.eval('production.seenBusy')
   driver.raw('!ready 1');until('production.released==true')
   assert lua.eval('production.binding.client.snapshot.owner')==0
   assert lua.eval('stops')==2
   assert sum(' release ' in m for m in messages)==1
  lua.execute('production:shutdown();production:shutdown()')
  print(json.dumps(dict(case=mode,messages=len(messages),software_only=True)))
 finally:driver.close()

if __name__=='__main__':
 binary=['wsl','--exec',sys.argv[2]] if sys.argv[1]=='--wsl' else sys.argv[1]
 root=sys.argv[3] if isinstance(binary,list) else sys.argv[2]
 for mode in ('good','ipc','hooks','fence','producer','decoder','bridge','ipc_timeout'):run(binary,root,mode)
