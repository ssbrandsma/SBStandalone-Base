-- Explicit construction only. Uses Jive's existing async transport and timers.
local M={}
local jnt=jnt
function M.options(o)
    if not o or o.enabled~=true then return nil end
    local copy={};for k,v in pairs(o) do copy[k]=v end
    local Framework=require("jive.ui.Framework")
    local Timer=require("jive.ui.Timer")
    local Player=require("jive.slim.Player")
    copy.jnt=copy.jnt or jnt
    copy.decode=copy.decode or require("squeezeplay.decode")
    local last,wrap=0,0
    copy.clock=copy.clock or function()
        local n=Framework:getTicks()
        if n<last then wrap=wrap+4294967296 end
        last=n;return wrap+n
    end
    copy.after=copy.after or function(ms,fn)
        local timer
        timer=Timer(math.max(1,ms),function() timer=nil;fn() end,true)
        timer:start()
        return timer
    end
    copy.getPlayback=copy.getPlayback or function()
        local player=Player:getLocalPlayer()
        return player and player.playback
    end
    copy.transport=copy.transport or require("applets.StandaloneBase.OwnershipHttp").new(copy)
    if not copy.transport then return nil end
    return copy
end
return M
