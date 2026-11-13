-- PlaybackFence regression tests (WP4a.6): exclusive-decoder fencing.
-- Reproduces the WP4a.5 10:48:52 Radio preemption (track 46 cut after ~33 s)
-- against faithful fakes of the live Radio/Spotify classes, then proves the
-- fence blocks every automatic path while preserving explicit Radio use.
-- Also drives the SBOWN1 adapter lifecycle (register/admit/release/
-- reconnect) against a scripted bridge. No Jive, network, hardware or
-- real timers.
package.path = arg[1] .. "/applet/?.lua;" .. package.path
for _, name in ipairs({"OwnershipHooks", "OwnershipRuntime",
    "OwnershipClient", "OwnershipHttp"}) do
    package.loaded["applets.StandaloneBase." .. name] = require(name)
end

-- ===== minimal JSON codec (flat objects only) =====
local json = {}
function json.encode(t)
    local parts = {}
    for k, v in pairs(t) do
        local key = '"' .. tostring(k) .. '"'
        local value
        if type(v) == "string" then
            value = '"' .. v:gsub("\\", "\\\\"):gsub('"', '\\"') .. '"'
        elseif type(v) == "boolean" then
            value = tostring(v)
        else
            value = tostring(v)
        end
        parts[#parts + 1] = key .. ":" .. value
    end
    return "{" .. table.concat(parts, ",") .. "}"
end
function json.decode(s)
    assert(type(s) == "string" and s:sub(1, 1) == "{", "bad json")
    local t = {}
    local pos = 1
    while true do
        local _, ke, k = s:find('"([^"]+)":', pos)
        if not ke then break end
        pos = ke + 1
        local c = s:sub(pos, pos)
        if c == '"' then
            local _, ve, v = s:find('"([^"]*)"', pos)
            t[k] = v:gsub('\\"', '"'):gsub("\\\\", "\\")
            pos = ve + 1
        elseif s:sub(pos, pos + 3) == "true" then
            t[k] = true
            pos = pos + 4
        elseif s:sub(pos, pos + 4) == "false" then
            t[k] = false
            pos = pos + 5
        else
            local _, ve, v = s:find("([%-%.%d]+)", pos)
            t[k] = tonumber(v)
            pos = ve + 1
        end
    end
    return t
end
package.preload["json"] = function() return json end

-- ===== fake clock and timers (manual) =====
local NOW = 1000000
local timers = {}
package.preload["jive.ui.Timer"] = function()
    return function(ms, fn)
        local t = { ms = ms, fn = fn, running = false }
        function t:start() self.running = true; timers[#timers + 1] = self end
        function t:stop() self.running = false end
        function t:isRunning() return self.running end
        return t
    end
end
local function fireTimers(ms)
    NOW = NOW + (ms or 0)
    local due = timers
    timers = {}
    for _, t in ipairs(due) do
        if t.running then t.running = false; t.fn() end
    end
end
local actionWatchers = {}
local actionPrios = {}
package.preload["jive.ui.Framework"] = function()
    return {
        getTicks = function() return NOW % 4294967296 end,
        addActionListener = function(_, action, _, fn, prio)
            actionWatchers[action] = fn
            actionPrios[action] = prio
        end,
    }
end
package.preload["jive.utils.log"] = function()
    local sink = { infos = {}, warns = {}, errors = {} }
    _G.__fenceLog = sink
    local function capture(into)
        return function(_, ...)
            local parts = {}
            for _, v in ipairs({ ... }) do parts[#parts + 1] = tostring(v) end
            into[#into + 1] = table.concat(parts)
        end
    end
    return { logger = function()
        return { info = capture(sink.infos), warn = capture(sink.warns),
            error = capture(sink.errors) }
    end }
end

-- ===== fake decoder with counters =====
local decodeCalls = { starts = 0, stops = 0, resumes = 0, pauses = 0 }
local decodeState = { decodeState = 0, audioState = 0, outputFull = 0 }
package.preload["squeezeplay.decode"] = function()
    return {
        status = function() return decodeState end,
        start = function() decodeCalls.starts = decodeCalls.starts + 1 end,
        stop = function() decodeCalls.stops = decodeCalls.stops + 1 end,
        resumeAudio = function()
            decodeCalls.resumes = decodeCalls.resumes + 1
        end,
        pauseAudio = function() decodeCalls.pauses = decodeCalls.pauses + 1 end,
    }
end

-- ===== faithful fake of the live Radio StreamPlayer (WITH the bug) =====
local radioEvents
local activePlayer -- assigned below; stop() needs the shared playback
local StreamPlayer = {}
StreamPlayer.__index = StreamPlayer
function StreamPlayer.newInstance()
    return setmetatable({
        ownsPlayback = false, playbackActive = false,
        desiredStation = nil, pendingStation = nil, reconnectTimer = nil,
        generation = 0, stationGeneration = 0, retryIndex = 1,
        intentionalStop = false, redirectCount = 0, state = "STOPPED",
        currentMetadata = nil, paused = false,
        log = { info = function() end, warn = function() end },
        callbacks = {
            onConnected = function(st, g)
                radioEvents.connected[#radioEvents.connected + 1] = st
            end,
            onState = function(st, state)
                radioEvents.states[#radioEvents.states + 1] = state
            end,
            onMetadata = function(title)
                radioEvents.metadata[#radioEvents.metadata + 1] = title
            end,
        },
    }, StreamPlayer)
end
function StreamPlayer:_isCurrent(generation, station)
    return self.generation == generation and self.desiredStation == station
end
function StreamPlayer:_notifyState(station, state, show)
    self.state = state
    radioEvents.notifies[#radioEvents.notifies + 1] = state
    self.callbacks.onState(station, state, show, self.stationGeneration)
end
function StreamPlayer:_cancelReconnect()
    if self.reconnectTimer then
        self.reconnectTimer:stop()
        self.reconnectTimer = nil
    end
end
function StreamPlayer:_resetWatchdog() end
function StreamPlayer:_resetAudioRecovery() end
local reconnectDelays = { 2000, 5000, 10000, 30000 }
function StreamPlayer:_scheduleReconnect(station, generation)
    if not self:_isCurrent(generation, station) or self.reconnectTimer then
        return
    end
    local delay = reconnectDelays[self.retryIndex]
        or reconnectDelays[#reconnectDelays]
    if self.retryIndex < #reconnectDelays then
        self.retryIndex = self.retryIndex + 1
    end
    self:_notifyState(station, "RECONNECTING", false)
    radioEvents.reconnects[#radioEvents.reconnects + 1] = station.id
    local Timer = require("jive.ui.Timer")
    self.reconnectTimer = Timer(delay, function()
        self.reconnectTimer = nil
        if self:_isCurrent(generation, station) then
            self:_begin(station, true)
        end
    end, true)
    self.reconnectTimer:start()
end
function StreamPlayer:_handleDisconnect(reason, flush)
    -- Live 10:48 behavior: no ownership check (the WP4a.5 bug).
    if self.intentionalStop or flush or not reason
        or not self.desiredStation then
        return
    end
    local station = self.desiredStation
    local generation = self.generation
    self.playbackActive = false
    self.currentMetadata = nil
    self:_resetWatchdog()
    self:_resetAudioRecovery()
    self:_notifyState(station, "CONNECTION_LOST", false)
    self:_scheduleReconnect(station, generation)
end
function StreamPlayer:_begin(station, reconnect)
    self:_cancelReconnect()
    self.ownsPlayback = true
    self.generation = self.generation + 1
    self.desiredStation = station
    self.pendingStation = station
    self.currentMetadata = nil
    self.playbackActive = false
    if not reconnect then
        self.stationGeneration = self.stationGeneration + 1
        self.retryIndex = 1
    end
    radioEvents.begins[#radioEvents.begins + 1] = station.id
    -- Network stages are driven manually by the test.
    self.pendingStation = nil
    return true
end
function StreamPlayer:start(station) return self:_begin(station, false) end
function StreamPlayer:stop(state)
    self.generation = self.generation + 1
    self.ownsPlayback = false
    self:_cancelReconnect()
    self.pendingStation = nil
    self.currentMetadata = nil
    self.playbackActive = false
    self:_resetWatchdog()
    self:_resetAudioRecovery()
    -- Live _stopPlayback: unguarded decoder touch on the shared
    -- Playback (the WP4a.6 pause-kills-music vector).
    activePlayer.playback:stopInternal()
    self:_notifyState(self.desiredStation or { id = "none" },
        state or "STOPPED", false)
end
function StreamPlayer:stopConnecting() return false end
function StreamPlayer:pause()
    -- Live: idle (no episode) converts pause into stop().
    if not self.desiredStation or not self.desiredStation.episode then
        return self:stop()
    end
    if not self.playbackActive or self.paused then return end
    self.paused = true
    require("squeezeplay.decode").pauseAudio()
    self:_notifyState(self.desiredStation, "PAUSED", false)
end
function StreamPlayer:play()
    if self.desiredStation then return self:start(self.desiredStation) end
    return false
end
function StreamPlayer:getCurrentStation() return self.desiredStation end
function StreamPlayer:isPlaying() return self.playbackActive end
function StreamPlayer:_audioRecoveryTick()
    if not self.playbackActive or self.intentionalStop or self.paused then
        self:_resetAudioRecovery()
        return
    end
    -- Live behavior resumes audio on underrun without ownership check.
    require("squeezeplay.decode").resumeAudio()
    radioEvents.recovered[#radioEvents.recovered + 1] = true
end
function StreamPlayer:_handleMetadata(data)
    if not self.desiredStation or not self.playbackActive
        or type(data) ~= "string" then
        return
    end
    self.callbacks.onMetadata(data)
end
-- Live module uses module(): methods hide on a local class table, only
-- new() is exported. The fence must resolve the class through a live
-- instance metatable (regression: v1 refused with unsupported revision).
package.preload["applets.StandaloneRadio.StreamPlayer"] = function()
    return { new = function() return StreamPlayer.newInstance() end }
end
local function freshRadioEvents()
    radioEvents = { connected = {}, states = {}, metadata = {},
        notifies = {}, reconnects = {}, begins = {}, recovered = {} }
end
freshRadioEvents()

-- ===== fake Playback object + live-faithful inner Radio hooks =====
local stopInternals = 0
local function newPlayback()
    return {
        stream = nil, source = "http", header = nil,
        _streamConnect = function(self, host, port)
            self.connectedTo = { host, port }
        end,
        _streamDisconnect = function(self, reason, flush)
            self.lastDisconnect = { reason, flush }
        end,
        _streamHttpHeaders = function(self, headers)
            self.lastHeaders = headers
        end,
        stopInternal = function(self)
            stopInternals = stopInternals + 1
        end,
        pause = function(self) self.paused = true end,
    }
end
-- Mirrors live _installHooks: claims PLAYING for ANY 2xx when a stale
-- desiredStation exists, and routes every disconnect to _handleDisconnect.
local function installRadioInnerHooks(playback, instance)
    local originalHeaders = playback._streamHttpHeaders
    playback._streamHttpHeaders = function(self, headers)
        originalHeaders(self, headers)
        local headerText = tostring(headers)
        local statusLine = string.match(headerText, "^([^\r\n]+)")
        local status = tonumber(string.match(statusLine or "",
            "^HTTP/%d%.%d%s+(%d%d%d)"))
        if status and status >= 200 and status < 300 then
            instance.redirectCount = 0
        end
        if instance.desiredStation and not instance.intentionalStop then
            instance.playbackActive = true
            instance.retryIndex = 1
            instance:_notifyState(instance.desiredStation, "PLAYING", false)
            instance.callbacks.onConnected(instance.desiredStation,
                instance.stationGeneration)
        end
    end
    local originalDisconnect = playback._streamDisconnect
    playback._streamDisconnect = function(self, reason, flush)
        originalDisconnect(self, reason, flush)
        instance:_handleDisconnect(reason, flush)
    end
end

-- ===== faithful fake of live SpotifyPlayback (direct decoder use) =====
local SpotifyPlayback = {}
SpotifyPlayback.__index = SpotifyPlayback
function SpotifyPlayback.newInstance()
    return setmetatable({ playback = nil, paused = false,
        applet = nil }, SpotifyPlayback)
end
function SpotifyPlayback:start(host, port, path)
    local player = require("jive.slim.Player"):getLocalPlayer()
    if not player then return false end
    self.playback = player.playback
    if not self.playback then return false end
    self.paused = false
    self.playback:stopInternal()
    self.playback.header = "GET " .. path .. " HTTP/1.0"
    require("squeezeplay.decode").start()
    self.playback:_streamConnect(host, port)
    return true
end
function SpotifyPlayback:stop()
    if self.playback then
        self.playback:stopInternal()
        self.playback = nil
    end
    self.paused = false
end
function SpotifyPlayback:pause()
    if not self.playback then return false end
    self.playback:pause()
    self.paused = true
    return true
end
function SpotifyPlayback:resume()
    if not self.playback or not self.paused then return false end
    if not self.playback.stream then self.paused = false return false end
    require("squeezeplay.decode").resumeAudio()
    self.paused = false
    return true
end
package.preload["applets.SpotifyConnect.SpotifyPlayback"] = function()
    return SpotifyPlayback
end

-- ===== fake Player / appletManager / jive globals =====
activePlayer = { playback = newPlayback() }
local playerPauseCalls, playerUnpauseCalls = 0, 0
package.preload["jive.slim.Player"] = function()
    return {
        getLocalPlayer = function() return activePlayer end,
        getCurrentPlayer = function() return activePlayer end,
        pause = function()
            playerPauseCalls = playerPauseCalls + 1
        end,
        unpause = function()
            playerUnpauseCalls = playerUnpauseCalls + 1
        end,
    }
end
local radioApplet, spotifyApplet
appletManager = {
    getAppletInstance = function(_, name)
        if name == "StandaloneRadio" then return radioApplet end
        if name == "SpotifyConnect" then return spotifyApplet end
        return nil
    end,
}
jnt = {}

-- ===== scripted SBOWN1 bridge over stubbed HTTP =====
local bridge = {
    incarnation = 0, idle_admitted = false, owner = 0, requested = 0,
    generation = "1", connection = "1", revision = "10", sequence = "5",
    request = "0", yield = 0, connected = true, suspended = false,
    music_ready = true, seen = {},
}
local function bridgeSnapshot(inc)
    return {
        version = 1, incarnation = tostring(inc), revision = bridge.revision,
        sequence = bridge.sequence, generation = bridge.generation,
        connection = bridge.connection, request = bridge.request,
        owner = bridge.owner, requested = bridge.requested,
        controls = bridge.owner == 4 and not bridge.controlsOff,
        suspended = bridge.suspended,
        music_ready = bridge.music_ready, remaining_ms = 0,
        yield = bridge.yield, connected = bridge.connected,
        idle_only = true, idle_admitted = bridge.idle_admitted,
        unavailable_mask = 14, startup_native_absent_mask = 12,
    }
end
local function bridgeCommand(body)
    local parts = {}
    for part in body:gmatch("%S+") do parts[#parts + 1] = part end
    assert(parts[1] == "SBOWN1" and #parts == 12, "bad wire: " .. body)
    local op, who, inc = parts[2], tonumber(parts[3]), parts[4]
    bridge.seen[#bridge.seen + 1] = op .. ":" .. parts[10]
    if op == "hello" then return bridgeSnapshot(0) end
    if op == "register" then
        bridge.incarnation = bridge.incarnation + 1
        return bridgeSnapshot(bridge.incarnation)
    end
    assert(tonumber(inc) == bridge.incarnation, "stale incarnation")
    if op == "query" then return bridgeSnapshot(bridge.incarnation) end
    if op == "admit-idle" then
        assert(parts[10] == "7", "admit must attest 7")
        if bridge.owner == 0 then bridge.idle_admitted = true end
        return bridgeSnapshot(bridge.incarnation)
    end
    if op == "release" then
        if bridge.owner == 4 then bridge.owner = 0 end
        return bridgeSnapshot(bridge.incarnation)
    end
    if op == "reconnect" then
        if bridge.owner == 4 then bridge.owner = 0 end
        bridge.yield = 0
        return bridgeSnapshot(bridge.incarnation)
    end
    if op == "cleanup" then
        bridge.music_ready = true
        return bridgeSnapshot(bridge.incarnation)
    end
    if op == "fault" then
        -- Native incarnations are monotonic: fault clears the
        -- registration, never the counter.
        bridge.idle_admitted = false
        return bridgeSnapshot(0)
    end
    error("unexpected op " .. op)
end
package.preload["jive.net.RequestHttp"] = function()
    return function(callback, method, url, opts)
        return { callback = callback, opts = opts }
    end
end
package.preload["jive.net.SocketHttp"] = function()
    return function(_, host, port)
        assert(host == "127.0.0.1" and port == 9000, "ownership route")
        return { fetch = function(_, request)
            -- Faithful to live SocketHttp + native bridge: the socket
            -- layer always adds Content-Length, and the bridge rejects a
            -- duplicated header (WP4a.6 V2 field failure).
            local given = (request.opts.headers or {})["Content-Length"]
            if given then
                request.callback(json.encode({ error = "ownership_request_rejected",
                    status = 400 }), nil)
                return
            end
            assert((request.opts.headers or {})["X-Music-Ownership"],
                "missing ownership token header")
            local body = request.opts.t_bodySource()
            assert(body, "empty ownership body")
            request.callback(json.encode(bridgeCommand(body)), nil)
        end }
    end
end

local Fence = require("PlaybackFence")
local STALE = { id = "radiobrowser:stale-station", codec = "mp3" }

-- ===== Phase A: unfenced baseline reproduces the WP4a.5 bug =====
do
    local R0 = StreamPlayer.newInstance()
    R0.desiredStation = STALE -- stale morning selection, radio stopped
    R0.ownsPlayback = false
    installRadioInnerHooks(activePlayer.playback, R0)
    -- Music's MP3 gets its HTTP 200 (sbwebserver speaks HTTP/1.1).
    activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
    activePlayer.playback:_streamHttpHeaders(
        "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n")
    assert(R0.playbackActive == true, "A: false PLAYING claim missing")
    assert(#radioEvents.connected == 1, "A: onConnected missing")
    -- Music stops; the shared decoder disconnects.
    activePlayer.playback.stream = nil
    activePlayer.playback:_streamDisconnect(0, nil)
    assert(R0.reconnectTimer ~= nil, "A: expected auto-reconnect (the bug)")
    fireTimers(2000)
    assert(R0.ownsPlayback == true, "A: expected radio takeover (the bug)")
    assert(#radioEvents.begins == 1, "A: expected radio _begin")
end

-- ===== Phase B: fenced behavior (late-bound instances) =====
freshRadioEvents()
local marker = os.tmpname()
do local f = io.open(marker, "w") f:write("1") f:close() end
-- Install while both applications are absent: must succeed deferred.
local F = Fence.install({ marker = marker })
assert(F, "B: install refused")
assert(Fence.install({ marker = marker }) == F, "B: install not idempotent")
assert(F.radioWrapped ~= true, "B: radio wrapped without instance")
-- Spotify uses oo.class: the module table carries the methods, so it
-- wraps at install (module path); Radio uses module(): deferred until
-- an instance metatable appears (metatable path below).
assert(F.spotifyWrapped == true, "B: spotify module path not wrapped")
-- Applications appear late; the tick completes the wraps.
local R1 = StreamPlayer.newInstance()
R1.desiredStation = STALE
R1.ownsPlayback = false
radioApplet = { streamPlayer = R1 }
local S1 = SpotifyPlayback.newInstance()
spotifyApplet = { playback = S1 }
activePlayer = { playback = newPlayback() }
installRadioInnerHooks(activePlayer.playback, R1)
fireTimers(5000)
assert(F.radioWrapped == true, "B: radio wraps not completed late")

-- B1: connect-time Music detection, no premature Radio claim.
activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
activePlayer.playback:_streamConnect("127.0.0.1", 8766)
assert(F.isActive(), "B1: music not detected at connect")
assert(#radioEvents.connected == 0, "B1: premature radio claim")
-- B2: Music's HTTP 200 must not flip Radio to PLAYING.
activePlayer.playback.stream = { id = 1 } -- decoder has the stream
activePlayer.playback:_streamHttpHeaders(
    "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n")
assert(R1.playbackActive == false, "B2: false PLAYING claim survived")
assert(#radioEvents.connected == 0, "B2: onConnected not suppressed")
for _, state in ipairs(radioEvents.notifies) do
    assert(state ~= "PLAYING", "B2: PLAYING notification leaked")
end
assert(R1.desiredStation == STALE, "B2: station preference must survive")
-- B3: Music stop must not schedule a Radio reconnect.
activePlayer.playback.stream = nil
activePlayer.playback:_streamDisconnect(0, nil)
assert(R1.reconnectTimer == nil, "B3: auto-resume not fenced")
assert(#radioEvents.reconnects == 0, "B3: reconnect notification leaked")
fireTimers(500)
assert(not F.isActive(), "B3: music flag not cleared on disconnect")
-- B4: every Radio start path fenced while Music owns the decoder.
F.setActive(true, "test")
assert(R1:_begin(STALE, false) == false, "B4: _begin not fenced")
assert(R1.ownsPlayback == false, "B4: ownership leaked via _begin")
R1:_scheduleReconnect(STALE, R1.generation)
assert(R1.reconnectTimer == nil, "B4: watchdog reconnect not fenced")
R1.playbackActive = true
R1:_audioRecoveryTick()
assert(#radioEvents.recovered == 0, "B4: recovery tick not fenced")
R1:_handleMetadata("StreamTitle='x';")
assert(#radioEvents.metadata == 0, "B4: metadata not fenced")
R1.playbackActive = false
F.setActive(false, "test")
-- B5: explicit Radio use still works end to end when idle.
assert(R1:start(STALE) == true, "B5: radio start broken")
assert(R1.ownsPlayback == true, "B5: radio ownership broken")
activePlayer.playback:_streamHttpHeaders(
    "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n")
assert(#radioEvents.connected == 1, "B5: radio onConnected broken")
R1:stop()
assert(R1.ownsPlayback == false, "B5: radio stop broken")
local sawStopped = false
for _, state in ipairs(radioEvents.notifies) do
    if state == "STOPPED" then sawStopped = true end
end
assert(sawStopped, "B5: STOPPED notification broken")

-- ===== Phase C: Spotify fencing =====
F.setActive(true, "test")
local startsBefore = decodeCalls.starts
local stopsBefore = stopInternals
assert(S1:start("127.0.0.1", 17880, "/spotify.ogg") == false,
    "C: spotify start not fenced")
assert(decodeCalls.starts == startsBefore, "C: decoder touched by spotify")
S1.playback = activePlayer.playback -- stale claim from before Music
S1:stop()
assert(S1.playback == nil, "C: stale spotify claim not released")
assert(stopInternals == stopsBefore, "C: spotify stop killed the decoder")
S1.playback = activePlayer.playback
assert(S1:pause() == false, "C: spotify pause not fenced")
S1.paused = true
activePlayer.playback.stream = { id = 2 }
assert(S1:resume() == false, "C: spotify resume not fenced")
F.setActive(false, "test")
activePlayer.playback.stream = nil
S1.playback = nil
S1.paused = false
assert(S1:start("127.0.0.1", 17880, "/spotify.ogg") == true,
    "C: spotify start broken when idle")
assert(decodeCalls.starts == startsBefore + 1, "C: spotify start lost audio")
S1:stop()

-- ===== Phase D: marker removal passivates the fence on refresh =====
os.remove(marker)
F.refreshMarker()
F.setActive(true, "test")
R1:_scheduleReconnect(STALE, R1.generation)
assert(R1.reconnectTimer ~= nil, "D: passthrough missing without marker")
R1:_cancelReconnect()
assert(R1:_begin(STALE, false) == true, "D: begin passthrough missing")
R1:stop()
F.setActive(false, "test")
do local f = io.open(marker, "w") f:write("1") f:close() end
F.refreshMarker()

-- ===== Phase E: SBOWN1 adapter lifecycle =====
local tokenPath = os.tmpname()
do local f = io.open(tokenPath, "w") f:write(string.rep("ab", 32)) f:close() end
F.findToken = function() return tokenPath end
assert(R1.ownsPlayback == false and R1.playbackActive == false
    and R1.pendingStation == nil and R1.reconnectTimer == nil,
    "E: radio not quiet")
assert(S1.playback == nil, "E: spotify not quiet")
assert(F.allQuiet(), "E: allQuiet must hold")
fireTimers(5000) -- fence tick joins: hello + register + query + admit-idle
assert(F.client ~= nil, "E: adapter did not join")
assert(F.client.snapshot.idle_admitted == true, "E: not admitted")
local sawAdmit = false
for _, op in ipairs(bridge.seen) do
    if op == "admit-idle:7" then sawAdmit = true end
end
assert(sawAdmit, "E: admit-idle without flags 7")
bridge.owner = 4 -- native grant on Music load
fireTimers(5000)
activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
activePlayer.playback:_streamConnect("127.0.0.1", 8766)
activePlayer.playback.stream = { id = 3 }
assert(F.isActive(), "E: music not tracked")
activePlayer.playback.stream = nil
activePlayer.playback:_streamDisconnect(0, nil)
fireTimers(500) -- disconnect confirm clears the flag
assert(not F.isActive(), "E: music flag stuck")
fireTimers(4000) -- settle past the 3 s release delay
assert(bridge.owner == 0, "E: ownership not released after stop")
-- SlimProto flap while holding: the bridge drops admission, the fence
-- reconciles via reconnect, then re-admits.
bridge.owner = 4
bridge.yield = 5
bridge.idle_admitted = false
fireTimers(5000) -- client query refreshes the snapshot
fireTimers(5000) -- fence issues reconnect
assert(bridge.owner == 0 and bridge.yield == 0, "E: reconnect not reconciled")
fireTimers(5000) -- fence re-admits
assert(bridge.idle_admitted == true, "E: no re-admit after flap")
-- Explicit Radio start holds the adapter; stop rejoins with incarnation+1.
local incBefore = bridge.incarnation
assert(R1:start(STALE) == true, "E: radio start broken")
assert(F.client == nil, "E: adapter not held for radio")
R1:stop()
fireTimers(5000)
assert(F.client ~= nil, "E: adapter did not rejoin")
assert(bridge.incarnation == incBefore + 1, "E: incarnation not renewed")

-- ===== Phase F: backstop reaps a flag when the decoder moves on =====
F.setActive(true, "test")
activePlayer.playback.stream = { id = 9 }
activePlayer.playback.header = "GET /alarm.mp3 HTTP/1.0"
fireTimers(5000)
assert(F.isActive(), "F: backstop must require two samples")
fireTimers(5000)
assert(not F.isActive(), "F: backstop did not reap foreign stream")
activePlayer.playback.stream = nil
-- ===== Phase H: SlimProto flap during music defers ownership work =====
-- The decoder still plays (stream set, music header); a flap drops the
-- native grant (revoke) and raises yield REVOKED. The fence must keep
-- fencing (flag stays) and must not release the live grant via the
-- reconnect op; reconciliation happens after the decoder goes idle.
activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
activePlayer.playback:_streamConnect("127.0.0.1", 8766)
activePlayer.playback.stream = { id = 11 }
assert(F.isActive(), "H: music not tracked")
bridge.owner = 4
bridge.yield = 0
bridge.idle_admitted = true
bridge.controlsOff = false
fireTimers(5000) -- client observes the grant
-- Flap: owner kept, yield revoked, controls dropped, admission cleared
-- (native sb_ob_connection unconditionally drops idle_admitted).
bridge.yield = 5
bridge.controlsOff = true
bridge.idle_admitted = false
bridge.connection = "99"
fireTimers(5000)
fireTimers(5000)
assert(F.isActive(), "H: flap cleared the music flag")
assert(bridge.owner == 4, "H: reconnect released a live grant")
assert(R1:_begin(STALE, false) == false, "H: radio not fenced during flap")
activePlayer.playback.stream = nil
activePlayer.playback:_streamDisconnect(0, nil)
fireTimers(500)
assert(not F.isActive(), "H: music flag stuck after disconnect")
fireTimers(5000)
fireTimers(5000)
assert(bridge.owner == 0, "H: no reconcile after idle")

-- ===== Phase G: shared OwnershipHttp transport has no duplicate =====
do
    local Http = require("OwnershipHttp")
    local transport = Http.new({ enabled = true, tokenPath = tokenPath,
        jnt = jnt })
    assert(transport, "G: transport refused")
    local bridgeWrites = #bridge.seen
    transport("SBOWN1 hello 4 0 1 0 0 0 0 0 0 0", function(v, err)
        assert(v and v.version == 1, "G: hello rejected (duplicate CL?)")
    end)
    assert(#bridge.seen == bridgeWrites + 1, "G: hello never delivered")
end

-- ===== Phase J: Radio per-start re-wrap chains above the fence =====
-- Live StreamPlayer:_installHooks reassigns playback._streamHttpHeaders
-- (and _streamDisconnect) on EVERY station start. The fence must adopt
-- the new top without wrap-failure errors, while its buried observer
-- keeps detecting Music exactly once through Radio's chain.
do
    local errsBefore = #_G.__fenceLog.errors
    local connectedBefore = #radioEvents.connected
    local notifiesBefore = #radioEvents.notifies
    installRadioInnerHooks(activePlayer.playback, R1)
    fireTimers(5000) -- fence tick re-verifies
    assert(#_G.__fenceLog.errors == errsBefore,
        "J: re-verify error spam on radio re-wrap")
    local infosBefore = #_G.__fenceLog.infos
    activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
    activePlayer.playback:_streamConnect("127.0.0.1", 8766)
    assert(F.isActive(), "J: music not detected through the chain")
    local actives = 0
    for i = infosBefore + 1, #_G.__fenceLog.infos do
        if _G.__fenceLog.infos[i]:find("music active", 1, true) then
            actives = actives + 1
        end
    end
    assert(actives == 1, "J: music activation not idempotent")
    activePlayer.playback.stream = { id = 21 }
    activePlayer.playback:_streamHttpHeaders(
        "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n")
    assert(R1.playbackActive == false, "J: false PLAYING claim survived")
    assert(#radioEvents.connected == connectedBefore,
        "J: onConnected not suppressed")
    for i = notifiesBefore + 1, #radioEvents.notifies do
        assert(radioEvents.notifies[i] ~= "PLAYING",
            "J: PLAYING notification leaked")
    end
    activePlayer.playback.stream = nil
    activePlayer.playback:_streamDisconnect(0, nil)
    assert(R1.reconnectTimer == nil, "J: auto-resume not fenced")
    fireTimers(500)
    assert(not F.isActive(), "J: music flag stuck after disconnect")
end

-- ===== Phase I: live V5 upgrade over a running older fence =====
-- The VM caches modules, so V5 ships as a separate file that must be
-- byte-identical to the tested fence. Install wraps outermost over the
-- old instance; the old tick/adapter is then silenced (V4 pattern) while
-- its wraps stay as consistent inner layers.
do
    local function slurp(path)
        local f = assert(io.open(path, "rb"), "I: cannot open " .. path)
        local data = f:read("*a")
        f:close()
        return data
    end
    assert(slurp(arg[1] .. "/applet/PlaybackFenceV5.lua")
        == slurp(arg[1] .. "/applet/PlaybackFence.lua"),
        "I: V5 drifted from the tested fence")
    local FenceV5 = require("PlaybackFenceV5")
    assert(FenceV5.VERSION == 5, "I: V5 version wrong")
    -- Silence-first: native register while a registration is live gets
    -- 409, so the old client must fault (slot cleared) before V5 joins.
    -- The old wraps stay as consistent inner layers (V4 pattern).
    local incBefore = bridge.incarnation
    F.timerOk = false
    F.joining = false
    assert(F.client ~= nil, "I: no live adapter to hand over")
    F.client:shutdown()
    F.client = nil
    local F5 = FenceV5.install({ marker = marker })
    assert(F5, "I: V5 install refused")
    assert(F5 ~= F, "I: V5 install aliased the old instance")
    assert(FenceV5.install({ marker = marker }) == F5,
        "I: V5 install not idempotent")
    F5.findToken = function() return tokenPath end
    -- V5 adapter joins as a new incarnation.
    fireTimers(5000)
    assert(F5.client ~= nil, "I: V5 adapter did not join")
    assert(bridge.incarnation == incBefore + 1, "I: incarnation not renewed")
    -- Old tick dead: a live old tick would rejoin (client nil) and bump
    -- the incarnation; V5's own tick never re-registers.
    local incSilenced = bridge.incarnation
    fireTimers(15000)
    assert(bridge.incarnation == incSilenced, "I: old tick rejoined")
    -- Fencing works through the double layer: V5 latches music at
    -- connect and the outer wrap refuses Radio starts.
    activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
    activePlayer.playback:_streamConnect("127.0.0.1", 8766)
    activePlayer.playback.stream = { id = 21 }
    assert(F5.isActive(), "I: V5 music not tracked")
    assert(R1:_begin(STALE, false) == false, "I: radio not fenced by V5")
    assert(R1.ownsPlayback == false, "I: ownership leaked via _begin")
    -- V5 holds the flag across a flap (the V3 bug being replaced).
    bridge.yield = 5
    bridge.controlsOff = true
    bridge.idle_admitted = false
    bridge.connection = "100"
    fireTimers(5000)
    fireTimers(5000)
    assert(F5.isActive(), "I: V5 dropped the flag on revoke")
    assert(bridge.owner == 4 or bridge.owner == 0,
        "I: unexpected bridge owner")
    activePlayer.playback.stream = nil
    activePlayer.playback:_streamDisconnect(0, nil)
    fireTimers(500)
    assert(not F5.isActive(), "I: V5 flag stuck after disconnect")
    assert(bridge.incarnation == incSilenced, "I: old tick rejoined late")
    assert(F.timerOk == false, "I: old tick re-armed")
end

-- ===== Phase K: V6 pause-forward fail-before (deselect != pause) =====
-- Live bug proven 2026-11-06 (Dreams epoch 4/5, GYOW epoch 3): a Player
-- deselect with a PAUSED decode unit tears down the stream exactly like a
-- stop -- the fence has no pause path, so the SBOWN1 grant is released
-- instead of forwarded as a slim pause. V6 must add a pause bridge.
--
-- V6 CONTRACT (binding on applet/PlaybackFenceV6.lua):
--   * F6 = FenceV6.install({ marker = marker, cometd = fn })
--     options.cometd injects the slim-request transport; live it POSTs
--     a Bayeux batch
--     [{"channel":"/slim/request","id":"v6-N","response":"/v6/pause",
--       "data":{"request":["<playerId>",["pause",0|1]]}}]
--     to /cometd. Only this shape reaches the Base music path (a bare
--     JSON-RPC method/params body falls to the legacy NowPlaying-only
--     branch and never drives strm-p). options.cometd(frameTable)
--     records/captures in tests.
--   * F6:playerPause({ paused=bool, streamId=num,
--     playbackType="local"|"remote" }) is the ONLY pause entry. The
--     snapshot producer (thin live glue on Player events) reads
--     decode.status()/isPaused; V6 never interprets firmware codes.
--   * LOCAL + paused=true + musicActive: emit ONE pause-1 frame per
--     transition (consecutive duplicates suppressed, pause/resume/
--     pause cycles allowed), hold fencing (PH2: isActive stays true),
--     emit NO bridge writes (no release/reconnect), keep owner granted.
--   * LOCAL + paused=false for a paused epoch: emit ONE pause-0 frame.
--   * REMOTE deselect (paused=false) or unknown streamId: NO cometd
--     frame; existing disconnect teardown path unchanged.
--   * A paused epoch survives its Jive disconnect (the pause unit, not
--     a stop); stop-while-paused reaps once decode goes idle.
-- While V6 is unshipped this phase records the assertions and proves the
-- gap is real (no pause entry on the live fence); it must NOT fail the
-- suite. The moment V6 loads, every K assertion below runs for real.
do
    local vok, FenceV6 = pcall(require, "PlaybackFenceV6")
    if not vok or not FenceV6 then
        local FenceV5 = require("PlaybackFenceV5")
        local live = FenceV5.install({ marker = marker })
        assert(live == nil or live.playerPause == nil,
            "K: pause handling appeared without V6 -- fail-before stale")
        print("Phase K: PENDING -- PlaybackFenceV6 unshipped; "
            .. "pause-forward assertions recorded, NOT verified; "
            .. "live gap stands (deselect-with-pause tears down)")
    else
        assert(FenceV6.VERSION == 6, "K: V6 version wrong")
        local FenceV5 = require("PlaybackFenceV5")
        local F5live = FenceV5.install({ marker = marker })
        -- Silence-first handover (V4 pattern): the old client faults so
        -- the native register slot is clear before V6 joins.
        if F5live then
            F5live.timerOk = false
            F5live.joining = false
            if F5live.client then
                F5live.client:shutdown()
                F5live.client = nil
            end
        end
        local cometdFrames = {}
        activePlayer.id = "00:04:20:22:92:c1"
        local F6 = FenceV6.install({ marker = marker,
            cometd = function(body)
                cometdFrames[#cometdFrames + 1] = body
            end })
        assert(F6, "K: V6 install refused")
        assert(F6.playerPause ~= nil, "K: V6 has no pause bridge")
        F6.findToken = function() return tokenPath end
        local incBefore = bridge.incarnation
        fireTimers(5000) -- V6 adapter joins as a new incarnation
        assert(F6.client ~= nil, "K: V6 adapter did not join")
        assert(bridge.incarnation == incBefore + 1,
            "K: V6 incarnation not renewed")
        -- Manual grant (Phase H convention): the native side admits
        -- the join out of band. Sync tick first so the client
        -- snapshot observes the grant (build 8: the flag tracks the
        -- grant, and hold/release paths read the snapshot).
        bridge.owner = 4
        bridge.yield = 0
        bridge.idle_admitted = true
        bridge.controlsOff = false
        bridge.connection = "101"
        fireTimers(5000)
        -- Latch music on stream epoch 31.
        activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
        activePlayer.playback:_streamConnect("127.0.0.1", 8766)
        activePlayer.playback.stream = { id = 31 }
        assert(F6.isActive(), "K: V6 music not tracked")
        -- K1: local pause forwards exactly one pause-1 slim frame.
        local writesBefore = #bridge.seen
        F6:playerPause({ paused = true, streamId = 31,
            playbackType = "local" })
        assert(#cometdFrames == 1, "K1: pause did not forward exactly once")
        local fr = cometdFrames[1]
        assert(fr.channel == "/slim/request", "K1: not a slim.request")
        assert(type(fr.response) == "string"
            and fr.response:sub(1, 1) == "/", "K1: bad response channel")
        local req = fr.data.request
        assert(req[1] == activePlayer.id, "K1: wrong player id")
        assert(req[2][1] == "pause" and req[2][2] == 1, "K1: not pause-1")
        -- K2: repeat pause for the same epoch emits nothing more.
        F6:playerPause({ paused = true, streamId = 31,
            playbackType = "local" })
        assert(#cometdFrames == 1, "K2: pause re-emitted for same epoch")
        -- K6: no bridge writes and no grant loss across the pause.
        assert(#bridge.seen == writesBefore, "K6: pause touched the bridge")
        assert(bridge.owner == 4, "K6: pause dropped the live grant")
        -- K5: PH2 -- fencing holds while paused.
        assert(F6.isActive(), "K5: pause cleared the music flag")
        assert(R1:_begin(STALE, false) == false,
            "K5: radio not fenced while paused")
        -- K3: resume forwards exactly one pause-0 slim frame.
        F6:playerPause({ paused = false, streamId = 31,
            playbackType = "local" })
        assert(#cometdFrames == 2, "K3: resume did not forward")
        local r3 = cometdFrames[2].data.request
        assert(r3[1] == activePlayer.id, "K3: wrong player id")
        assert(r3[2][1] == "pause" and r3[2][2] == 0, "K3: not pause-0")
        assert(#bridge.seen == writesBefore, "K3: resume touched bridge")
        -- K7: granted teardown holds the flag (buffered decode
        -- outlives the HTTP stream); sustained decoder idle releases
        -- and the flag clears on the release acknowledgement.
        activePlayer.playback.stream = nil
        activePlayer.playback:_streamDisconnect(0, nil)
        fireTimers(500)
        assert(F6.isActive(), "K7: grant not held after disconnect")
        fireTimers(4000)
        assert(bridge.owner == 0, "K7: no release after idle")
        fireTimers(5000)
        fireTimers(5000)
        assert(not F6.isActive(), "K7: flag stuck after release")
        -- K8: a paused epoch survives its Jive disconnect; decode idle
        -- (stop-while-paused) reaps it. Relatch on epoch 33.
        activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
        activePlayer.playback:_streamConnect("127.0.0.1", 8766)
        activePlayer.playback.stream = { id = 33 }
        assert(F6.isActive(), "K8: music not relatched")
        bridge.owner = 4 -- re-grant after K7's release
        bridge.yield = 0
        bridge.idle_admitted = true
        fireTimers(5000) -- snapshot sync
        local writes33 = #bridge.seen
        F6:playerPause({ paused = true, streamId = 33,
            playbackType = "local" })
        assert(#cometdFrames == 3, "K8: re-pause did not forward")
        local r8 = cometdFrames[3].data.request
        assert(r8[2][1] == "pause" and r8[2][2] == 1, "K8: not pause-1")
        decodeState.audioState = 1 -- symbolic non-idle (paused) decode
        activePlayer.playback.stream = nil
        activePlayer.playback:_streamDisconnect(0, nil)
        fireTimers(500)
        assert(F6.isActive(), "K8a: pause did not survive disconnect")
        fireTimers(5000) -- recheck: decode still held, no release
        assert(F6.isActive(), "K8a: pause reaped while decode held")
        for i = writes33 + 1, #bridge.seen do
            assert(bridge.seen[i]:find("^release") ~= 1
                and bridge.seen[i]:find("^reconnect") ~= 1,
                "K8a: held pause released " .. bridge.seen[i])
        end
        assert(bridge.owner == 4, "K8a: held pause dropped the grant")
        decodeState.audioState = 0 -- stop-while-paused: decode idle
        fireTimers(5000)
        assert(F6.pauseHeld == nil, "K8b: stop-while-paused hold stuck")
        assert(F6.isActive(), "K8b: grant not held")
        fireTimers(4000)
        assert(bridge.owner == 0, "K8b: no release after stop")
        fireTimers(5000)
        fireTimers(5000)
        assert(not F6.isActive(), "K8b: flag stuck after release")
        -- K4: remote deselect of an unknown epoch emits no frame.
        local framesBefore = #cometdFrames
        F6:playerPause({ paused = false, streamId = 32,
            playbackType = "remote" })
        assert(#cometdFrames == framesBefore,
            "K4: remote deselect emitted a slim frame")
        assert(not F6.isActive(), "K4: remote deselect latched music")
        -- K10: pause wire bytes match the Base contract. The Base
        -- reads data.response (np_lms_envelope_read); a top-level
        -- response draws a silent 400 (live root cause, builds â‰¤10).
        local wire = F6.serializeCometd({ channel = "/slim/request",
            id = 7, response = "/v6/pause",
            data = { request = { "00:04:20:22:92:c1",
                { "pause", 1 } } } })
        local dAt = wire:find('"data":{', 1, true)
        local rAt = wire:find('"response":"/v6/pause"', 1, true)
        assert(dAt ~= nil and rAt ~= nil and rAt > dAt,
            "K10: response not inside data: " .. wire)
        assert(wire:find('"id":7', 1, true) ~= nil,
            "K10: id not numeric: " .. wire)
        assert(wire:find('"request":["00:04:20:22:92:c1",'
            .. '["pause",1]]', 1, true) ~= nil,
            "K10: bad request payload: " .. wire)
        -- The pre-fix shape must FAIL these checks (discriminates).
        local old = '[{"channel":"/slim/request","id":"v6-1",'
            .. '"response":"/v6/pause","data":{"request":['
            .. '"00:04:20:22:92:c1",["pause",1]]}}]'
        local oR = old:find('"response":"/v6/pause"', 1, true)
        local oD = old:find('"data":{', 1, true)
        assert(oR ~= nil and oD ~= nil and oR < oD,
            "K10: stale-shape guard broken")
    end
end

-- ===== Phase L: V6 Player pause/unpause wraps drive the bridge =====
-- The live glue lives in V6.install: jive.slim.Player pause/unpause
-- (LocalPlayer.pause funnels through Player.pause) forward to
-- playerPause with playbackType from instance:isLocal(). Drive the
-- full press/disconnect/resume cycle through the wrapped methods.
do
    local vok, FenceV6 = pcall(require, "PlaybackFenceV6")
    if not vok or not FenceV6 then
        print("Phase L: PENDING -- PlaybackFenceV6 unshipped")
    else
        local F6 = FenceV6.install({ marker = marker })
        assert(F6.playerWrapped == true, "L: player wraps missing")
        local P = require("jive.slim.Player")
        local localFlag = true
        activePlayer.isLocal = function() return localFlag end
        bridge.owner = 4 -- re-grant after K8b's release
        fireTimers(5000) -- snapshot sync
        -- L1: pause press while gated forwards and holds.
        activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
        activePlayer.playback:_streamConnect("127.0.0.1", 8766)
        activePlayer.playback.stream = { id = 35 }
        assert(F6.isActive(), "L1: music not latched")
        local seq0, pc0 = F6.cometdSeq, playerPauseCalls
        P.pause(activePlayer)
        assert(F6.cometdSeq == seq0 + 1, "L1: pause press not forwarded")
        assert(playerPauseCalls == pc0 + 1, "L1: original pause skipped")
        assert(F6.pauseHeld == F6.musicSince, "L1: pause not held")
        decodeState.audioState = 1
        activePlayer.playback.stream = nil
        activePlayer.playback:_streamDisconnect(0, nil)
        fireTimers(500)
        assert(F6.isActive(), "L1: held pause tore down")
        -- L2: unpause press resumes and releases the hold.
        local seq1, uc0 = F6.cometdSeq, playerUnpauseCalls
        P.unpause(activePlayer)
        assert(F6.cometdSeq == seq1 + 1, "L2: unpause not forwarded")
        assert(playerUnpauseCalls == uc0 + 1, "L2: original unpause skipped")
        assert(F6.pauseHeld == nil, "L2: hold not cleared")
        decodeState.audioState = 0
        fireTimers(5000)
        assert(F6.isActive(), "L2: grant not held")
        fireTimers(4000)
        assert(bridge.owner == 0, "L2: no release after idle")
        fireTimers(5000)
        fireTimers(5000)
        assert(not F6.isActive(), "L2: flag stuck after release")
        -- L3: pause while idle passes through with no frame.
        local seq2, pc2 = F6.cometdSeq, playerPauseCalls
        P.pause(activePlayer)
        assert(F6.cometdSeq == seq2, "L3: idle pause emitted a frame")
        assert(playerPauseCalls == pc2 + 1, "L3: idle pause not passed")
        -- L4: non-local player fails safe (no forward, no hold).
        activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
        activePlayer.playback:_streamConnect("127.0.0.1", 8766)
        activePlayer.playback.stream = { id = 36 }
        assert(F6.isActive(), "L4: music not latched")
        bridge.owner = 4 -- re-grant after L2's release
        bridge.yield = 0
        bridge.idle_admitted = true
        fireTimers(5000) -- snapshot sync
        localFlag = false
        local seq3, pc3 = F6.cometdSeq, playerPauseCalls
        P.pause(activePlayer)
        assert(F6.cometdSeq == seq3, "L4: remote pause forwarded")
        assert(playerPauseCalls == pc3 + 1, "L4: remote pause not passed")
        assert(F6.pauseHeld == nil, "L4: remote pause held the grant")
        localFlag = true
        activePlayer.playback.stream = nil
        activePlayer.playback:_streamDisconnect(0, nil)
        fireTimers(500)
        assert(F6.isActive(), "L4: grant not held")
        fireTimers(4000)
        assert(bridge.owner == 0, "L4: no release after idle")
        fireTimers(5000)
        fireTimers(5000)
        assert(not F6.isActive(), "L4: flag stuck after release")
        -- L5: version stamp lands where the deploy prober looks.
        local stampPath = os.tmpname()
        assert(F6:stampVersion(stampPath), "L5: stamp failed")
        local sf = assert(io.open(stampPath, "r"), "L5: stamp unreadable")
        local stamped = sf:read("*a")
        sf:close()
        os.remove(stampPath)
        assert(stamped:find("version=6", 1, true),
            "L5: wrong stamped version")
    end
end

-- ===== Phase M: V6 action watcher + status snapshot =====
-- The fence watches Jive pause/play/stop actions itself (no other
-- music routing was discoverable), forwards pause/play while gated,
-- never consumes, and publishes a status snapshot every tick and
-- transition.
do
    local vok, FenceV6 = pcall(require, "PlaybackFenceV6")
    if not vok or not FenceV6 then
        print("Phase M: PENDING -- PlaybackFenceV6 unshipped")
    else
        local F6 = FenceV6.install({ marker = marker })
        assert(F6.actionsWatched == true, "M: actions not watched")
        assert(type(actionWatchers.pause) == "function"
            and type(actionWatchers.play) == "function"
            and type(actionWatchers.stop) == "function",
            "M: action set incomplete")
        -- M1: idle pause action is logged, not forwarded.
        local seq0 = F6.cometdSeq
        actionWatchers.pause()
        assert(F6.lastAction == "pause", "M1: pause action not logged")
        assert(F6.cometdSeq == seq0, "M1: idle action forwarded")
        -- M2: gated pause action forwards and holds.
        activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
        activePlayer.playback:_streamConnect("127.0.0.1", 8766)
        activePlayer.playback.stream = { id = 37 }
        assert(F6.isActive(), "M2: music not latched")
        bridge.owner = 4
        bridge.yield = 0
        bridge.idle_admitted = true
        fireTimers(5000) -- snapshot sync
        local seq1 = F6.cometdSeq
        actionWatchers.pause()
        assert(F6.cometdSeq == seq1 + 1, "M2: gated pause not forwarded")
        assert(F6.pauseHeld == F6.musicSince, "M2: action pause not held")
        local snap = F6:statusSnapshot()
        assert(snap.musicActive == true and snap.gated == true
            and snap.owner == 4 and snap.pauseHeld == F6.musicSince
            and snap.version == 6, "M2: bad status snapshot")
        -- M3: play action while held resumes.
        local seq2 = F6.cometdSeq
        actionWatchers.play()
        assert(F6.cometdSeq == seq2 + 1, "M3: play action not forwarded")
        assert(F6.pauseHeld == nil, "M3: hold not cleared")
        -- M4: stop action is logged, never forwarded.
        local seq3 = F6.cometdSeq
        actionWatchers.stop()
        assert(F6.lastAction == "stop", "M4: stop action not logged")
        assert(F6.cometdSeq == seq3, "M4: stop action forwarded")
        -- M5: status file round-trips.
        activePlayer.playback.stream = nil
        activePlayer.playback:_streamDisconnect(0, nil)
        fireTimers(500)
        assert(F6.isActive(), "M5: grant not held")
        fireTimers(4000)
        assert(bridge.owner == 0, "M5: no release after idle")
        fireTimers(5000)
        fireTimers(5000)
        assert(not F6.isActive(), "M5: flag stuck after release")
        local statusPath = os.tmpname()
        F6:writeStatus(statusPath)
        local sf = assert(io.open(statusPath, "r"), "M5: status unreadable")
        local body = sf:read("*a")
        sf:close()
        os.remove(statusPath)
        assert(body:find("musicActive=false", 1, true)
            and body:find("version=6", 1, true),
            "M5: bad status file")
        -- M6: stream-event ring captures verbatim disconnects.
        activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
        activePlayer.playback:_streamConnect("127.0.0.1", 8766)
        activePlayer.playback.stream = { id = 38 }
        activePlayer.playback.stream = nil
        activePlayer.playback:_streamDisconnect(7, true)
        local ring = F6:streamEventLog()
        local sawC, sawD = false, false
        for _, line in ipairs(ring) do
            if line:find("CONNECT", 1, true) then sawC = true end
            if line:find("DISCONNECT reason=7", 1, true)
                and line:find("flush=true", 1, true) then
                sawD = true
            end
        end
        assert(sawC and sawD, "M6: stream ring missed events")
        fireTimers(500)
        -- M7: stopInternal observer traces its invoker, passes through.
        local si0 = stopInternals
        activePlayer.playback:stopInternal()
        assert(stopInternals == si0 + 1, "M7: stopInternal not passed")
        local sawS = false
        for _, line in ipairs(F6:streamEventLog()) do
            if line:find("STOPINTERNAL", 1, true) then sawS = true end
        end
        assert(sawS, "M7: stopInternal not traced")
        -- M6's fireTimers already unlatched via its ring-test
        -- disconnect; no extra teardown (an idle disconnect would
        -- schedule a stale radio reconnect, which is live behavior
        -- for dropout recovery, not test hygiene).
        assert(not F6.isActive(), "M7: flag stuck")
    end
end

-- ===== Phase N: V6 idle-radio stop/pause fencing (the pause killer) ==
-- Live root cause (epochs 6-10): Radio's global pause/stop action
-- listeners (priority -100, consuming) convert EVERY pause press
-- into StreamPlayer:stop() while idle, whose _stopPlayback() hits
-- the shared Playback's stopInternal and murders Music. The fence
-- must run idle-radio stop's state cleanup WITHOUT the decoder
-- touch, fence idle-radio pause at the source, and watch actions at
-- a priority ahead of Radio's.
do
    local vok, FenceV6 = pcall(require, "PlaybackFenceV6")
    if not vok or not FenceV6 then
        print("Phase N: PENDING -- PlaybackFenceV6 unshipped")
    else
        local F6 = FenceV6.install({ marker = marker })
        -- N1: idle-radio pause while gated touches no decoder.
        activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
        activePlayer.playback:_streamConnect("127.0.0.1", 8766)
        activePlayer.playback.stream = { id = 39 }
        assert(F6.isActive(), "N1: music not latched")
        bridge.owner = 4
        bridge.yield = 0
        bridge.idle_admitted = true
        fireTimers(5000) -- snapshot sync
        assert(R1.ownsPlayback == false, "N1: radio not idle")
        local si0, gen0 = stopInternals, R1.generation
        R1:pause()
        assert(stopInternals == si0, "N1: idle radio pause killed music")
        assert(F6.isActive(), "N1: music flag dropped")
        assert(R1.generation == gen0 + 1, "N1: radio cleanup skipped")
        assert(R1.ownsPlayback == false, "N1: ownership leaked to radio")
        -- Tear the latch down on ALL layers (F/F5 latched 39 too).
        activePlayer.playback.stream = nil
        activePlayer.playback:_streamDisconnect(0, nil)
        fireTimers(500)
        assert(F6.isActive(), "N1: grant not held")
        fireTimers(4000)
        assert(bridge.owner == 0, "N1: no release after idle")
        fireTimers(5000)
        fireTimers(5000)
        assert(not F6.isActive(), "N1: flag stuck after release")
        -- N2: owning-radio stop still passes through while gated.
        assert(R1:start(STALE) == true, "N2: radio start broken")
        assert(R1.ownsPlayback == true, "N2: radio ownership broken")
        F6.setActive(true, "test")
        local si1 = stopInternals
        R1:stop()
        assert(stopInternals == si1 + 1, "N2: owning stop fenced wrongly")
        assert(R1.ownsPlayback == false, "N2: radio stop broken")
        F6.setActive(false, "test")
        -- N3: ungated stop passes through (B5 behavior on V6 wraps).
        assert(R1:start(STALE) == true, "N3: radio start broken")
        local si2 = stopInternals
        R1:stop()
        assert(stopInternals == si2 + 1, "N3: idle stop broken")
        -- N4: action watchers run ahead of Radio's consuming -100
        -- (Framework sorts by abs(priority): 50 < 100 runs first).
        assert(actionPrios.pause == -50
            and actionPrios.play == -50
            and actionPrios.stop == -50,
            "N4: watcher priority not ahead of radio")
        assert(not F6.isActive(), "N: flag stuck at end")
        assert(R1.ownsPlayback == false, "N: radio left owning")
    end
end

-- ===== Phase O: grant-race hold (decode busy, snapshot not yet synced) ==
-- Live (build 8, every play): Jive slurps the loopback track in ~6 s and
-- disconnects while the native grant exists but the snapshot still reads
-- ungranted. The backstop must NOT clear into that race while the
-- decoder is busy: re-arm and hold until the grant syncs (latch follows
-- within one tick) or the decoder goes idle.
do
    local vok, FenceV6 = pcall(require, "PlaybackFenceV6")
    if not vok or not FenceV6 then
        print("Phase O: PENDING -- PlaybackFenceV6 unshipped")
    else
        local F6 = FenceV6.install({ marker = marker })
        -- O1: ungranted + decode busy holds across disconnects.
        assert(bridge.owner == 0, "O1: bridge not idle")
        activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
        activePlayer.playback:_streamConnect("127.0.0.1", 8766)
        activePlayer.playback.stream = { id = 40 }
        assert(F6.isActive(), "O1: music not latched")
        decodeState.audioState = 1 -- decode busy, grant in flight
        activePlayer.playback.stream = nil
        activePlayer.playback:_streamDisconnect(0, nil)
        fireTimers(500)
        assert(F6.isActive(), "O1: flag cleared into grant race")
        fireTimers(5000) -- re-armed recheck: still busy, still held
        assert(F6.isActive(), "O1: hold not re-armed")
        assert(bridge.owner == 0, "O1: phantom grant created")
        -- O2: decoder idle with no grant clears (no stuck fence).
        decodeState.audioState = 0
        fireTimers(5000)
        assert(not F6.isActive(), "O2: flag stuck after idle")
        -- O3: the grant sync then latches through the race window.
        activePlayer.playback.header = "GET /music/stream/tok HTTP/1.1"
        activePlayer.playback:_streamConnect("127.0.0.1", 8766)
        activePlayer.playback.stream = { id = 41 }
        assert(F6.isActive(), "O3: music not relatched")
        bridge.owner = 4
        bridge.yield = 0
        bridge.idle_admitted = true
        fireTimers(5000) -- snapshot sync: grant observed
        assert(F6.isActive(), "O3: grant sync dropped the flag")
        assert(bridge.owner == 4, "O3: grant lost")
        -- The granted-hold + idle-release tail on a relatched
        -- epoch is covered by K7/K8b/L2/L4/M5/N1 against live
        -- clients; Phase O's stacked install has no live client
        -- (cumulative-harness adapter hold), so it stops here.
        -- O4: a latch whose stream moved on to non-Music reaps
        -- even while decoding (a new owner holds the decoder).
        -- (Foreign-owner surrender needs a live snapshot; the
        -- stacked install is clientless here, so that clause is
        -- covered by the live radio-regression test instead.)
        activePlayer.playback.header = "GET /radio/stream/x HTTP/1.1"
        activePlayer.playback.stream = { id = 42 }
        decodeState.audioState = 1
        fireTimers(5000)
        fireTimers(5000)
        assert(not F6.isActive(), "O4: moved-on latch not reaped")
    end
end

os.remove(marker)
os.remove(tokenPath)
print("test_playback_fence: PASS")
