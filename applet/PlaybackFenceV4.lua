-- PlaybackFenceV4: silencer for the superseded V2 instance.
--
-- V2's tick loops forever: its playback re-verify can never succeed now
-- that V3 wraps outermost, and its adapter predates the Content-Length
-- fix (every hello is bridge-rejected). V2's class wraps stay in place
-- as consistent inner layers; only its tick, adapter and music gate are
-- retired. V3 (registered and admitted) is untouched.
local M = {}
M.VERSION = 4
function M.install()
    local logModule = require("jive.utils.log")
    local log = logModule.logger("StandaloneBase")
    local ok, V2 = pcall(require, "applets.StandaloneBase.PlaybackFenceV2")
    if not ok or type(V2) ~= "table"
        or type(V2.install) ~= "function" then
        log:warn("PlaybackFenceV4: V2 module absent; nothing to silence")
        return nil, "v2_absent"
    end
    local ok2, inst = pcall(function() return V2.install() end)
    if not ok2 or type(inst) ~= "table" then
        log:warn("PlaybackFenceV4: V2 instance unavailable")
        return nil, "v2_no_instance"
    end
    -- Stop the tick (it re-arms only when timerOk is set), freeze the
    -- music flag off (its observers would otherwise re-latch it through
    -- the wrapper chain), and drop the dead adapter.
    inst.timerOk = false
    inst.joining = false
    inst.musicActive = false
    inst.setActive = function() end
    if inst.client and type(inst.client.shutdown) == "function" then
        pcall(function() inst.client:shutdown() end)
    end
    inst.client = nil
    log:info("PlaybackFenceV4: V2 silenced (tick off, gate frozen, wraps kept)")
    return true
end
return M
