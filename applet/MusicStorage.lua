-- WP6a opt-in adapter seam. Not loaded by applet startup; no menus/services
-- registered here. bridge is an injected nonblocking native ingress, never a
-- shell command or MediaManager/SqueezeCenter action. Firmware still mounts.
local M = {}
function M.start(options)
    if not options or options.enabled ~= true or options.platform ~= "fab4" then return nil end
    local bridge, listen = options.bridge, options.addUeventListener
    if not bridge or type(bridge.hint) ~= "function" or type(bridge.state) ~= "function" or type(listen) ~= "function" then return nil end
    local self = { active = true, sequence = 0, kernelSequence = 0, bridge = bridge }
    -- Hints contain no trusted filesystem path/identity. Native reconciliation
    -- reads OS mount state on its worker. No filesystem access in a UI callback.
    function self:changed(msg)
        if not self.active then return false end
        if msg and (msg.SUBSYSTEM ~= "block" or (msg.ACTION ~= "add" and msg.ACTION ~= "remove" and msg.ACTION ~= "change")) then return false end
        local kernelSequence = msg and tonumber(msg.SEQNUM)
        if kernelSequence and kernelSequence > 0 and kernelSequence <= 9007199254740991 and kernelSequence % 1 == 0 then
            if kernelSequence <= self.kernelSequence then return false end
            self.kernelSequence = kernelSequence
        end
        self.sequence = self.sequence + 1
        -- Removal hint must revoke matching live generations before asynchronous
        -- discovery. The bridge must not delay it behind a validation job.
        local device = msg and type(msg.DEVPATH)=="string" and msg.DEVPATH:match("([^/]+)$") or ""
        return self.bridge:hint(self.sequence, msg and msg.ACTION or "reconcile", device)
    end
    function self:state() if self.active then return self.bridge:state() end end
    function self:stop() self.active = false end
    listen("", function(_, msg) self:changed(msg) end)
    self:changed(nil) -- one startup discovery, no permanent timer/polling
    return self
end
return M
