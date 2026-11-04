-- A challenge-bound UI-loop witness. This grants no playback ownership.
local io, os, tostring, tonumber, string = io, os, tostring, tonumber, string
local M = {}
local RUN = "/var/run/standalonebase/"

function M.new(framework, timer)
    local self = { sequence=0 }
    function self:pulse()
        local disabled=io.open(RUN.."heartbeat.stop","r")
        if disabled then disabled:close(); self:stop(); return false end
        local f=io.open(RUN.."restart.challenge", "r")
        if not f then return false end
        local nonce=f:read(66); f:close()
        if not nonce or #nonce~=65 or nonce:sub(65)~="\n" or
           nonce:sub(1,64):find("[^0-9a-f]") then return false end
        nonce=nonce:sub(1,64)
        f=io.open("/proc/self/stat", "r")
        if not f then return false end
        local stat=f:read(4096); f:close()
        if not stat then return false end
        local pid, fields=stat:match("^(%d+) %b() (.*)$")
        if not pid then return false end
        local index, start=0, nil
        for token in fields:gmatch("%S+") do
            index=index+1
            if index==20 then start=token; break end
        end
        if not start or start:find("[^0-9]") then return false end
        local ticks=tonumber(framework:getTicks())
        if not ticks then return false end
        if self.nonce~=nonce then self.nonce=nonce; self.sequence=0 end
        self.sequence=self.sequence+1
        f=io.open(RUN.."jive.health.tmp", "w")
        if not f then return false end
        local ok=f:write("SBUI1 ",nonce," ",pid," ",start," ",
                         tostring(ticks)," ",tostring(self.sequence),"\n")
        local closed=f:close()
        if not ok or not closed then os.remove(RUN.."jive.health.tmp"); return false end
        return os.rename(RUN.."jive.health.tmp",RUN.."jive.health") and true or false
    end
    function self:start()
        self.timer=timer(1000,function() self:pulse() end)
        self.timer:start()
    end
    function self:stop()
        if self.timer then self.timer:stop(); self.timer=nil end
    end
    return self
end
return M
