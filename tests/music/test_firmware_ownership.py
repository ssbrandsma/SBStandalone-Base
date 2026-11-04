# Actual Lua profiles/app methods against native protocol; instrumented decoder/producers.
from pathlib import Path
import json,sys,time,heapq
from lupa.lua54 import LuaRuntime
from test_ownership_bridge import Driver

def run(binary,base,radio,spotify,qobuz):
    start=time.monotonic();lua=LuaRuntime(unpack_returned_tuples=True)
    driver=Driver(binary);queue=[];pending={};now=0;order=0;messages=0
    def table(value):
        return lua.table_from({k:table(v) for k,v in value.items()}) if isinstance(value,dict) else value
    def after(ms,fn):
        nonlocal order
        order+=1;heapq.heappush(queue,(now+int(ms),order,fn))
    def transport(body,done):
        nonlocal messages
        messages+=1;driver.raw('!time '+str(now+1));result=driver.raw(body)
        if result['status']==202:pending[int(body.split()[2])]=(body,done)
        else:after(0,lambda:done(table(result.get('reply')),None if result['status']==200 else 'rejected'))
        for owner,(query,callback) in list(pending.items()):
            value=driver.raw(query)
            if value['status']!=202:
                del pending[owner]
                after(0,lambda cb=callback,v=value:cb(table(v.get('reply')),None if v['status']==200 else 'rejected'))
    def pump(ms=0):
        nonlocal now
        target=now+ms;count=0
        while queue and queue[0][0]<=target:
            now,_,fn=heapq.heappop(queue);fn();count+=1
            assert count<10000,'busy Lua event loop'
        now=target;driver.raw('!time '+str(now+1))
    def until(expression,limit=1000):
        began=now
        while not lua.eval(expression):
            assert now-began<limit,expression
            pump(5)
        return now-began
    lua.globals().after=after;lua.globals().clock=lambda:now;lua.globals().transport=transport
    lua.execute("local a,c,t=after,clock,transport;after=function(...) return a(...) end;clock=function() return c() end;transport=function(...) return t(...) end")
    def preload(name,path):
        lua.globals().module_name=name;lua.globals().module_path=str(path).replace('\\','/')
        lua.execute('local p=module_path;package.preload[module_name]=function() return dofile(p) end')
    for p in Path(base,'applet').glob('Ownership*.lua'):preload('applets.StandaloneBase.'+p.stem,p)
    for name in ['MusicOwnership','NativeMusicOwnership']:preload('applets.StandaloneBase.'+name,Path(base,'applet',name+'.lua'))
    preload('radioProfile',Path(radio,'applet/StandaloneRadio/RadioOwnership.lua'))
    preload('spotifyProfile',Path(spotify,'applet/SpotifyOwnership.lua'))
    preload('qobuzProfile',Path(qobuz,'applet/QobuzConnect/QobuzOwnership.lua'))
    lua.execute(r'''
    timers,trace={},{}
    local nop=function() end
    log={info=nop,warn=nop,error=nop,debug=nop}
    package.loaded['loop.simple']={class=nop}
    package.loaded['jive.ui.Timer']=function(ms,callback,once)
        local t={callback=callback,start=function(self) self.running=true end,stop=function(self) self.running=false end}
        timers[#timers+1]=t;return t
    end
    status={decodeState=0,audioState=0,outputFull=0}
    decoder={status=function() return status end,start=function() status.decodeState=1;status.audioState=1 end,
        resumeAudio=function() trace[#trace+1]='resume' end,audioGain=function() trace[#trace+1]='gain' end}
    package.loaded['squeezeplay.decode']=decoder
    package.loaded['squeezeplay.stream']={icyMetaInterval=nop}
    playback={source='off',slimproto={send=nop,disconnect=function() error('ownership must retain Base connection') end},
        stopInternal=function(self)
            self.stream=nil;status.decodeState=32;status.audioState=32
            after(25,function() status.decodeState=0;status.audioState=0;status.outputFull=0 end)
        end,_getGainFromVolume=function() return 1 end,_streamConnect=function(self) self.stream={} end,
        pause=nop,_audg=nop,_aude=nop,_strm=nop,_cont=nop,_streamHttpHeaders=function() trace[#trace+1]="headers" end,_streamDisconnect=function() trace[#trace+1]="disconnect" end}
    player={playback=playback,incrementSequenceNumber=nop,volumeLocal=nop}
    package.loaded['jive.slim.Player']={getLocalPlayer=function() return player end,getCurrentPlayer=function() return player end}
    package.loaded['jive.ui.Framework']={wakeup=nop}
    for _,name in ipairs({'jive.net.RequestHttp','jive.net.SocketHttp','jive.ui.Task',
        'applets.StandaloneRadio.Resolver','applets.StandaloneRadio.UrlTransport','applets.StandaloneRadio.OpmlClient',
        'applets.StandaloneRadio.HlsMetadata','applets.QobuzConnect.QobuzLifecyclePolicy'}) do
        package.loaded[name]={new=function() return {} end}
    end
    function loadApp(path)
        local env=setmetatable({module=nop,playback=false},{__index=_G});env._M=env
        assert(loadfile(path,'t',env))();return env
    end
    function object(proto,fields) return setmetatable(fields or {},{__index=proto}) end
    function options()
        return {enabled=true,clock=clock,after=after,transport=transport,qualifiedFirmware=true,
            decode=decoder,getPlayback=function() return playback end}
    end
    function service(proto)
        local s=object(proto,{operation=0,applet={setServiceActive=nop}})
        s.ownershipDriver={stop=function(_,operation,done)
            after(50,function() done({operation=operation,producer_stopped=true,restart_fenced=true}) end)
        end}
        return s
    end
    ''')
    for name,value in [('radioRoot',radio),('spotifyRoot',spotify),('qobuzRoot',qobuz)]:lua.globals()[name]=str(value).replace('\\','/')
    try:
        driver.raw('!ready 1')
        lua.execute(r'''
        local r=loadApp(radioRoot..'/applet/StandaloneRadio/StreamPlayer.lua')
        radioPlayer=r.new({log=log,callbacks={onState=function() end}})
        ro=options();ro.player=radioPlayer;rg=assert(require('radioProfile').new(ro))
        spotifyPlayer=object(loadApp(spotifyRoot..'/applet/SpotifyPlayback.lua'),{})
        spotifyService=service(loadApp(spotifyRoot..'/applet/SpotifyService.lua'))
        so=options();so.playback=spotifyPlayer;so.service=spotifyService;sg=assert(require('spotifyProfile').new(so))
        qobuzPlayer=object(loadApp(qobuzRoot..'/applet/QobuzConnect/QobuzPlayback.lua'),{})
        qobuzPlayer:init(log,function() end)
        qobuzService=service(loadApp(qobuzRoot..'/applet/QobuzConnect/QobuzService.lua'))
        qo=options();qo.playback=qobuzPlayer;qo.service=qobuzService;qo.connectionReady=function() return true end
        qg=assert(require('qobuzProfile').new(qo))
        mo=options();mo.attach=function() return true end;mo.fenceMusic=function() return true end
        mo.stopResources=function(done) after(50,function() done(true) end) end
        mo.releaseMusicHooks=function(done) done(true) end
        mg=assert(require('applets.StandaloneBase.NativeMusicOwnership').new(mo));gates={rg,sg,qg,mg}
        ''')
        pump();lua.execute('for _,g in ipairs(gates) do assert(g:reconcile("idle")) end');pump(200)
        lua.execute('collectgarbage("collect")')
        idle_heap=lua.eval('collectgarbage("count")');idle_messages=messages
        pump(1000);assert messages==idle_messages

        lua.execute('assert(rg:request())');pump(20)
        lua.execute('assert(rg:canStart());radioPlayer.ownsPlayback=true;radioPlayer.desiredStation={};radioPlayer:_installHooks(playback)')
        # Standby holds admission, physically releases Radio, then requires a fresh request.
        lua.execute('standbyDone=nil;assert(rg:setStandby(true,function(ok) standbyDone=ok end))');pump(200)
        lua.execute('assert(standbyDone==true);assert(not rg:canStart());assert(mg:request())');pump(20)
        lua.execute('assert(not mg:canStart());assert(mg.client.active);assert(rg:setStandby(false))');pump(20)
        lua.execute('assert(not rg:canStart());assert(rg:request())');pump(20)
        lua.execute('assert(rg:canStart());radioPlayer.ownsPlayback=true;radioPlayer.desiredStation={};radioPlayer:_installHooks(playback)')
        transitions=[]
        for index,name in [(1,'radio'),(2,'spotify'),(3,'qobuz')]:
            if index!=1:lua.execute(f'assert(gates[{index}]:request())');pump(20)
            lua.execute(f'assert(gates[{index}]:canStart())')
            if index==2:lua.execute('assert(spotifyPlayer:start("127.0.0.1",17880,"/spotify.ogg"))')
            if index==3:lua.execute('assert(qobuzPlayer:start("http://127.0.0.1/music",1,"flac"))')
            if index==1:lua.execute('retiredHeaders=playback._streamHttpHeaders;retiredDisconnect=playback._streamDisconnect')
            if index==3:lua.execute('retiredQobuzHeaders=playback._streamHttpHeaders')
            before=now;lua.execute('assert(mg:request())');until('mg:canStart()')
            lua.execute(f'assert(mg:canStart());assert(not gates[{index}]:canStart())')
            if index==1:lua.execute('assert(not radioPlayer:_begin({},false));assert(not radioPlayer:_isCurrent(radioPlayer.generation,radioPlayer.desiredStation))')
            if index==2:lua.execute('assert(not spotifyPlayer:resume());assert(not spotifyPlayer:setRemoteVolume(50));assert(not spotifyService:start(false))')
            if index==3:lua.execute('assert(not qobuzPlayer:resume());assert(not qobuzPlayer:setVolume(50));assert(not qobuzService:_launch())')
            transitions.append({'from':name,'to':'music','simulated_elapsed_ms':now-before})
            before_return=now;lua.execute(f'assert(gates[{index}]:request())');until(f'gates[{index}]:canStart()')
            lua.execute(f'assert(gates[{index}]:canStart());assert(not mg:canStart())')
            if index==1:lua.execute('local before=#trace;retiredHeaders(playback,"HTTP/1.0 200 OK\\r\\n\\r\\n");retiredDisconnect(playback,0,false);assert(#trace==before)')
            if index==3:lua.execute('local before=#trace;retiredQobuzHeaders(playback,"HTTP/1.0 200 OK\\r\\n\\r\\n");assert(#trace==before)')
            transitions.append({'from':'music','to':name,'simulated_elapsed_ms':now-before_return})
            lua.execute(f'assert(gates[{index}]:reconcile("release"))');pump(200)
        lua.execute('assert(sg:request())');pump(20)
        lua.execute('spotifyService.ownershipDriver=nil;assert(mg:request())');pump(100)
        lua.execute('assert(not mg:canStart());assert(not sg:canStart());assert(sg.client.active)')
        # Reconcile refused producers explicitly, then exercise real HLS/hook paths.
        lua.execute('spotifyService.ownershipDriver=service({}).ownershipDriver;assert(sg:reconcile("release"))');pump(200)
        lua.execute('assert(rg:request())');pump(20)
        lua.execute('radioPlayer.desiredStation={hls=true};radioPlayer.ownsPlayback=true;assert(mg:request())');pump(200)
        lua.execute('assert(not mg:canStart());assert(not rg:canStart());ro.hlsStop=function(_,done) after(25,function() done(true) end) end;assert(rg:reconcile("release"))');pump(200)
        lua.execute('assert(qg:request())');pump(20)
        lua.execute('assert(qobuzPlayer:start("http://127.0.0.1/music",2,"mp3"));savedHeader=playback._streamHttpHeaders;newerHeader=function() end;playback._streamHttpHeaders=newerHeader;assert(mg:request())');pump(200)
        lua.execute('assert(not mg:canStart());assert(playback._streamHttpHeaders==newerHeader);playback._streamHttpHeaders=savedHeader;assert(qg:reconcile("release"))');pump(200)
        lua.execute('assert(sg:request())');pump(20)
        lua.execute('spotifyService.ownershipDriver={stop=function(_,operation,done) lateOperation=operation;lateProducer=done end};assert(mg:request())');pump(10050)
        lua.execute('assert(not mg:canStart());lateProducer({operation=lateOperation,producer_stopped=true,restart_fenced=true});assert(not mg:canStart());assert(not sg:canStart())')
        lua.execute(r'''
        local Hooks=require('applets.StandaloneBase.OwnershipHooks')
        local a,b=function() end,function() end
        local target={fn=a};local hooks=Hooks.new()
        assert(hooks:install(target,'fn',function(old) return function(...) return old(...) end end))
        local wrapper=target.fn;assert(hooks:install(target,'fn',function() error('stack') end))
        target.fn=b;assert(not hooks:restore());assert(target.fn==b)
        target.fn=wrapper;assert(hooks:restore());assert(hooks:restore());assert(target.fn==a)
        assert(require('applets.StandaloneBase.OwnershipClient').increment('9007199254740993')=='9007199254740994')
        local result
        require('applets.StandaloneBase.OwnershipDecoder').stop({qualified=false},function(ok) result=ok end)
        assert(result==false)
        for _,g in ipairs(gates) do g:shutdown() end
        ''');pump(100)
        print(json.dumps({'lua_native_six_transitions':transitions,'wire_messages':messages,'wall_seconds':time.monotonic()-start,
            'pending_subscriptions_max':4,'idle_lua_heap_with_apps_kib':idle_heap,'idle_simulated_ms_without_new_messages':1000,'lua_heap_kib':lua.eval('collectgarbage("count")'),
            'native_peak_rss_kib':int(next(x.split()[1] for x in Path(f'/proc/{driver.process.pid}/status').read_text().splitlines() if x.startswith('VmHWM:'))),
            'proof':'actual app/adapters and native protocol; instrumented producer/decoder; no hardware audio'}))
    finally:driver.close()
if __name__=='__main__':run(*sys.argv[1:6])
