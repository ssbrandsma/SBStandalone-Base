local Binding=require("applets.StandaloneBase.OwnershipBinding")
local M={}
function M.new(o)
    if not o or type(o.releaseMusicHooks)~="function" then return nil end
    local gate
    return Binding.new(o,{owner="music",
        attach=function(binding) gate=binding;if o.attach then return o.attach(binding) end return false end,
        fence=function() return o.fenceMusic and o.fenceMusic()==true end,
        stopProducer=function(done)
            if o.stopResources then o.stopResources(done);return end
            if not gate.client:command("cleanup",0,"0",0,function()
                gate.client:awaitMusicReady(done)
            end) then done(false) end
        end,
        releaseHooks=o.releaseMusicHooks,grant=o.grantMusic})
end
return M
