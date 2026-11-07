-- Real Radio/Spotify constructor and start methods; instrumented firmware only.
local base,radioRoot,spotifyRoot=assert(arg[1]),assert(arg[2]),assert(arg[3])
local function nop() end
package.loaded['applets.StandaloneBase.OwnershipHooks']=dofile(base..'/applet/OwnershipHooks.lua')
local timers={}
package.loaded['loop.simple']={class=nop}
package.loaded['jive.ui.Timer']=function(_,callback)
 local t={callback=callback,start=function(self) self.running=true end,stop=function(self) self.running=false end}
 timers[#timers+1]=t;return t
end
package.loaded['jive.slim.Player']={getLocalPlayer=function() error('fenced path reached physical player') end}
package.loaded['jive.ui.Framework']={wakeup=nop}
package.loaded['squeezeplay.decode']={start=function() error('decoder must remain unused') end}
package.loaded['squeezeplay.stream']={}
for _,name in ipairs({'jive.net.RequestHttp','jive.net.SocketHttp','jive.ui.Task',
 'applets.StandaloneRadio.Resolver','applets.StandaloneRadio.UrlTransport','applets.StandaloneRadio.OpmlClient',
 'applets.StandaloneRadio.HlsMetadata'}) do package.loaded[name]={new=function() return {} end} end
local names={'applets.StandaloneRadio.StreamPlayer','applets.SpotifyConnect.SpotifyService','applets.SpotifyConnect.SpotifyPlayback'}
local paths={radioRoot..'/applet/StandaloneRadio/StreamPlayer.lua',spotifyRoot..'/applet/SpotifyService.lua',spotifyRoot..'/applet/SpotifyPlayback.lua'}
for i,name in ipairs(names) do
 local file=paths[i]
 package.preload[name]=function()
  -- The target's vendor Lua supports bit operators. Lua 5.4 supplies those
  -- operators on the host; emulate only the legacy module environment.
  local env=setmetatable({module=nop},{__index=_G});env._M=env
  assert(loadfile(file,'t',env))(name);return env
 end
end
local Fence=dofile(base..'/applet/MusicIdleFence.lua')
local f=assert(Fence.install())
assert(f:quiet())
local Radio=require(names[1]);local Service=require(names[2]);local Player=require(names[3])
local r=Radio.new({log={info=nop,warn=nop},callbacks={onState=nop}})
local s=setmetatable({},{__index=Service});s:init({})
local p=setmetatable({},{__index=Player});p:init({})
assert(f:quiet());assert(r.ownership and not r.ownership:canStart())
assert(s.ownershipFenced and not s.desiredRunning)
assert(s:start(false)==false);assert(s:restart()==false)
assert(p:start('127.0.0.1',1,'/never')==false)
assert(r:_installHooks({})==false)
for _,timer in ipairs(timers) do assert(not timer.running) end
-- Delayed start paths and mutations cannot turn an idle fence into a grant.
assert(not p.ownership:canStart());assert(not r.ownership:request())
s.desiredRunning=true;assert(not f:quiet());s.desiredRunning=false
r.pendingStation={};assert(not f:quiet());r.pendingStation=nil
local wrapper=Radio.new;Radio.new=function() end;assert(not f:quiet());Radio.new=wrapper
assert(f:quiet());f:shutdown();f:shutdown();assert(not f:quiet())
local refused,why=Fence.install();assert(not refused and why=='legacy_module_already_loaded')
print('Real application constructor fencing, stale starts, hooks and reload refusal passed; no physical decoder used')
