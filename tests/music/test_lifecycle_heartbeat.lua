local root=arg[1]
local files, renamed={},0
local realio, realos=io,os
io={open=function(path,mode)
    if mode=="r" then
        if not files[path] then return nil end
        return {read=function() return files[path] end,close=function() return true end}
    end
    local contents=""
    return {write=function(self,...)
        for _,v in ipairs({...}) do contents=contents..v end
        files[path]=contents; return self
    end,close=function() return true end}
end}
os={remove=function(path) files[path]=nil end,
    rename=function(a,b) files[b]=files[a];files[a]=nil;renamed=renamed+1;return true end}
local ticks,callback=10,nil
local heartbeat=dofile(root.."/applet/LifecycleHeartbeat.lua").new(
    {getTicks=function() return ticks end},
    function(interval,fn)
        assert(interval==1000);callback=fn
        return {start=function() end,stop=function() callback=nil end}
    end)
local run="/var/run/standalonebase/"
assert(not heartbeat:pulse())
files[run.."restart.challenge"]="bad\n"
assert(not heartbeat:pulse())
local nonce=string.rep("a",64)
files[run.."restart.challenge"]=nonce.."\n"
files["/proc/self/stat"]="123 (jive worker) S "..string.rep("0 ",18).."456 0\n"
heartbeat:start();callback()
assert(files[run.."jive.health"]=="SBUI1 "..nonce.." 123 456 10 1\n")
ticks=20;callback()
assert(files[run.."jive.health"]:match("20 2\n$"))
files[run.."restart.challenge"]=string.rep("b",64).."\n";callback()
assert(files[run.."jive.health"]:match("20 1\n$"))
files["/proc/self/stat"]="malformed";assert(not heartbeat:pulse())
assert(renamed==3)
files[run.."heartbeat.stop"]="disabled\n"
assert(not heartbeat:pulse());assert(callback==nil)
io,os=realio,realos
print("Lifecycle UI witness: challenge, PID/start-time, ticks, sequence, atomic replacement and shutdown passed")
