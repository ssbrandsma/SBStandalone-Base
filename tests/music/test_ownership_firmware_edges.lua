local root=assert(arg[1])
local Decoder=dofile(root..'/applet/OwnershipDecoder.lua')
local Hooks=dofile(root..'/applet/OwnershipHooks.lua')
local function run(kind)
 local clock,queue,result,count=0,{},nil,0
 local state={decodeState=32,audioState=32,outputFull=0}
 local pb={source=kind=='capture' and 'capture' or 'off',stream={}}
 function pb:stopInternal() count=count+1;if kind=='throw' then error('stop failed') end;self.stream=nil end
 local options={qualified=true,playback=pb,decode={status=function() return state end},
  clock=function() return clock end,after=function(ms,fn) queue[#queue+1]={clock+ms,fn} end,
  valid=function() return kind~='stale' end,timeout=100}
 Decoder.stop(options,function(ok) assert(result==nil);result=ok end)
 if kind=='capture' or kind=='throw' then assert(result==false);return end
 if kind=='good' then state.decodeState=0;state.audioState=0 end
 if kind=='missing' then state={} end
 if kind=='fifo' then state.decodeState=0;state.audioState=0;state.outputFull=1 end
 local samples=0
 while #queue>0 do local item=table.remove(queue,1);clock=item[1];item[2]();samples=samples+1;assert(samples<20) end
 assert(result==(kind=='good'))
 if kind=='good' then assert(samples==2 and clock==50) end
 assert(count==1)
end
for _,kind in ipairs({'good','capture','throw','stale','missing','fifo','running'}) do run(kind) end
local original,newer=function() end,function() end
local target={fn=original};local h=Hooks.new()
assert(h:install(target,'fn',function(old) return function(...) return old(...) end end))
local wrapper=target.fn;assert(h:install(target,'fn',function() error('stack') end))
target.fn=newer;assert(not h:restore());assert(target.fn==newer)
target.fn=wrapper;assert(h:restore());assert(h:restore());assert(target.fn==original)
assert(not h:install(target,'fn',function() return newer end))
print('Firmware queued-stop observation, two turns, missing/running/FIFO/capture/error/stale refusal and exact hook restoration passed')

-- New explicit Spotify composition must also work after Lua5.1 module isolation.
package.loaded['loop.simple']={class=function() end}
package.loaded['jive.slim.Player']={}
package.loaded['squeezeplay.decode']={}
package.loaded['squeezeplay.stream']={}
package.preload['applets.StandaloneBase.OwnershipRuntime']=function() return dofile(root..'/applet/OwnershipRuntime.lua') end
local chunk=loadfile(root..'/../../SBSpotifyConnect/applet/SpotifyPlayback.lua')
if chunk and module then
    chunk('ownership.spotify.fixture')
    assert(package.loaded['ownership.spotify.fixture'].createOwnership({}, {enabled=false})==nil)
end
