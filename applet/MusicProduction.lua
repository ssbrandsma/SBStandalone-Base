-- Explicit production composition for one idle-to-Music MP3 qualification.
local Native=require("applets.StandaloneBase.NativeMusicOwnership")
local Decoder=require("applets.StandaloneBase.OwnershipDecoder")
local Runtime=require("applets.StandaloneBase.OwnershipRuntime")
local M={}
local keys={"_audg","_strm","_cont","_aude","_streamHttpHeaders","_streamDisconnect","stopInternal"}
function M.readiness(options,done)
    local o=Runtime.options(options)
    if not o then done(false);return end
    o.transport("SBOWN1 hello 4 0 1 0 0 0 0 0 0 0",function(v,err)
        done(not err and type(v)=="table" and v.version==1 and v.idle_only==true and v.incarnation=="0")
    end)
end
function M.new(options)
    if not options or options.enabled~=true or not options.fence or
       options.qualifiedFirmware~=true or type(options.stockPlayback)~="table" or
       type(options.stockSend)~="function" then return nil end
    local o=Runtime.options(options);if not o then return nil end
    local self={active=true,busy=false,admitted=false,seenBusy=false,deadline=o.clock()+10000}
    local playback,binding,probeTimer
    local function hooks()
        local p=o.getPlayback()
        if not p or (playback and p~=playback) or not p.slimproto or p.slimproto.send~=o.stockSend then return false end
        for _,key in ipairs(keys) do
            if type(o.stockPlayback[key])~="function" or p[key]~=o.stockPlayback[key] then return false end
        end
        return true
    end
    local function fault(reason)
        if not self.active then return end
        self.error=reason;self.active=false
        if probeTimer and probeTimer.stop then probeTimer:stop() end
        if binding and binding.client.active then binding:shutdown() end
        if options.fault then options.fault(reason) end
    end
    local function schedule()
        if probeTimer or not self.active then return end
        probeTimer=o.after(100,function()
            probeTimer=nil
            if not self.active or not binding or self.busy then return end
            if not self.admitted and o.clock()>=self.deadline then fault("initial_admission_deadline");return end
            local c=binding.client;local v=c.snapshot
            if not c.active then fault("ownership_bridge_lost");return end
            if not v or not v.connected or v.connection=="0" or c.busy then schedule();return end
            if not options.fence:quiet() or not hooks() then fault("application_or_hook_fence_lost");c:shutdown();return end
            -- Radio is an in-Jive producer, proved by the application fence.
            -- Native absence applies only to Spotify/Qobuz processes (12).
            if not v.idle_only or v.unavailable_mask~=14 or v.startup_native_absent_mask~=12 then
                fault("idle_production_policy_unavailable");c:shutdown();return
            end
            if v.owner==4 then
                if not v.music_ready then self.seenBusy=true end
                if self.admitted and v.music_ready then
                    self.busy=true
                    binding:reconcile("release",function(ok)
                        self.busy=false;self.released=ok
                        if not ok then fault("decoder_release_unproven") end
                    end)
                end
                return
            end
            if self.admitted or v.idle_admitted then self.admitted=true;return end
            if v.owner~=0 or v.requested~=0 or v.suspended or not v.music_ready then schedule();return end
            playback=o.getPlayback()
            if playback.stream or playback.source=="capture" then fault("initial_decoder_not_idle");return end
            self.busy=true;local generation,connection=v.generation,v.connection
            Decoder.stop({qualified=true,playback=playback,decode=o.decode,clock=o.clock,after=o.after,
                valid=function()
                    local now=c.snapshot
                    return self.active and c.active and options.fence:quiet() and hooks() and now and
                        now.owner==0 and now.generation==generation and now.connection==connection
                end},function(ok)
                if not self.active then return end
                if not ok then self.busy=false;fault("initial_decoder_quiescence_unproven");return end
                if not c:command("admit-idle",7,"0",0,function(reply,err)
                    self.busy=false
                    if not reply or err or not reply.idle_admitted then fault("initial_idle_admission_refused")
                    else self.admitted=true;if options.ready then options.ready(self) end end
                end) then self.busy=false;schedule() end
            end)
        end)
    end
    o.fenceMusic=function() return options.fence:quiet() and hooks() end
    o.releaseMusicHooks=function(done) done(options.fence:quiet() and hooks()) end
    o.attach=function(b) binding=b;self.binding=b;return true end
    o.ready=function() schedule() end
    o.observed=function() schedule() end
    o.fault=fault
    binding=Native.new(o)
    if not binding then fault("native_binding_unavailable");return nil end
    self.binding=binding;schedule()
    function self:shutdown()
        if not self.active then return end
        self.active=false
        if probeTimer and probeTimer.stop then probeTimer:stop() end
        binding:shutdown()
    end
    return self
end
return M
