-- Concrete profiles bind here; no installation/startup code constructs this.
local Client=require("applets.StandaloneBase.OwnershipClient")
local Seam=require("applets.StandaloneBase.MusicOwnership")
local Decoder=require("applets.StandaloneBase.OwnershipDecoder")
local M={}
function M.new(o,p)
    if not o or o.enabled~=true or type(p)~="table" then return nil end
    local binding={epoch=0,active=true,playback=nil}
    local function fence()
        binding.epoch=binding.epoch+1
        binding.playback=o.getPlayback()
        return p.fence()==true
    end
    local function quiet(done)
        local epoch=binding.epoch
        Decoder.stop({qualified=o.qualifiedFirmware==true,playback=binding.playback,decode=o.decode,
            clock=o.clock,after=o.after,valid=function() return binding.active and binding.epoch==epoch end},done)
    end
    local client
    client=Client.new({enabled=true,owner=p.owner,clock=o.clock,after=o.after,transport=o.transport,
        seam=function(incarnation,send)
            return Seam.new({enabled=true,owner=p.owner,incarnation=incarnation,clock=o.clock,send=send,
                fence=fence,stopProducer=p.stopProducer,releaseHooks=p.releaseHooks,decoderQuiescent=quiet})
        end,
        grant=function(v) if p.grant then p.grant(v) end end,
        revoke=function() fence() end,
        fault=function(reason) fence();if o.fault then o.fault(reason) end end,
        ready=function(c) if o.ready then o.ready(binding,c) end end})
    if not client then return nil end
    binding.client=client
    function binding:canStart() return self.active and client:canStart() end
    function binding:request(done) return client:request(done) end
    -- Initial/reconnect reconciliation is explicit, never HELO-derived idle.
    function binding:reconcile(mode,done)
        if self.reconciling or not self.active or not client.active or client.busy then return false end
        if mode~="idle" and mode~="release" and mode~="reconnect" then return false end
        local v=client.snapshot
        local owner=({radio=1,spotify=2,qobuz=3,music=4})[p.owner]
        if not v or (mode=="idle" and v.owner~=0) or (mode~="idle" and v.owner~=owner) then return false end
        self.reconciling=true
        local epoch=self.epoch+1; local completed=false;local timer
        local function finish(ok)
            if completed then return end
            completed=true;if timer and timer.stop then timer:stop() end;self.reconciling=false;if done then done(ok) end
        end
        if not fence() then finish(false);return false end
        local function valid() return not completed and self.active and self.epoch==epoch and client.active end
        local producerDone=false
        p.stopProducer(function(ok)
            if producerDone or not valid() then return end;producerDone=true
            if ok~=true then finish(false);return end
            local hooksDone=false
            p.releaseHooks(function(ok)
                if hooksDone or not valid() then return end;hooksDone=true
                if ok~=true then finish(false);return end
                quiet(function(ok)
                    if not valid() then return end
                    if ok~=true then finish(false);return end
                    if not client:command(mode,7,"0",0,function(v,err) finish(v~=nil and not err) end) then finish(false) end
                end)
            end)
        end)
        if not completed then timer=o.after(9000,function() if valid() then finish(false) end end) end
        return true
    end
    function binding:setStandby(suspended,done)
        return client:command("suspend",suspended and 1 or 0,"0",0,function(v,err)
            if not v or err then if done then done(false) end;return end
            local owner=({radio=1,spotify=2,qobuz=3,music=4})[p.owner]
            if suspended and v.owner==owner then self:reconcile("release",done)
            elseif done then done(true) end
        end)
    end
    function binding:shutdown() self.active=false;fence();client:shutdown() end
    if not p.attach(binding) then binding:shutdown();return nil end
    return binding
end
return M
