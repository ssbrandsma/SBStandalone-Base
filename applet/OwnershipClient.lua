-- Nonblocking protocol client. Explicit construction; no startup side effects.
local M = {}
local owners={radio=1,spotify=2,qobuz=3,music=4}
local function valid(s)
    return type(s)=="string" and s:match("^%d+$") and #s<=20 and
        (s=="0" or s:sub(1,1)~="0") and (#s<20 or s<="18446744073709551615")
end
local function increment(s)
    if not valid(s) or s=="18446744073709551615" then return nil end
    local out,carry={},1
    for i=#s,1,-1 do local n=tonumber(s:sub(i,i))+carry;out[i]=tostring(n%10);carry=math.floor(n/10) end
    return (carry==1 and "1" or "")..table.concat(out)
end
M.increment=increment
local function cancel(timer)
    if timer and type(timer.stop)=="function" then timer:stop() end
end
function M.new(o)
    if not o or o.enabled~=true or not owners[o.owner] or type(o.transport)~="function"
        or type(o.clock)~="function" or type(o.after)~="function" or type(o.seam)~="function" then return nil end
    local self={active=true, sequence="0", snapshot=nil, busy=false, polling=false, serial=0, granted=false}
    local function fail(reason)
        if not self.active then return end
        -- Best-effort fault is a quarantine notification, never release proof.
        local v=self.snapshot
        if v and v.incarnation~="0" then
            local seq=increment(self.sequence)
            if seq then
                local message=table.concat({"SBOWN1","fault",tostring(owners[o.owner]),v.incarnation,seq,
                    v.generation,v.connection,"0","0","0","0","0"}," ")
                pcall(o.transport,message,function() end)
            end
        end
        self.active=false;self.granted=false;self.serial=self.serial+1
        cancel(self.commandTimer);cancel(self.pollTimer);cancel(self.yieldTimer);cancel(self.registrationTimer)
        if self.awaiting then cancel(self.awaiting.timer);self.awaiting=nil end
        if self.adapter then self.adapter:invalidate() end
        if o.fault then o.fault(reason) end
    end
    local function observe(v)
        if type(v)~="table" or v.version~=1 then return false end
        for _,k in ipairs({"incarnation","revision","sequence","generation","connection","request"}) do if not valid(v[k]) then return false end end
        local old=self.snapshot
        if old and (#v.revision<#old.revision or (#v.revision==#old.revision and v.revision<old.revision)) then return true end
        self.snapshot=v
        if self.awaiting then
            local waiting=self.awaiting
            if v.generation~=waiting.generation or v.connection~=waiting.connection then
                cancel(waiting.timer);self.awaiting=nil;waiting.done(false)
            elseif v.music_ready==true then cancel(waiting.timer);self.awaiting=nil;waiting.done(true) end
        end
        if self.adapter then
            local pending=self.adapter.pending
            if pending and (pending.generation~=v.generation or pending.connection~=v.connection
                or pending.request_id~=v.request or v.requested==0) then
                self.adapter:invalidate()
                if o.revoke then o.revoke(v) end
            end
            if v.owner==owners[o.owner] and v.controls==true then
                if not self.granted then
                    if not self.adapter:authorize(v.generation,v.connection) then return false end
                    self.granted=true
                    if o.grant then o.grant(v) end
                end
            elseif v.owner==owners[o.owner] and v.requested~=0 and v.remaining_ms>0 then
                if self.granted then
                    self.granted=false
                    local req={request_id=v.request,generation=v.generation,connection=v.connection,
                               deadline=o.clock()+math.min(v.remaining_ms,10000)}
                    if not self.adapter:request(req) then return false end
                    local serial=self.serial
                    if self.adapter.pending then
                        self.yieldTimer=o.after(math.min(v.remaining_ms,10000),function()
                            if self.active and self.serial==serial then self.adapter:tick() end
                        end)
                    end
                end
            elseif self.granted then
                self.granted=false;self.adapter:invalidate()
                if o.revoke then o.revoke(v) end
            end
        end
        return true
    end
    local function wire(op, seq, flags, req, timeout, revision)
        local v=self.snapshot or {}
        return table.concat({"SBOWN1",op,tostring(owners[o.owner]),v.incarnation or "0",seq,
            v.generation or "0",v.connection or "0",req or "0",tostring(timeout or 0),
            tostring(flags or 0),revision or "0","0"}," ")
    end
    function self:command(op, flags, request, timeout, callback)
        if not self.active or self.busy or not self.snapshot then return false end
        self.sequence=increment(self.sequence)
        if not self.sequence then fail("sequence_exhausted");return false end
        local body=wire(op,self.sequence,flags,request,timeout)
        self.busy=true;local used=false;local serial=self.serial
        o.transport(body,function(v,err)
            if used or not self.active or self.serial~=serial then return end
            used=true;cancel(self.commandTimer);self.commandTimer=nil;self.busy=false
            if err=="bridge_rejected" or err=="rejected" then
                if callback then callback(nil,err) end
                return -- refusal is not a bridge crash and grants nothing
            end
            if err or not observe(v) then fail(err or "invalid_reply");return end
            if callback then callback(v) end
        end)
        if not used then self.commandTimer=o.after(11000,function() if self.active and self.serial==serial and not used then fail("command_timeout") end end) end
        return true
    end
    function self:poll()
        if not self.active or self.polling or not self.snapshot then return end
        self.polling=true;local used=false;local serial=self.serial
        local body=wire("query",increment(self.sequence) or self.sequence,0,"0",0,self.snapshot.revision)
        o.transport(body,function(v,err)
            if used or not self.active or self.serial~=serial then return end
            used=true;cancel(self.pollTimer);self.pollTimer=nil;self.polling=false
            if err or not observe(v) then fail(err or "invalid_query");return end
            o.after(0,function() self:poll() end)
        end)
        if not used then self.pollTimer=o.after(25000,function() if self.active and self.serial==serial and not used then fail("bridge_timeout") end end) end
    end
    function self:awaitMusicReady(done)
        local v=self.snapshot
        if not self.active or not v or self.awaiting then done(false);return end
        if v.music_ready==true then done(true);return end
        local waiting={generation=v.generation,connection=v.connection,done=done}
        self.awaiting=waiting
        waiting.timer=o.after(5000,function() if self.awaiting==waiting then self.awaiting=nil;done(false) end end)
    end
    function self:request(callback) return self:command("request",0,"0",10000,callback) end
    function self:release(callback) return self:command("release",7,"0",0,callback) end
    function self:canStart()
        local v=self.snapshot
        return self.active and self.granted and v and not v.suspended
            and self.adapter:canStart(v.generation,v.connection)
    end
    function self:shutdown() fail("shutdown") end
    -- Read-only hello recovers the server's sequence without guessing uint64s.
    o.transport("SBOWN1 hello "..owners[o.owner].." 0 1 0 0 0 0 0 0 0",function(v,err)
        if not self.active then return end
        if err or not observe(v) or v.incarnation~="0" then fail("registration_unavailable");return end
        self.sequence=increment(v.sequence)
        if not self.sequence then fail("sequence_exhausted");return end
        -- Registration explicitly has no owner/connection tuple yet.
        local body="SBOWN1 register "..owners[o.owner].." 0 "..self.sequence.." 0 0 0 0 0 0 0"
        o.transport(body,function(reply,err)
            if not self.active then return end
            if err or not observe(reply) then fail("register_failed");return end
            cancel(self.registrationTimer);self.registrationTimer=nil
            self.adapter=o.seam(reply.incarnation,function(proof)
                cancel(self.yieldTimer);self.yieldTimer=nil
                if not self:command("ack",proof.accepted and 7 or 0,proof.request_id,0) then fail("proof_busy") end
            end)
            if not self.adapter then fail("adapter_unavailable");return end
            if o.ready then o.ready(self) end
            self:poll()
        end)
    end)
    if not self.adapter then self.registrationTimer=o.after(11000,function() if self.active and not self.adapter then fail("registration_timeout") end end) end
    return self
end
return M
