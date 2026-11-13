-- PlaybackFence: Jive-VM exclusive-decoder fence and SBOWN1 music adapter.
--
-- WP4a.6 production fix for the WP4a.5 Radio preemption (track 46 cut after
-- ~33 s). Standalone Radio and Spotify Connect drive the SqueezePlay decoder
-- directly from the Jive VM and never consult the native ownership manager,
-- so native-only fencing cannot reach them. This module runs INSIDE the Jive
-- VM (loaded via a skin switch, no Jive restart) and:
--
--   1. Wraps the live Radio StreamPlayer class: auto-resume paths
--      (_handleDisconnect, _scheduleReconnect, watchdog follow-up) and
--      explicit starts (_begin) are refused while a Music stream owns the
--      decoder. Stale desiredStation state is preserved as a user
--      preference; only decoder commands are fenced.
--   2. Wraps the live SpotifyPlayback class: start/pause/resume are refused
--      while Music owns the decoder; stop releases Spotify's claim without
--      touching the decoder.
--   3. Observes the actual decoder stream (playback header + connect /
--      headers / disconnect hooks) to track Music activity locally. No
--      network polling is required for fencing decisions.
--   4. Acts as the SBOWN1 music adapter over the existing Base HTTP
--      transport: honest idle attestation (admit-idle), heartbeat,
--      reconnect after SlimProto flaps, and release after Music stops.
--
-- Everything is marker-gated: without /var/run/standalonebase/music-fence
-- .enabled every wrapper passes through to the original behavior, so
-- deleting the marker is an instant live rollback. Installation is
-- idempotent and refuses unknown application revisions instead of
-- partially patching them.
local M = {}
M.VERSION = 5
M.MARKER = "/var/run/standalonebase/music-fence.enabled"
M.PIDDIR = "/tmp/standalonebase"
M.MUSIC_STREAM_MATCH = "/music/stream/"

local installed = nil

local function tryRequire(name)
    local ok, value = pcall(require, name)
    if ok then return value end
    return nil, value
end

