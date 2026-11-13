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
--   5. (V6) Wraps jive.slim.Player pause/unpause (LocalPlayer
--      funnels through Player.pause after its timeout guard): a pause
--      press while Music owns the decoder is forwarded to the Base as
--      a Bayeux /slim/request ["pause",1] frame over /cometd and the
--      grant is HELD across the accompanying local disconnect instead
--      of released. Unpause forwards ["pause",0] and clears the hold.
--      No bridge writes happen while paused; fencing holds until
--      resume or stop. Non-music pauses pass through untouched.
--   6. (Build 11) Pause frames carry a NUMERIC CometD id with the
--      response path nested under data (the Base envelope contract);
--      the legacy top-level response + string id drew a silent 400.
--   7. (Build 12 DIAG, temporary) Snoop Jive's /cometd reply bodies
--      into fence.snoop to name the Now Playing 121-byte reply.
--   8. (Build 13 DIAG, temporary) Snoop URL filter fixed
--      (t_httpRequest.uri.path); ownership noise excluded.
--   9. (Build 14 DIAG, temporary) Snoop also captures /cometd
--      REQUEST bodies (wrap, never consume).
--   7. (V6 build 8) The music flag tracks the SBOWN1 GRANT
--      (owner==4), not the Jive HTTP stream object: Jive slurps a
--      whole loopback track in seconds (clean FIN) and decodes from
--      its buffer for minutes with no stream. Disconnects and the
--      backstop never clear a granted flag; release comes from
--      sustained decoder idle (releaseIdle) and the flag clears on
--      release acknowledgement. Flaps (owner lost, decode busy) hold.
--
-- Everything is marker-gated: without /var/run/standalonebase/music-fence
-- .enabled every wrapper passes through to the original behavior, so
-- deleting the marker is an instant live rollback. Installation is
-- idempotent and refuses unknown application revisions instead of
-- partially patching them.
local M = {}
M.VERSION = 6
M.BUILD = 14
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
        playerWrapped = false,
        spotifyOwns = false, radioOwns = false,
        timerOk = false,
        -- V6 pause bridge: pauseEpochs[streamId] is "paused" or
        -- "resumed"; pauseHeld is the streamId whose Jive disconnect
        -- must not release the grant (stop-while-paused reaps it once
        -- the decode goes idle). cometdSeq numbers slim frames.
        pauseEpochs = {}, pauseHeld = nil, cometdSeq = 0,
        cometd = options.cometd, lastAction = nil, lastActionAt = 0,
        build = M.BUILD,
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
            if self.streamRingPush then
                self.streamRingPush("FLAG-OFF reason=" .. tostring(reason))
            end
            self.idleSamples = 0
            -- V6: idle retires every pause epoch; a later play starts
            -- fresh pause/resume transitions.
            self.pauseHeld = nil
            self.pauseEpochs = {}
        end
        self:writeStatus()
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
            "_handleMetadata", "_resetAudioRecovery",
            "stop", "pause"}) do
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
                    -- Build 10 diag (temporary): name every
                    -- reconnect scheduler entry.
                    if self.streamRingPush then
                        self.streamRingPush("RSCHED gated="
                            .. tostring(gated()))
                    end
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
                    -- Build 10 diag (temporary): name every
                    -- radio start entry.
                    if self.streamRingPush then
                        self.streamRingPush("RBEGIN recon="
                            .. tostring(reconnect)
                            .. " gated=" .. tostring(gated()))
                    end
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
        -- V6: an idle Radio stop must not touch a Music-owned
        -- decoder. Radio's global pause/stop action listeners (priority
        -- -100, consuming) convert EVERY pause press into
        -- StreamPlayer:stop() while idle, which _stopPlayback()s the
        -- shared Playback: that was the WP4a.6 pause-kills-music bug
        -- (epochs 6-10). Fenced path mirrors stop()'s full state
        -- cleanup minus the decoder touch. A Radio that OWNS the
        -- decoder, or any stop while ungated, passes through.
        if not hooks:install(StreamPlayer, "stop",
            function(original)
                return function(instance, state)
                    -- Build 10 diag (temporary): name every
                    -- radio stop entry.
                    if self.streamRingPush then
                        self.streamRingPush("RSTOP owns="
                            .. tostring(instance.ownsPlayback)
                            .. " gated=" .. tostring(gated()))
                    end
                    if gated() and not instance.ownsPlayback then
                        instance.paused = false
                        instance.episodeEOF = false
                        instance.lastPosition = nil
                        if instance.hlsMetadata then
                            instance.hlsMetadata:stop()
                        end
                        if instance.playlistResolver
                            and instance.playlistResolver
                                .cancelPlaylist then
                            instance.playlistResolver:cancelPlaylist()
                        end
                        instance.generation = instance.generation + 1
                        instance.ownsPlayback = false
                        instance:_cancelReconnect()
                        instance.pendingStation = nil
                        instance.currentMetadata = nil
                        instance.playbackActive = false
                        instance:_resetWatchdog()
                        instance:_resetAudioRecovery()
                        -- _stopPlayback() deliberately skipped.
                        if instance.lastStation then
                            instance:_notifyState(instance.lastStation,
                                state or "STOPPED", false)
                        end
                        self.radioOwns = false
                        log:info("PlaybackFence: radio stop fenced " ..
                            "(decoder untouched)")
                        return true
                    end
                    local result = original(instance, state)
                    if markerPresent() then
                        self.radioOwns = false
                    end
                    return result
                end
            end) then
            return nil, "radio_stop_wrap_failed"
        end
        -- V6: fence idle-radio pause at the source (the global pause
        -- listener calls pause() directly). Delegate to the wrapped
        -- stop so radio runs its full state cleanup (generation bump
        -- invalidates pending radio ops) without the decoder touch.
        -- Owning radio (podcast pause) passes through.
        if not hooks:install(StreamPlayer, "pause",
            function(original)
                return function(instance)
                    -- Build 10 diag (temporary): name every
                    -- radio pause entry.
                    if self.streamRingPush then
                        self.streamRingPush("RPAUSE owns="
                            .. tostring(instance.ownsPlayback)
                            .. " gated=" .. tostring(gated()))
                    end
                    if gated() and not instance.ownsPlayback then
                        log:info("PlaybackFence: radio pause fenced")
                        return instance:stop()
                    end
                    return original(instance)
                end
            end) then
            return nil, "radio_pause_wrap_failed"
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

    -- Build 4: stream-event ring. The mid-play killer sends DSCO
    -- +STMf from Jive; capture verbatim (reason, flush, stream, decode
    -- codes) so the next kill is attributable. In-memory ring (20),
    -- flushed to the status dir on every event.
    local streamRing = {}
    function self:streamEventLog()
        return streamRing
    end
    local function ringPush(text)
        streamRing[#streamRing + 1] = tostring(self.clock()) .. " " .. text
        while #streamRing > 20 do table.remove(streamRing, 1) end
        pcall(function()
            local f = assert(io.open(
                "/tmp/standalonebase/fence.streams", "w"))
            for _, line in ipairs(streamRing) do
                f:write(line, "\n")
            end
            f:close()
        end)
    end
    self.streamRingPush = ringPush
    -- Build 12 diag (temporary): snoop Jive->Base /cometd REPLY
    -- bodies (request bodies can't be re-read safely). Proves what
    -- the Now Playing query actually gets back. Stripped in final.
    local snoopRing = {}
    local function snoopPush(data)
        snoopRing[#snoopRing + 1] = tostring(self.clock())
            .. " " .. tostring(data):sub(1, 1200)
        while #snoopRing > 4 do table.remove(snoopRing, 1) end
        pcall(function()
            local f = assert(io.open(
                "/tmp/standalonebase/fence.snoop", "w"))
            for _, line in ipairs(snoopRing) do
                f:write(line, "\n")
            end
            f:close()
        end)
    end
    local function wrapSnoop()
        local SocketHttp = tryRequire("jive.net.SocketHttp")
        if type(SocketHttp) ~= "table" then return true end
        if SocketHttp._sbSnoop then return true end
        if type(SocketHttp.fetch) ~= "function" then
            return nil, "unsupported_socket_revision"
        end
        local origFetch = SocketHttp.fetch
        SocketHttp._sbSnoop = true
        SocketHttp.fetch = function(socket, request)
            local ours = request and request._sbFenceOurs
            origFetch(socket, request)
            if ours then return end
            local ok, prev = pcall(function()
                return request.t_setResponseBody
            end)
            if not ok or type(prev) ~= "function" then return end
            if request._sbSnoopWrapped then return end
            request._sbSnoopWrapped = true
            local url = ""
            pcall(function()
                local spec = request.t_httpRequest
                if spec and spec.uri then
                    url = tostring(spec.uri.path or "")
                end
            end)
            request.t_setResponseBody = function(req, data)
                if type(data) == "string" and #data > 0
                    and (url == "" or url:find("cometd", 1, true))
                    and data:find("^%s*[[{]") then
                    pcall(snoopPush, data)
                end
                return prev(req, data)
            end
            -- Build 14: also capture REQUEST bodies (wrap, never
            -- consume: the fetcher calls our wrapper instead).
            local isCometd = url == ""
                or url:find("cometd", 1, true) ~= nil
            local okS, source = pcall(function()
                return request.t_bodySource
            end)
            if okS and isCometd and not request._sbSnoopSrcWrapped then
                if type(source) == "string" and #source > 0 then
                    request._sbSnoopSrcWrapped = true
                    pcall(snoopPush, "REQ " .. source:sub(1, 400))
                elseif type(source) == "function" then
                    request._sbSnoopSrcWrapped = true
                    request.t_bodySource = function(...)
                        local okR, body = pcall(source, ...)
                        if not okR then error(body) end
                        if type(body) == "string" and #body > 0 then
                            pcall(snoopPush, "REQ " .. body:sub(1, 400))
                        end
                        return body
                    end
                end
            end
        end
        log:info("PlaybackFence: cometd snoop installed")
        return true
    end
    local function decodeCodes()
        if not self.decode then return "?/?" end
        local ok, st = pcall(function() return self.decode:status() end)
        if ok and type(st) == "table" then
            return tostring(st.decodeState) .. "/" ..
                tostring(st.audioState)
        end
        return "err"
    end
    local function mkConnect(original)
        return function(instance, ...)
            ringPush("CONNECT stream=" ..
                (instance.stream ~= nil and "set" or "nil") ..
                " hdr=" .. tostring(instance.header):sub(1, 44) ..
                " dec=" .. decodeCodes())
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
    -- V6: decode quiescence without the stream check (the Jive
    -- stream object is already gone on pause/stop disconnects).
    local function decodeNowIdle()
        if not self.decode then return false end
        local ok, status = pcall(function()
            return self.decode:status()
        end)
        if not ok or type(status) ~= "table" then return false end
        return status.decodeState == 0 and status.audioState == 0
    end
    -- Build 5: stopInternal is the kill vector (the fatal
    -- _streamDisconnect(nil, true) comes from it or the strm
    -- handlers). Traceback every call so the next kill names its
    -- invoker. Pure observer: never blocks, never alters.
    local function mkStopInternal(original)
        return function(instance, ...)
            local tb = "notrace"
            pcall(function()
                tb = tostring(debug.traceback("stopInternal", 2))
                    :gsub("\n", " | "):sub(1, 300)
            end)
            ringPush("STOPINTERNAL music=" ..
                tostring(self.musicActive) .. " tb=" .. tb)
            return original(instance, ...)
        end
    end
    local function mkDisconnect(original)
        return function(instance, reason, flush)
            ringPush("DISCONNECT reason=" .. tostring(reason) ..
                " flush=" .. tostring(flush) ..
                " stream=" ..
                (instance.stream ~= nil and "set" or "nil") ..
                " hdr=" .. tostring(instance.header):sub(1, 44) ..
                " dec=" .. decodeCodes() ..
                " music=" .. tostring(self.musicActive))
            local result = original(instance, reason, flush)
            if instance.stream == nil then
                local function recheck()
                    if instance.stream ~= nil then return end
                    if not self.musicActive then return end
                    local gv = (self.client
                        and self.client.snapshot) or {}
                    if gv.owner == 4 then
                        -- Build 8: granted. The HTTP stream is gone
                        -- but the decoder may play from buffer for
                        -- minutes (loopback slurp + FIN): never clear
                        -- here. Only notice stop-while-paused; the
                        -- grant lifecycle (releaseIdle + release ack)
                        -- owns the flag.
                        if self.pauseHeld ~= nil then
                            if decodeNowIdle() then
                                self.pauseHeld = nil
                                self.pauseEpochs = {}
                            else
                                self.after(5000, recheck)
                            end
                        end
                        return
                    end
                    if self.pauseHeld ~= nil then
                        -- A paused stream survives its Jive
                        -- disconnect; only a decode that went idle
                        -- (stop-while-paused) reaps it.
                        if decodeNowIdle() then
                            setActive(false, "pause-stop")
                            self:maybeRelease()
                        else
                            self.after(5000, recheck)
                        end
                        return
                    end
                    if not decodeNowIdle() then
                        -- Build 9: decode busy but no grant
                        -- observed yet (snapshot race after the
                        -- loopback slurp + FIN, or a live
                        -- preemption attempt): hold the fence and
                        -- recheck. Only a truly idle decoder ends
                        -- an ungranted latch.
                        self.after(5000, recheck)
                        return
                    end
                    setActive(false, "disconnect")
                    self:maybeRelease()
                end
                self.after(500, recheck)
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
                    "_streamDisconnect", mkDisconnect)
                and hooks:reinstall(playback,
                    "stopInternal", mkStopInternal) then
                self.wrappedConnect = playback._streamConnect
                self.wrappedDisconnect = playback._streamDisconnect
                self.wrappedHeaders = playback._streamHttpHeaders
                return true
            end
            return nil, "playback_readopt_failed"
        end
        if type(playback._streamConnect) ~= "function"
            or type(playback._streamDisconnect) ~= "function"
            or type(playback._streamHttpHeaders) ~= "function"
            or type(playback.stopInternal) ~= "function" then
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
        if not hooks:install(playback, "stopInternal", mkStopInternal) then
            return nil, "playback_stopinternal_wrap_failed"
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
            ringPush("RELEASE owner=4 yield=0 idle=true")
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

    -- V6 pause bridge. A true local pause is forwarded to the Base
    -- as a Bayeux /slim/request frame; the Base music path (not the
    -- legacy NowPlaying branch) drives strm-p/strm-u on the decoder.
    -- Fixed-shape manual JSON: no codec dependency, nothing
    -- attacker-controlled except the charset-validated player id.
    local function serializeCometd(frame)
        local req = frame.data.request
        -- Wire contract (np_lms_envelope_read): NUMERIC id and the
        -- response path nested under data; the legacy shape with a
        -- top-level response and string id draws a silent 400.
        return "[{\"channel\":\"/slim/request\",\"id\":"
            .. tostring(frame.id)
            .. ",\"data\":{\"request\":[\"" .. req[1]
            .. "\",[\"" .. req[2][1] .. "\"," .. tostring(req[2][2])
            .. "]],\"response\":\"" .. frame.response .. "\"}}]"
    end
    -- Test hook: the wire shape is a live contract (the Base reads
    -- data.response); Phase K pins it byte-for-byte.
    self.serializeCometd = serializeCometd
    local function makeCometdTransport()
        local RequestHttp = tryRequire("jive.net.RequestHttp")
        local SocketHttp = tryRequire("jive.net.SocketHttp")
        if not RequestHttp or not SocketHttp then return nil end
        return function(frame)
            local body = serializeCometd(frame)
            local sent, finished = false, false
            local request = RequestHttp(function(chunk, err)
                if finished then return end
                finished = true
                -- Build 9 diag (temporary): snapshot the Base
                -- reply so a silent strm-p refusal names
                -- itself. Stripped in the final build.
                pcall(function()
                    local f = io.open(
                        "/tmp/standalonebase/fence.cometd", "w")
                    if f then
                        f:write("id=", tostring(frame.id),
                            " err=", tostring(err), "\n")
                        if type(chunk) == "string" then
                            f:write(chunk:sub(1, 1024))
                        end
                        f:close()
                    end
                end)
                if err then
                    log:warn("PlaybackFence: cometd pause post failed")
                end
            end, "POST", "/cometd", {
                headers = { Connection = "close" },
                t_bodySource = function()
                    if not sent then sent = true return body end
                end,
            })
            request._sbFenceOurs = true
            local socket = SocketHttp(o.jnt, "127.0.0.1", 9000,
                "StandaloneFence")
            socket:fetch(request)
        end
    end
    -- The ONLY pause entry. snapshot = { paused=bool, streamId=num,
    -- playbackType="local"|"remote" }; the Player pause/unpause wraps
    -- below fill playbackType from instance:isLocal(). Transitions only:
    -- consecutive duplicates emit nothing. Never touches the SBOWN1
    -- client: no bridge writes while paused, fencing holds.
    function self:playerPause(snapshot)
        if not markerPresent() then return false, "passive" end
        if type(snapshot) ~= "table"
            or type(snapshot.streamId) ~= "number"
            or type(snapshot.paused) ~= "boolean" then
            return false, "bad_snapshot"
        end
        if snapshot.playbackType ~= "local" then
            return false, "remote_passthrough"
        end
        if not self.musicActive then
            return false, "not_active"
        end
        local want = snapshot.paused and "paused" or "resumed"
        if self.pauseEpochs[snapshot.streamId] == want then
            return false, "duplicate"
        end
        local player = Player:getLocalPlayer()
        local playerId = player and player.id
        if type(playerId) ~= "string" or playerId == ""
            or not playerId:match("^[0-9A-Za-z:]+$") then
            log:error("PlaybackFence: no local player id; pause refused")
            return false, "no_player_id"
        end
        if not self.cometd then
            self.cometd = makeCometdTransport()
            if not self.cometd then
                log:error("PlaybackFence: no cometd transport")
                return false, "no_transport"
            end
        end
        self.cometdSeq = self.cometdSeq + 1
        self.cometd({ channel = "/slim/request",
            id = self.cometdSeq,
            response = "/v6/pause",
            data = { request = { playerId,
                { "pause", snapshot.paused and 1 or 0 } } } })
        self.pauseEpochs[snapshot.streamId] = want
        if snapshot.paused then
            self.pauseHeld = snapshot.streamId
            log:info("PlaybackFence: pause forwarded")
        else
            if self.pauseHeld == snapshot.streamId then
                self.pauseHeld = nil
            end
            log:info("PlaybackFence: resume forwarded")
        end
        self:writeStatus()
        return true
    end

    local function localType(instance)
        if instance and type(instance.isLocal) == "function" then
            local ok, v = pcall(function()
                return instance:isLocal()
            end)
            if ok and v then return "local" end
        end
        return "remote"
    end
    local function localPlayerType()
        local ok, player = pcall(function()
            return Player:getLocalPlayer()
        end)
        if ok then return localType(player) end
        return "remote"
    end
    -- Live observability: Jive logs to /dev/console, so the tick and
    -- every pause transition publish a machine-readable status file.
    function self:statusSnapshot()
        local v = snapshot() or {}
        local epochs = 0
        for _ in pairs(self.pauseEpochs) do epochs = epochs + 1 end
        -- Debug dump (build 3): is the Jive world blind to
        -- server-driven streams? All pcall-guarded, never fails.
        local mode, paused, pbObj, pbStream, pbHeader, decS, decA
        pcall(function()
            local player = Player:getLocalPlayer()
            if player then
                if type(player.getPlayMode) == "function" then
                    mode = player:getPlayMode()
                end
                if type(player.isPaused) == "function" then
                    paused = player:isPaused()
                end
            end
        end)
        pcall(function()
            local pb = self.getPlayback and self.getPlayback()
            if pb then
                pbObj = tostring(pb):match(": ([0-9A-Fa-fx]+)") or "obj"
                pbStream = pb.stream ~= nil and "set" or "nil"
                if type(pb.header) == "string" then
                    pbHeader = pb.header:sub(1, 48)
                else
                    pbHeader = type(pb.header)
                end
            else
                pbObj = "nil"
            end
        end)
        pcall(function()
            if self.decode then
                local st = self.decode:status()
                if type(st) == "table" then
                    decS, decA = st.decodeState, st.audioState
                end
            end
        end)
        -- Build 10 diag (temporary): live radio state.
        local rOwns, rActive, rPaused, rPending, rTimer
        pcall(function()
            local rp = radioPlayer()
            if rp then
                rOwns = rp.ownsPlayback
                rActive = rp.playbackActive
                rPaused = rp.paused
                rPending = rp.pendingStation ~= nil
                rTimer = rp.reconnectTimer ~= nil
            end
        end)
        return {
            version = M.VERSION, build = M.BUILD,
            musicActive = self.musicActive, pauseHeld = self.pauseHeld,
            gated = gated(), client = self.client ~= nil,
            active = self.client and self.client.active,
            owner = v.owner, incarnation = v.incarnation,
            yield = v.yield, idle_admitted = v.idle_admitted,
            connected = v.connected, epochs = epochs,
            cometdSeq = self.cometdSeq, lastAction = self.lastAction,
            lastActionAt = self.lastActionAt, now = self.clock(),
            playerWrapped = self.playerWrapped,
            actionsWatched = self.actionsWatched,
            radioWrapped = self.radioWrapped,
            spotifyWrapped = self.spotifyWrapped,
            playbackWrapped = self.playbackWrapped,
            playerMode = mode, playerPaused = paused,
            pbObj = pbObj, pbStream = pbStream, pbHeader = pbHeader,
            decS = decS, decA = decA,
            grantIdleSince = self.grantIdleSince,
            radioOwnsPb = rOwns, radioActive = rActive,
            radioPaused = rPaused, radioPending = rPending,
            radioTimer = rTimer,
        }
    end
    function self:writeStatus(path)
        pcall(function()
            local f = assert(io.open(path or
                "/tmp/standalonebase/fence.status", "w"))
            local s = self:statusSnapshot()
            for _, k in ipairs({ "version", "build", "musicActive",
                "pauseHeld", "gated", "client", "active", "owner",
                "incarnation", "yield", "idle_admitted", "connected",
                "epochs", "cometdSeq", "lastAction", "lastActionAt",
                "now", "playerWrapped", "actionsWatched", "playerMode",
                "playerPaused", "pbObj", "pbStream", "pbHeader",
                "decS", "decA", "radioWrapped", "spotifyWrapped",
                "playbackWrapped", "grantIdleSince", "radioOwnsPb",
                "radioActive", "radioPaused", "radioPending",
                "radioTimer" }) do
                f:write(k, "=", tostring(s[k]), "\n")
            end
            f:close()
        end)
    end
    -- The Jive "pause"/"play"/"stop" actions are consumed by
    -- Radio's global listeners (priority -100) before any music
    -- routing could see them, so the fence watches them itself at
    -- -50: log every arrival, forward pause/play while gated, and
    -- NEVER consume (radio and other owners keep their behavior).
    local function watchActions()
        if self.actionsWatched then return true end
        if not Framework
            or type(Framework.addActionListener) ~= "function" then
            return nil, "no_action_api"
        end
        local function onAction(action)
            return function()
                -- Silenced layers observe only: a single live
                -- instance emits pause frames.
                if not self.timerOk then return end
                self.lastAction = action
                self.lastActionAt = self.clock()
                if action == "pause" and gated() then
                    self:playerPause({ paused = true,
                        streamId = self.musicSince,
                        playbackType = localPlayerType() })
                elseif action == "play" and self.pauseHeld ~= nil then
                    self:playerPause({ paused = false,
                        streamId = self.pauseHeld,
                        playbackType = localPlayerType() })
                end
                self:writeStatus()
            end
        end
        -- Priority -50 runs before Radio's consuming -100 media
        -- listeners (Framework sorts global listeners by
        -- abs(priority) ascending): pause/play forward while gated
        -- even though Radio consumes the event afterwards. Never
        -- consumes itself.
        for _, action in ipairs({ "pause", "play", "stop" }) do
            local ok = pcall(function()
                Framework:addActionListener(action, self,
                    onAction(action), -50)
            end)
            if not ok then
                return nil, "action_watch_failed"
            end
        end
        self.actionsWatched = true
        log:info("PlaybackFence: media actions watched")
        return true
    end
    local function wrapPlayerPause()
        if self.playerWrapped then return true end
        -- Player is the jive.slim.Player module table: LocalPlayer
        -- overrides pause but funnels through Player.pause after its
        -- timeout guard, and unpause is not overridden. One wrap
        -- point covers both.
        if type(Player.pause) ~= "function"
            or type(Player.unpause) ~= "function" then
            log:error("PlaybackFence: unsupported Player revision")
            return nil, "unsupported_player_revision"
        end
        local hooks = self.hooks
        if not hooks:install(Player, "pause",
            function(original)
                return function(instance, useBackgroundRequest)
                    -- Build 10 diag (temporary).
                    ringPush("PPAUSE gated=" .. tostring(gated()))
                    if self.timerOk and gated() then
                        self:playerPause({ paused = true,
                            streamId = self.musicSince,
                            playbackType = localType(instance) })
                    end
                    return original(instance, useBackgroundRequest)
                end
            end) then
            return nil, "player_pause_wrap_failed"
        end
        if not hooks:install(Player, "unpause",
            function(original)
                return function(instance)
                    if self.timerOk and self.pauseHeld ~= nil then
                        self:playerPause({ paused = false,
                            streamId = self.pauseHeld,
                            playbackType = localType(instance) })
                    end
                    return original(instance)
                end
            end) then
            return nil, "player_unpause_wrap_failed"
        end
        self.playerWrapped = true
        log:info("PlaybackFence: player pause wrappers installed")
        return true
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
                self.grantIdleSince = nil
                setActive(true, "grant-cb")
            end,
            revoke = function()
                -- A flap drops the native grant while the decoder keeps
                -- playing. updateGrantFlag clears only on owner-loss
                -- WITH decoder idle: keep fencing, reconcile when idle.
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

    -- Build 8: the flag tracks the GRANT, not the HTTP stream
    -- object (Jive FINs a slurped loopback track in seconds and
    -- decodes from buffer for minutes). Latch the moment owner==4
    -- is observed; clear only on owner-loss with decoder idle
    -- (flaps hold).
    local function updateGrantFlag(v)
        if v.owner == 4 and not self.musicActive then
            setActive(true, "grant")
        elseif v.owner ~= 4 and self.musicActive and decoderIdle() then
            setActive(false, "grant-lost")
        end
        if v.owner ~= 4 or v.yield ~= 0 or not decoderIdle() then
            self.grantIdleSince = nil
        end
    end
    -- Build 8: release a granted-but-idle decoder (natural end,
    -- explicit stop, stop-while-paused). Decodes that are busy or
    -- paused never reach the release; the flag clears on the
    -- release acknowledgement via updateGrantFlag.
    local function releaseIdleGrant(v)
        if v.owner ~= 4 or v.yield ~= 0 or not decoderIdle() then
            return
        end
        if not self.grantIdleSince then
            self.grantIdleSince = self.clock()
            return
        end
        if self.clock() - self.grantIdleSince <= 3000 then return end
        self.grantIdleSince = nil
        ringPush("RELEASE-IDLE owner=4 idle>3s")
        self.client:command("release", 7, "0", 0,
            function(reply, err)
                if reply then
                    log:info("PlaybackFence: idle release accepted")
                else
                    log:warn("PlaybackFence: idle release refused (",
                        tostring(err), ")")
                end
            end)
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
        -- Latch first: the flag follows the grant within one tick.
        updateGrantFlag(v)
        releaseIdleGrant(v)
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
        wrapRetry("playerWrapped", wrapPlayerPause, "player")
        wrapRetry("actionsWatched", watchActions, "actions")
        wrapRetry("snoopWrapped", wrapSnoop, "snoop")
        observeForeignOwnership()
        -- Local backstop: reap a stale active flag when the decoder is
        -- provably idle (covers missed disconnect observations) or has
        -- moved on to a non-Music stream (alarm tone, explicit stop).
        local playback = self.getPlayback()
        -- V6: a held pause is not a stale flag; the disconnect recheck
        -- owns stop-while-paused teardown via decode quiescence.
        -- Build 8: a granted flag is never stale (buffered decode
        -- outlives the HTTP stream); releaseIdle owns its end.
        local gv = snapshot() or {}
        -- Build 9: only reap an ungranted stream-nil latch once
        -- the decoder is provably idle (buffered decode outlives
        -- the HTTP stream); a latch whose stream moved on to
        -- non-Music, or whose native ownership moved to another
        -- app, still reaps (a new owner holds the decoder).
        local movedOn = playback.stream ~= nil
            and not headerIsMusic(playback)
        local foreignOwns = gv.owner ~= nil and gv.owner ~= 0
            and gv.owner ~= 4
        if self.musicActive and playback and self.pauseHeld == nil
            and gv.owner ~= 4
            and ((playback.stream == nil and decodeNowIdle())
                or movedOn or foreignOwns) then
            self.idleSamples = self.idleSamples + 1
            if self.idleSamples >= 2 then
                setActive(false, "backstop")
                self:maybeRelease()
            end
        else
            self.idleSamples = 0
        end
        self:pollAdapter()
        self:writeStatus()
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
    ok, err = wrapRetry("playerWrapped", wrapPlayerPause, "player")
    if not ok then
        log:error("PlaybackFence: install refused (", tostring(err), ")")
        return nil, err
    end
    -- Action watching is observability + belt-and-braces forwarding;
    -- its absence must not refuse the install (tick retries it).
    wrapRetry("actionsWatched", watchActions, "actions")
    wrapRadioCallbacks()
    ok, err = wrapPlayback()
    if ok == nil then
        log:error("PlaybackFence: install refused (", tostring(err), ")")
        return nil, err
    end
    -- Live-proof stamp: Jive logs to /dev/console (not file
    -- readable), so the installed version + incarnation time land in a
    -- tiny status file next to the supervisor pidfiles. Best effort;
    -- a missing directory simply skips the stamp (host tests).
    function self:stampVersion(path)
        local ok = pcall(function()
            local f = assert(io.open(path or
                "/tmp/standalonebase/fence.version", "w"))
            f:write("version=" .. tostring(M.VERSION)
                .. " build=" .. tostring(M.BUILD)
                .. " installed_at=" .. tostring(self.clock()) .. "\n")
            f:close()
        end)
        return ok
    end
    self:stampVersion()
    self:writeStatus()
    self.timerOk = true
    self.after(5000, tick)
    log:info("PlaybackFence: installed version=", M.VERSION,
        " marker=", markerPresent() and "armed" or "passive")
    installed = self
    return self
end

return M
