#!/usr/bin/env python3
"""Actual Radio watchdog methods with controlled decoder/timer boundaries.
No VPN dependency is added to Radio and no physical playback claim is made.
"""
import sys
from pathlib import Path
try:
 from lupa.lua54 import LuaRuntime
except ImportError:
 print('SKIP: development-only lupa.lua54 required');raise SystemExit(77)
source=Path(sys.argv[1],'applet/StandaloneRadio/StreamPlayer.lua').read_text()
def method(name):
 first=source.index('function StreamPlayer:'+name+'(');last=source.find('\nfunction StreamPlayer:',first+1)
 return source[first:last if last>=0 else len(source)]
lua=LuaRuntime()
lua.execute("""
StreamPlayer={};local StreamPlayer=StreamPlayer
power='on';bytes=100;playback={stream=true};timers={};begins=0
jiveMain={getSoftPowerState=function()return power end}
decode={status=function()return {bytesReceivedL=bytes}end}
Timer=function(delay,callback,once)local t={callback=callback,start=function()end,stop=function(self)self.stopped=true end};timers[#timers+1]=t;return t end
""")
lua.execute('local StreamPlayer=StreamPlayer;local WATCHDOG_STALL_POLLS=2;local reconnectDelays={2000,5000,10000,30000};local localPlayback=function()return playback end;'+
 '\n'.join(method(n) for n in ['_isStandby','_standbyFence','_isCurrent','_resetWatchdog','_resetAudioRecovery','_watchdogTick','_cancelReconnect','_scheduleReconnect','stop','pause']))
lua.execute("""
station={id='same-station',logo='same-logo',hls=true};metadata={title='same-title'}
function player()
 return setmetatable({generation=1,stationGeneration=1,retryIndex=1,desiredStation=station,lastStation=station,ownsPlayback=true,playbackActive=true,currentMetadata=metadata,volume=37,
 callbacks={},log={warn=function()end,info=function()end},_notifyState=function(self,s,state)self.state=state end,_stopPlayback=function()end,
 _begin=function(self,s,reconnect)assert(s==station and reconnect);begins=begins+1 end},{__index=StreamPlayer})
end
-- Healthy streams survive network-policy changes without forced restart/reset.
p=player();for i=1,10 do bytes=bytes+100;p:_watchdogTick()end
assert(#timers==0 and begins==0 and p.currentMetadata==metadata and p.volume==37 and p.desiredStation.logo=='same-logo')
-- Stalled playback schedules exactly one bounded retry, retaining identity/intent.
p:_watchdogTick();p:_watchdogTick();assert(p.reconnectTimer and #timers==1)
for i=1,20 do p:_watchdogTick()end;assert(#timers==1)
timers[1].callback();assert(begins==1 and p.desiredStation==station and p.volume==37)
-- Stop/pause during the queued retry invalidates even a late timer callback.
for _,action in ipairs({'stop','pause'})do
 local q=player();playback.stream=nil;q:_watchdogTick();local pending=q.reconnectTimer;assert(pending)
 q[action](q);local before=begins;pending.callback();assert(begins==before and not q.playbackActive and not q.desiredStation)
end
-- Paused, finished and foreign-owner states never initiate recovery.
for _,key in ipairs({'paused','episodeEOF','intentionalStop'})do local q=player();q[key]=true;local before=#timers;q:_watchdogTick();assert(#timers==before)end
local q=player();q.playbackActive=false;local before=#timers;q:_watchdogTick();assert(#timers==before)
print('Radio watchdog: healthy stream retained, one retry, stop/pause fence, metadata/volume identity and inactive intent passed')
""")
