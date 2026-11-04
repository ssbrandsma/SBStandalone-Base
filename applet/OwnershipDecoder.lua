-- Conservative firmware stop observation; not proof of analogue silence.
-- A qualified binding must provide the actual firmware decode object, playback
-- and existing one-shot scheduler. Never call stop() on a capture source.
local M = {}
function M.stop(options, done)
    if not options or not options.qualified or not options.playback or not options.decode
        or type(options.after)~="function" or type(options.clock)~="function" then done(false); return end
    local playback, decode = options.playback, options.decode
    if playback.source=="capture" then done(false); return end
    local ok=pcall(function() playback:stopInternal() end)
    if not ok then done(false); return end
    local deadline=options.clock()+math.min(options.timeout or 2000,5000)
    local good, finished=0,false
    local function sample()
        if finished then return end
        if options.valid and not options.valid() then finished=true; done(false); return end
        local ok,status=pcall(function() return decode:status() end)
        if not ok or type(status)~="table" then finished=true; done(false); return end
        -- Native stop handler clears BOTH states while holding audio lock.
        -- Require empty FIFO and closed producer on two separate Jive turns.
        if status.decodeState==0 and status.audioState==0 and status.outputFull==0
            and playback.stream==nil then good=good+1 else good=0 end
        if good>=2 then finished=true; done(true); return end
        if options.clock()>=deadline then finished=true; done(false); return end
        options.after(25,sample)
    end
    options.after(25,sample)
end
return M