function M.install(options)
    if installed then return installed end
    options = options or {}
    local marker = options.marker or M.MARKER
    local Hooks = tryRequire("applets.StandaloneBase.OwnershipHooks")
    local Runtime = tryRequire("applets.StandaloneBase.OwnershipRuntime")
    local Client = tryRequire("applets.StandaloneBase.OwnershipClient")
    local Player = tryRequire("jive.slim.Player")
    local decode = tryRequire("squeezeplay.decode")
    local Framework = tryRequire("jive.ui.Framework")
    local logModule = tryRequire("jive.utils.log")
    local log = logModule and logModule.logger("StandaloneBase")
        or { info = function() end, warn = function() end, error = function() end }
    if not Hooks or not Runtime or not Client or not Player then
        log:error("PlaybackFence: missing runtime modules; refused")
        return nil, "missing_runtime_modules"
    end

    local self = {
        version = M.VERSION, marker = marker, hooks = Hooks.new(),
        musicActive = false, musicSince = 0, musicEnded = 0,
        idleSamples = 0, client = nil, joining = false,
        radioWrapped = false, spotifyWrapped = false,
        playbackWrapped = false, callbacksWrapped = false,
        spotifyOwns = false, radioOwns = false,
        timerOk = false,
    }
    -- The fence builds its own SBOWN1 transport per join (token discovery
    -- happens lazily); clock/timers/playback come from the runtime.
    local o = Runtime.options({ enabled = true,
        transport = function() end })
    if not o then
        log:error("PlaybackFence: runtime options refused")
        return nil, "runtime_options_refused"
    end
    self.clock = o.clock
    self.after = o.after
    self.getPlayback = o.getPlayback
    self.decode = decode
    self.log = log

    -- Marker state is cached and refreshed on the 5 s tick so hot paths
    -- (including the 1 s audio-recovery wrapper) never touch the disk.
    -- Removing the marker passivates the fence within one tick.
    local markerCache = false
    local function markerPresent()
        return markerCache
    end
    local function refreshMarker()
        local f = io.open(marker, "r")
        if f then f:close() markerCache = true else markerCache = false end
        return markerCache
    end
    refreshMarker()
    self.markerPresent = markerPresent
    self.refreshMarker = refreshMarker

    local function getRadio()
        local manager = appletManager
        if not manager or type(manager.getAppletInstance) ~= "function" then
            return nil
        end
        local ok, radio = pcall(function()
            return manager:getAppletInstance("StandaloneRadio")
        end)
        if ok then return radio end
        return nil
    end

    local function radioPlayer()
        local radio = getRadio()
        return radio and radio.streamPlayer
    end

    local function getSpotify()
        local manager = appletManager
        if not manager or type(manager.getAppletInstance) ~= "function" then
            return nil
        end
        local ok, applet = pcall(function()
            return manager:getAppletInstance("SpotifyConnect")
        end)
        if ok then return applet end
        return nil
    end

    local function spotifyPlayback()
        local applet = getSpotify()
        return applet and applet.playback
    end

    -- Fencing gate: marker armed AND a Music stream observed on the decoder.
    local function gated()
        return markerPresent() and self.musicActive
    end
    self.gated = gated

    local function setActive(active, reason)
        if active then
            self.idleSamples = 0
            if not self.musicActive then
                self.musicActive = true
                self.musicSince = self.clock()
                log:info("PlaybackFence: music active (", tostring(reason), ")")
            end
        else
            if self.musicActive then
                self.musicActive = false
                self.musicEnded = self.clock()
                log:info("PlaybackFence: music idle (", tostring(reason), ")")
            end
            self.idleSamples = 0
        end
    end
    self.setActive = setActive
    self.isActive = function() return self.musicActive end

    local function detectMusicStream(playback, reason)
        if not playback then return end
        local header = playback.header
        if type(header) == "string"
            and header:find(M.MUSIC_STREAM_MATCH, 1, true) then
            setActive(true, reason)
        end
    end

    -- Undo Radio's false PLAYING claim residue after its headers hook ran
    -- for a Music stream. Notifications were already suppressed by the
    -- _notifyState wrapper; this resets the mirroring fields.
    local function undoRadioClaim()
        if not markerPresent() or not self.musicActive then return end
        local player = radioPlayer()
        if player and not player.ownsPlayback and player.playbackActive then
            player.playbackActive = false
            player.currentMetadata = nil
            if player.desiredStation then
                player.desiredStation.observedAudio = nil
            end
            log:info("PlaybackFence: neutralized foreign stream claim")
        end
    end

    -- Application modules use `module()` (methods hidden on a local class
    -- table) or `oo.class(_M)` (methods on the module table). Resolve the
    -- live class through a running instance's metatable first, the module
    -- table second. Missing class means absent-or-late (defer to the tick
    -- retry); a resolved class with the wrong shape refuses the install.
    local function resolveClass(getInstance, moduleName, probe)
        local instance = getInstance()
        if instance then
            local mt = getmetatable(instance)
            if type(mt) == "table"
                and type(mt[probe]) == "function" then
                return mt
            end
        end
        local mod = tryRequire(moduleName)
        if type(mod) == "table"
            and type(mod[probe]) == "function" then
            return mod
        end
        return nil
    end

    local function wrapRadio()
        if self.radioWrapped then return true end
        local StreamPlayer = resolveClass(radioPlayer,
            "applets.StandaloneRadio.StreamPlayer", "_handleDisconnect")
        if not StreamPlayer then
            return nil, "radio_absent"
        end
        for _, key in ipairs({"_handleDisconnect", "_scheduleReconnect",
            "_begin", "_notifyState", "_audioRecoveryTick",
            "_handleMetadata", "_resetAudioRecovery"}) do
            if type(StreamPlayer[key]) ~= "function" then
                log:error("PlaybackFence: unsupported Radio revision (",
                    tostring(key), ")")
                return nil, "unsupported_radio_revision"
            end
        end
        local hooks = self.hooks
        if not hooks:install(StreamPlayer, "_handleDisconnect",
            function(original)
                return function(instance, reason, flush)
                    if gated() then
                        instance.playbackActive = false
                        instance.currentMetadata = nil
                        if instance.desiredStation then
                            instance.desiredStation.observedAudio = nil
                        end
                        log:info("PlaybackFence: radio auto-resume fenced")
                        return
                    end
                    return original(instance, reason, flush)
                end
            end) then
            return nil, "radio_disconnect_wrap_failed"
        end
        if not hooks:install(StreamPlayer, "_scheduleReconnect",
            function(original)
                return function(instance, station, generation, redirect)
                    if gated() then
                        log:info("PlaybackFence: radio reconnect fenced")
                        return
                    end
                    return original(instance, station, generation, redirect)
                end
            end) then
            return nil, "radio_reconnect_wrap_failed"
        end
        if not hooks:install(StreamPlayer, "_begin",
            function(original)
                return function(instance, station, reconnect)
                    if gated() then
                        log:warn("PlaybackFence: radio start fenced during music")
                        return false
                    end
                    local ok = original(instance, station, reconnect)
                    if ok and markerPresent() then
                        self.radioOwns = instance.ownsPlayback == true
                        if self.radioOwns then self:adapterHold() end
                    end
                    return ok
                end
            end) then
            return nil, "radio_begin_wrap_failed"
        end
        -- Only the decoder owner may drive Radio UI/applet state. Legit
        -- Radio flows set ownsPlayback before connecting, so this only
        -- suppresses claims for foreign (Music) streams.
        if not hooks:install(StreamPlayer, "_notifyState",
            function(original)
                return function(instance, station, state, show)
                    if markerPresent() and not instance.ownsPlayback
                        and (state == "PLAYING"
                            or state == "CONNECTION_LOST"
                            or state == "RECONNECTING") then
                        instance.state = state
                        return
                    end
                    return original(instance, station, state, show)
                end
            end) then
            return nil, "radio_notify_wrap_failed"
        end
        if not hooks:install(StreamPlayer, "_audioRecoveryTick",
            function(original)
                return function(instance)
                    if markerPresent() and not instance.ownsPlayback then
                        instance:_resetAudioRecovery()
                        return
                    end
                    return original(instance)
                end
            end) then
            return nil, "radio_recovery_wrap_failed"
        end
        if not hooks:install(StreamPlayer, "_handleMetadata",
            function(original)
                return function(instance, data)
                    if markerPresent() and not instance.ownsPlayback then
                        return
                    end
                    return original(instance, data)
                end
            end) then
            return nil, "radio_metadata_wrap_failed"
        end
        -- Radio stop() always passes through; record the ownership drop so
        -- the adapter can rejoin once the decoder is idle.
        if type(StreamPlayer.stop) == "function" then
            if not hooks:install(StreamPlayer, "stop",
                function(original)
                    return function(instance, state)
                        local result = original(instance, state)
                        if markerPresent() then
                            self.radioOwns = false
                        end
                        return result
                    end
                end) then
                return nil, "radio_stop_wrap_failed"
            end
        end
        self.radioWrapped = true
        log:info("PlaybackFence: radio wrappers installed")
        return true
    end

    local wrappedCallbackTables = {}
    local function wrapRadioCallbacksFor(player)
        if not player or type(player.callbacks) ~= "table" then
            return false
        end
        -- Instance-level callbacks table; wrap in place exactly once.
        if wrappedCallbackTables[player.callbacks] then return true end
        if player.callbacks._sbFenceV1 then
            wrappedCallbackTables[player.callbacks] = true
            return true
        end
        local original = player.callbacks.onConnected
        if type(original) == "function" then
            player.callbacks.onConnected =
                function(station, generation)
                    if markerPresent() and not player.ownsPlayback then
                        log:info("PlaybackFence: radio onConnected suppressed")
                        return
                    end
                    return original(station, generation)
                end
        end
        player.callbacks._sbFenceV1 = true
        wrappedCallbackTables[player.callbacks] = true
        log:info("PlaybackFence: radio callbacks wrapped")
        return true
    end

    local function wrapRadioCallbacks()
        if wrapRadioCallbacksFor(radioPlayer()) then
            self.callbacksWrapped = true
            return true
        end
        return false
    end

    local function wrapSpotify()
        if self.spotifyWrapped then return true end
        local Playback = resolveClass(spotifyPlayback,
            "applets.SpotifyConnect.SpotifyPlayback", "start")
        if not Playback then
            return nil, "spotify_absent"
        end
        for _, key in ipairs({"start", "stop", "pause", "resume"}) do
            if type(Playback[key]) ~= "function" then
                log:error("PlaybackFence: unsupported Spotify revision (",
                    tostring(key), ")")
                return nil, "unsupported_spotify_revision"
            end
        end
        local hooks = self.hooks
        if not hooks:install(Playback, "start",
            function(original)
                return function(instance, host, port, path)
                    if gated() then
                        log:warn("PlaybackFence: spotify start fenced during music")
                        return false
                    end
                    local ok = original(instance, host, port, path)
                    if ok and markerPresent() then
                        self.spotifyOwns = true
                        self:adapterHold()
                    end
                    return ok
                end
            end) then
            return nil, "spotify_start_wrap_failed"
        end
        if not hooks:install(Playback, "stop",
            function(original)
                return function(instance)
                    if gated() then
                        -- Release Spotify's claim without touching the
                        -- Music-owned decoder.
                        instance.playback = nil
                        instance.paused = false
                        self.spotifyOwns = false
                        log:info("PlaybackFence: spotify stop fenced (claim released)")
                        return true
                    end
                    local result = original(instance)
                    if markerPresent() then
                        self.spotifyOwns = false
                    end
                    return result
                end
            end) then
            return nil, "spotify_stop_wrap_failed"
        end
        for _, key in ipairs({"pause", "resume"}) do
            if not hooks:install(Playback, key,
                function(original)
                    return function(instance, ...)
                        if gated() then
                            log:info("PlaybackFence: spotify ", key,
                                " fenced during music")
                            return false
                        end
                        return original(instance, ...)
                    end
                end) then
                return nil, "spotify_" .. key .. "_wrap_failed"
            end
        end
        self.spotifyWrapped = true
        log:info("PlaybackFence: spotify wrappers installed")
        return true
    end

    local function mkConnect(original)
        return function(instance, ...)
            detectMusicStream(instance, "connect")
            return original(instance, ...)
        end
    end
    local function mkHeaders(original)
        return function(instance, ...)
            local result = original(instance, ...)
            detectMusicStream(instance, "headers")
            undoRadioClaim()
            return result
        end
    end
    local function mkDisconnect(original)
        return function(instance, reason, flush)
            local result = original(instance, reason, flush)
            if instance.stream == nil then
                self.after(500, function()
                    if instance.stream == nil then
                        setActive(false, "disconnect")
                        self:maybeRelease()
                    end
                end)
            end
            return result
        end
    end
    local function wrapPlayback()
        local playback = self.getPlayback()
        if not playback then return false end
        local hooks = self.hooks
        if self.playbackWrapped and self.wrappedPlayback == playback then
            -- Chain-aware re-verify: Radio stacks its own header hooks
            -- above ours on every station start. Adopt the new top
            -- instead of fighting it; the buried observer keeps
            -- running through Radio's chain (it calls its original).
            if hooks:reinstall(playback, "_streamConnect", mkConnect)
                and hooks:reinstall(playback,
                    "_streamHttpHeaders", mkHeaders)
                and hooks:reinstall(playback,
                    "_streamDisconnect", mkDisconnect) then
                self.wrappedConnect = playback._streamConnect
                self.wrappedDisconnect = playback._streamDisconnect
                self.wrappedHeaders = playback._streamHttpHeaders
                return true
            end
            return nil, "playback_readopt_failed"
        end
        if type(playback._streamConnect) ~= "function"
            or type(playback._streamDisconnect) ~= "function"
            or type(playback._streamHttpHeaders) ~= "function" then
            log:error("PlaybackFence: unsupported Playback revision")
            return nil, "unsupported_playback_revision"
        end
        if not hooks:install(playback, "_streamConnect", mkConnect) then
            return nil, "playback_connect_wrap_failed"
        end
        if not hooks:install(playback, "_streamHttpHeaders", mkHeaders) then
            return nil, "playback_headers_wrap_failed"
        end
        if not hooks:install(playback, "_streamDisconnect", mkDisconnect) then
            return nil, "playback_disconnect_wrap_failed"
        end
        self.wrappedPlayback = playback
        self.wrappedConnect = playback._streamConnect
        self.wrappedDisconnect = playback._streamDisconnect
        self.wrappedHeaders = playback._streamHttpHeaders
        self.playbackWrapped = true
        log:info("PlaybackFence: playback observers installed")
        return true
    end

    local function radioQuiet()
        local player = radioPlayer()
        if not player then return true end
        return not player.ownsPlayback and not player.playbackActive
            and player.pendingStation == nil and player.reconnectTimer == nil
    end
    self.radioQuiet = radioQuiet

    local function spotifyQuiet()
        local playback = spotifyPlayback()
        if not playback then return true end
        return playback.playback == nil
    end
    self.spotifyQuiet = spotifyQuiet

    -- Passive decoder probe: never stops anything. Mirrors the firmware
    -- state fields used by the ownership qualification.
    local function decoderIdle()
        local playback = self.getPlayback()
        if not playback or playback.stream ~= nil
            or playback.source == "capture" then
            return false
        end
        if self.decode then
            local ok, status = pcall(function()
                return self.decode:status()
            end)
            if not ok or type(status) ~= "table" then return false end
            if status.decodeState ~= 0 or status.audioState ~= 0 then
                return false
            end
        end
        return true
    end
    self.decoderIdle = decoderIdle

    local function allQuiet()
        return radioQuiet() and spotifyQuiet() and decoderIdle()
    end
    self.allQuiet = allQuiet

    -- Token discovery: the Base incarnation directory changes per restart
    -- and stale ones linger, so resolve via the live process table.
    local function tokenFromCmdline(cmd)
        if type(cmd) == "string" and cmd:find("sbbase-music", 1, true) then
            local token = cmd:match("%-%-ownership%-token%z([^%z]+)")
            if token and token ~= "" then return token end
        end
        return nil
    end
    local function findToken()
        local lfs = tryRequire("lfs")
        if lfs and type(lfs.dir) == "function" then
            local ok, iter = pcall(function() return lfs.dir("/proc") end)
            if ok and type(iter) == "function" then
                for name in iter do
                    if type(name) == "string" and name:match("^%d+$") then
                        local c = io.open("/proc/" .. name .. "/cmdline", "r")
                        if c then
                            local cmd = c:read("*a")
                            c:close()
                            local token = tokenFromCmdline(cmd)
                            if token then return token end
                        end
                    end
                end
            end
        end
        -- Fallback: newest readable token under /dev.
        if lfs and type(lfs.dir) == "function" then
            local best, bestTime
            local ok, iter = pcall(function() return lfs.dir("/dev") end)
            if ok and type(iter) == "function" then
                for name in iter do
                    if type(name) == "string"
                        and name:match("^sbmusic%-") then
                        local path = "/dev/" .. name .. "/owner/token"
                        local attr = lfs.attributes(path)
                        if attr and attr.mode == "file" then
                            local t = attr.modification or 0
                            if not bestTime or t >= bestTime then
                                best, bestTime = path, t
                            end
                        end
                    end
                end
            end
            if best then return best end
        end
        return nil
    end
    self.findToken = findToken

    -- ownership_bridge.h: NONE=0 RADIO=1 SPOTIFY=2 QOBUZ=3 MUSIC=4;
    -- yield IDLE=0 REQUESTED=1 ACK=2 REFUSED=3 TIMEOUT=4 REVOKED=5.
    local function snapshot()
        return self.client and self.client.snapshot
    end

    function self:maybeRelease()
        local v = snapshot()
        if not v or not markerPresent() or not self.client
            or not self.client.active then
            return
        end
        if v.owner == 4 and v.yield == 0 and not self.musicActive
            and allQuiet()
            and self.clock() - self.musicEnded > 3000 then
            self.client:command("release", 7, "0", 0,
                function(reply, err)
                    if reply then
                        log:info("PlaybackFence: music ownership released")
                    else
                        log:warn("PlaybackFence: release refused (",
                            tostring(err), ")")
                    end
                end)
        end
    end

    -- Another application legitimately took the decoder (observed via its
    -- own start path). Hold admission until it is quiet again.
    function self:adapterHold()
        if self.client then
            log:info("PlaybackFence: holding adapter for foreign owner")
            self.client:shutdown()
            self.client = nil
        end
        self.joining = false
    end

    local function seamAdapter(incarnation, send)
        local adapter = { grant = nil }
        function adapter:authorize(generation, connection)
            self.grant = { generation = generation,
                connection = connection }
            return true
        end
        function adapter:canStart(generation, connection)
            return self.grant ~= nil
                and self.grant.generation == generation
                and self.grant.connection == connection
        end
        function adapter:invalidate()
            self.grant = nil
        end
        -- Yield delivery: unreachable under idle_only (no other adapter
        -- may register), but answer honestly if it ever arrives.
        function adapter:request(req)
            log:warn("PlaybackFence: unexpected yield request; cleaning up")
            if not self.client or not self.client.active then
                return false
            end
            self.client:command("cleanup", 0, "0", 0,
                function(v, err)
                    local quiet = err == nil and allQuiet()
                    send({ owner = "music", incarnation = incarnation,
                        request_id = req.request_id,
                        generation = req.generation,
                        connection = req.connection,
                        accepted = quiet, producer_stopped = quiet,
                        hooks_released = true,
                        decoder_quiescent = quiet })
                    if quiet then
                        setActive(false, "yield")
                    end
                end)
            return true
        end
        function adapter:tick() end
        return adapter
    end

    -- SBOWN1-over-HTTP transport, inlined so the fence is a single
    -- deployable module. Live SocketHttp always adds Content-Length
    -- itself; passing another copy makes the bridge reject the call.
    local function makeTransport(tokenPath)
        local f = io.open(tokenPath, "r")
        if not f then return nil end
        local token = f:read(65)
        f:close()
        if not token or #token ~= 64
            or not token:match("^[0-9a-f]+$") then
            return nil
        end
        local RequestHttp = tryRequire("jive.net.RequestHttp")
        local SocketHttp = tryRequire("jive.net.SocketHttp")
        local json = tryRequire("json")
        if not RequestHttp or not SocketHttp or not json then
            return nil
        end
        return function(body, done)
            local sent, finished = false, false
            local request = RequestHttp(function(data, err)
                if finished then return end
                if err then
                    finished = true
                    done(nil, "transport_error")
                    return
                end
                if data and #data <= 4096 then
                    local ok, value = pcall(json.decode, data)
                    if ok and type(value) == "table" then
                        finished = true
                        if value.error then
                            done(nil, "bridge_rejected")
                        else
                            done(value)
                        end
                    end
                elseif data then
                    finished = true
                    done(nil, "oversized_reply")
                end
            end, "POST", "/api/music/ownership", {
                headers = { Connection = "close",
                    ["X-Music-Ownership"] = token },
                t_bodySource = function()
                    if not sent then sent = true return body end
                end,
            })
            local socket = SocketHttp(o.jnt, "127.0.0.1", 9000,
                "StandaloneFence")
            socket:fetch(request)
        end
    end

    local function joinLocked()
        if self.joining or self.client then return end
        if not markerPresent() then return end
        if self.musicActive or not allQuiet() then return end
        local tokenPath = self.findToken()
        if not tokenPath then
            log:warn("PlaybackFence: no ownership token found")
            return
        end
        local transport = makeTransport(tokenPath)
        if not transport then
            log:warn("PlaybackFence: token unreadable")
            return
        end
        self.joining = true
        local client = Client.new({ enabled = true, owner = "music",
            clock = self.clock, after = self.after,
            transport = transport, seam = seamAdapter,
            grant = function()
                log:info("PlaybackFence: music grant observed")
            end,
            revoke = function()
                -- A flap drops the native grant while the decoder keeps
                -- playing. The music flag tracks the decoder, never
                -- native ownership: keep fencing, reconcile when idle.
                log:warn("PlaybackFence: music grant revoked; holding fence")
            end,
            fault = function(reason)
                log:error("PlaybackFence: adapter fault (",
                    tostring(reason), ")")
                self.client = nil
                self.joining = false
            end,
            observed = function() end,
            ready = function(c)
                log:info("PlaybackFence: adapter registered")
                self.client = c
                self.joining = false
                c:poll()
                self:pollAdapter()
            end })
        if not client then
            log:error("PlaybackFence: adapter construction refused")
            self.joining = false
            return
        end
    end

    function self:pollAdapter()
        if not markerPresent() then
            if self.client then
                log:info("PlaybackFence: marker removed; holding adapter")
                self:adapterHold()
            end
            return
        end
        if not self.client then
            joinLocked()
            return
        end
        local v = snapshot()
        if not v then return end
        if self.client.busy then return end
        if not v.idle_admitted and v.owner == 0 and not v.suspended
            and v.connected then
            if self.musicActive or not allQuiet() then return end
            self.client:command("admit-idle", 7, "0", 0,
                function(reply, err)
                    if reply and reply.idle_admitted then
                        log:info("PlaybackFence: idle admitted")
                    else
                        log:warn("PlaybackFence: admit-idle refused (",
                            tostring(err), ")")
                    end
                end)
            return
        end
        if not v.idle_admitted and v.owner == 4 and v.yield == 5
            and v.connected then
            if self.musicActive then
                -- The decoder still plays: reconnect would release a
                -- live grant. Defer until the decoder goes idle.
                log:warn("PlaybackFence: reconnect deferred until idle")
                return
            end
            -- SlimProto flap while holding: reconcile, then re-admit.
            self.client:command("reconnect", 7, "0", 0,
                function(reply, err)
                    if reply then
                        log:info("PlaybackFence: reconnect reconciled")
                    else
                        log:warn("PlaybackFence: reconnect refused (",
                            tostring(err), ")")
                    end
                end)
            return
        end
        self:maybeRelease()
    end

    local function headerIsMusic(playback)
        local header = playback and playback.header
        return type(header) == "string"
            and header:find(M.MUSIC_STREAM_MATCH, 1, true) ~= nil
    end

    -- Retry helper: absent applications are retried on the tick (they
    -- may load later); resolved-but-wrong revisions refuse loudly.
    local lastWrapErr = {}
    local function wrapRetry(flag, fn, name)
        if self[flag] then return true end
        local ok, err = fn()
        if ok then
            if lastWrapErr[name] then
                log:info("PlaybackFence: ", name, " wraps completed late")
                lastWrapErr[name] = nil
            end
            return true
        end
        if err ~= lastWrapErr[name] then
            lastWrapErr[name] = err
            log:warn("PlaybackFence: ", name, " wraps pending (",
                tostring(err), ")")
        end
        return nil, err
    end

    -- Sure-path ownership observation: the _begin wrappers are the fast
    -- path, but an application that started between ticks (or before its
    -- wraps completed) is still caught here within 5 s.
    local function observeForeignOwnership()
        local rp = radioPlayer()
        if rp and rp.ownsPlayback then
            self.radioOwns = true
            if self.client then self:adapterHold() end
        elseif self.radioOwns and (not rp or not rp.ownsPlayback) then
            self.radioOwns = false
        end
        local sp = spotifyPlayback()
        if sp and sp.playback ~= nil then
            self.spotifyOwns = true
            if self.client then self:adapterHold() end
        elseif self.spotifyOwns and (not sp or sp.playback == nil) then
            self.spotifyOwns = false
        end
    end

    local function tick()
        if not self.timerOk then return end
        self.after(5000, tick)
        refreshMarker()
        -- Playback object may appear late or be replaced; verify wraps.
        local ok, err = wrapPlayback()
        if ok == nil then
            log:error("PlaybackFence: playback wrap failed (",
                tostring(err), ")")
        end
        wrapRadioCallbacks()
        wrapRetry("radioWrapped", wrapRadio, "radio")
        wrapRetry("spotifyWrapped", wrapSpotify, "spotify")
        observeForeignOwnership()
        -- Local backstop: reap a stale active flag when the decoder is
        -- provably idle (covers missed disconnect observations) or has
        -- moved on to a non-Music stream (alarm tone, explicit stop).
        local playback = self.getPlayback()
        if self.musicActive and playback
            and (playback.stream == nil or not headerIsMusic(playback)) then
            self.idleSamples = self.idleSamples + 1
            if self.idleSamples >= 2 then
                setActive(false, "backstop")
                self:maybeRelease()
            end
        else
            self.idleSamples = 0
        end
        self:pollAdapter()
    end

    -- Install order: application wraps first (pure Lua tables), playback
    -- observers when the player exists, adapter via the poll loop.
    local ok, err = wrapRetry("radioWrapped", wrapRadio, "radio")
    if not ok and err ~= "radio_absent" then
        log:error("PlaybackFence: install refused (", tostring(err), ")")
        return nil, err
    end
    ok, err = wrapRetry("spotifyWrapped", wrapSpotify, "spotify")
    if not ok and err ~= "spotify_absent" then
        log:error("PlaybackFence: install refused (", tostring(err), ")")
        return nil, err
    end
    wrapRadioCallbacks()
    ok, err = wrapPlayback()
    if ok == nil then
        log:error("PlaybackFence: install refused (", tostring(err), ")")
        return nil, err
    end
    self.timerOk = true
    self.after(5000, tick)
    log:info("PlaybackFence: installed version=", M.VERSION,
        " marker=", markerPresent() and "armed" or "passive")
    installed = self
    return self
end

return M
