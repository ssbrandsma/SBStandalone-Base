-- Qualification-only application fence. Never acknowledges an external release.
-- Install before applet registration; refuse a legacy instance/hook baseline.
local M = {}
local names={"applets.StandaloneRadio.StreamPlayer", "applets.SpotifyConnect.SpotifyService",
             "applets.SpotifyConnect.SpotifyPlayback"}
function M.install(o)
    o=o or {};local loaded=o.loaded or package.loaded;local load=o.load or require
    for _,name in ipairs(names) do if loaded[name] then return nil,"legacy_module_already_loaded" end end
    if loaded["applets.QobuzConnect.QobuzPlayback"] then return nil,"qobuz_loaded" end
    local radio,service,spotify=load(names[1]),load(names[2]),load(names[3])
    if type(radio.new)~="function" or type(service.init)~="function" or
       type(service.ownershipFence)~="function" or type(spotify.init)~="function" or
       type(spotify.configureOwnership)~="function" then return nil,"unsupported_application_revision" end
    local self={radios={},services={},players={},valid=true}
    local gate={epoch=1,canStart=function() return false end,request=function() return false end}
    local originalRadio,originalService,originalSpotify=radio.new,service.init,spotify.init
    local newRadio,newService,newSpotify
    newRadio=function(...)
        local instance=originalRadio(...)
        if not instance or type(instance.configureOwnership)~="function" or
           not instance:configureOwnership(gate) or not instance:ownershipFence() then
            self.valid=false;error("Music idle fence: Radio baseline refused")
        end
        self.radios[#self.radios+1]=instance
        return instance
    end
    newService=function(instance,...)
        originalService(instance,...)
        if not instance:ownershipFence() then self.valid=false;error("Music idle fence: Spotify fence refused") end
        self.services[#self.services+1]=instance
    end
    newSpotify=function(instance,...)
        originalSpotify(instance,...)
        if not instance:configureOwnership(gate) then self.valid=false;error("Music idle fence: Spotify baseline refused") end
        self.players[#self.players+1]=instance
    end
    radio.new,service.init,spotify.init=newRadio,newService,newSpotify
    function self:quiet()
        if not self.valid or radio.new~=newRadio or service.init~=newService or spotify.init~=newSpotify then return false end
        for _,r in ipairs(self.radios) do
            if r.ownership~=gate or r.ownsPlayback or r.playbackActive or r.desiredStation or r.pendingStation or
               r.reconnectTimer or r.watchdogTimer or r.audioRecoveryTimer or r.powerKeepaliveTimer or r.hookedPlayback then return false end
        end
        for _,s in ipairs(self.services) do
            if not s.ownershipFenced or s.desiredRunning or s.startTimer or s.stopTimer or s.healthTimer or
               s.lifecycleState~="stopped" then return false end
        end
        for _,p in ipairs(self.players) do if p.ownership~=gate or p.playback then return false end end
        return true
    end
    -- Keep fences latched for this Jive incarnation. Restoring application
    -- behavior requires verified Music release and the qualified recovery path.
    function self:shutdown() self.valid=false end
    return self
end
return M
