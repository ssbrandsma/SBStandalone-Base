-- SB-WP4A6-RADIOSTOP (diagnostic, not production): remote radio stop
-- for when the Touch UI is unusable (CometD flap). Calls the real
-- StreamPlayer:stop() (ownsPlayback=false, reconnect cancelled, decoder
-- stopped, STOPPED notified). Idempotent; safe to trigger twice.
local M = {}
function M.install()
    local lok, logModule = pcall(require, "jive.utils.log")
    local log = (lok and logModule.logger("StandaloneBase"))
        or { info = function() end, warn = function() end }
    local mgr = appletManager
    if not mgr or type(mgr.getAppletInstance) ~= "function" then
        return nil, "no_manager"
    end
    local ok, radio = pcall(function()
        return mgr:getAppletInstance("StandaloneRadio")
    end)
    if not ok or type(radio) ~= "table" then
        return nil, "no_radio"
    end
    local sp = radio.streamPlayer
    if type(sp) ~= "table" or type(sp.stop) ~= "function" then
        return nil, "no_player"
    end
    local sok, serr = pcall(function() sp:stop() end)
    log:warn("SB-RADIOSTOP stopped=", tostring(sok),
        " err=", tostring(serr))
    if sok then return true end
    return nil, serr
end
return M
