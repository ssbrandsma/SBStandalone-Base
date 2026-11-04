local root = assert(arg[1])
local Module = dofile(root .. "/applet/MusicOwnership.lua")
assert(Module.new({enabled=false}) == nil)
assert(Module.new({enabled=true, owner="radio", incarnation="1"}) == nil)
for _, owner in ipairs({"radio", "spotify", "qobuz", "music"}) do
    local clock, replies, callbacks = 100, {}, {}
    local adapter = assert(Module.new({enabled=true, owner=owner, incarnation="9007199254740993",
        clock=function() return clock end, fence=function() return true end,
        send=function(proof) replies[#replies+1] = proof end,
        stopProducer=function(done) callbacks.producer = done end,
        releaseHooks=function(done) callbacks.hooks = done end,
        decoderQuiescent=function(done) callbacks.decoder = done end}))
    assert(adapter:authorize("9007199254740994", "3"))
    assert(adapter:canStart("9007199254740994", "3"))
    assert(adapter:request({request_id="1", generation="9007199254740994", connection="3", deadline=200}))
    assert(not adapter:canStart("9007199254740994", "3"))
    assert(not adapter:request({request_id="2", generation="4", connection="3", deadline=200}))
    assert(callbacks.producer(true)); assert(#replies == 0)
    assert(not callbacks.producer(true)) -- repeated callback cannot run another stage
    assert(callbacks.hooks(true)); assert(#replies == 0)
    assert(callbacks.decoder(true)); assert(#replies == 1 and replies[1].accepted)
    assert(replies[1].incarnation == "9007199254740993")
    assert(adapter:authorize("9007199254740995", "4"))
    assert(not adapter:authorize("9007199254740994", "3"))
    assert(not adapter:request({request_id="1", generation="9007199254740994", connection="3", deadline=200}))
    assert(adapter:request({request_id="2", generation="9007199254740995", connection="4", deadline=200}))
    local stale = callbacks.producer
    assert(adapter:cancel("2")); assert(not stale(true)); assert(not replies[2].accepted)
    assert(adapter:authorize("9007199254740996", "4"))
    assert(adapter:request({request_id="3", generation="9007199254740996", connection="4", deadline=200}))
    clock = 201; adapter:tick(); assert(#replies == 3 and not replies[3].accepted)
    assert(not callbacks.producer(true))
    adapter:stop(); assert(not adapter:authorize("9007199254740996", "5"))
end
print("Opt-in Lua proof sequencing, all owners, 64-bit string identities, cancel and timeout passed")
