-- Opt-in physical adapter seam. NOT required by any startup path.
-- Applications must supply real restart fencing, producer/hook cleanup and a
-- positive decoder/output probe. Metadata leases are deliberately absent.
local M = {}

local function identity(value)
    return type(value) == "string" and #value <= 20 and value:match("^[1-9][0-9]*$") ~= nil
        and (#value < 20 or value <= "18446744073709551615")
end

local function greater(a, b)
    return #a > #b or (#a == #b and a > b)
end

function M.new(options)
    if not options or options.enabled ~= true or not identity(options.incarnation) then return nil end
    if options.owner ~= "radio" and options.owner ~= "spotify" and options.owner ~= "qobuz" and options.owner ~= "music" then return nil end
    for _, name in ipairs({"clock", "send", "fence", "stopProducer", "releaseHooks", "decoderQuiescent"}) do
        if type(options[name]) ~= "function" then return nil end
    end
    local self = { active = true, serial = 0, pending = nil, grant = nil, lastGeneration = "0", lastRequest = "0" }

    local function live(serial)
        return self.active and self.pending and self.serial == serial
            and options.clock() < self.pending.deadline
    end

    local function finish(serial, accepted)
        if not self.active or not self.pending or self.serial ~= serial then return false end
        local pending = self.pending
        accepted = accepted == true and options.clock() < pending.deadline
            and pending.producer == true and pending.hooks == true and pending.decoder == true
        self.pending = nil
        -- The injected bridge must retain/retry bounded IPC delivery; this
        -- helper never acknowledges decoder ownership locally.
        options.send({ owner = options.owner, incarnation = options.incarnation,
            request_id = pending.request_id, generation = pending.generation,
            connection = pending.connection, accepted = accepted,
            producer_stopped = pending.producer == true,
            hooks_released = pending.hooks == true, decoder_quiescent = pending.decoder == true })
        return true
    end

    local function invoke(serial, callback, nextStage)
        local used = false
        local ok = pcall(callback, function(confirmed)
            if used or not live(serial) then return false end
            used = true
            if confirmed ~= true then return finish(serial, false) end
            nextStage()
            return true
        end)
        if not ok then finish(serial, false) end
    end

    function self:request(request)
        if not self.active or self.pending or type(request) ~= "table"
            or not identity(request.request_id) or not identity(request.generation)
            or not identity(request.connection) or type(request.deadline) ~= "number"
            or request.deadline ~= request.deadline then return false end
        if not self.grant or self.grant.generation ~= request.generation or self.grant.connection ~= request.connection
            or not greater(request.request_id, self.lastRequest) or self.serial >= 9007199254740991 then return false end
        local now = options.clock()
        if request.deadline <= now or request.deadline - now > 10000 then return false end
        self.serial = self.serial + 1
        self.lastRequest = request.request_id
        local serial = self.serial
        self.grant = nil -- prevent retries/remote starts while yielding
        self.pending = { request_id = request.request_id, generation = request.generation,
            connection = request.connection, deadline = request.deadline }
        local ok, fenced = pcall(options.fence)
        if not ok or fenced ~= true then finish(serial, false); return true end
        invoke(serial, options.stopProducer, function()
            self.pending.producer = true
            invoke(serial, options.releaseHooks, function()
                self.pending.hooks = true
                invoke(serial, options.decoderQuiescent, function()
                    self.pending.decoder = true
                    finish(serial, true)
                end)
            end)
        end)
        return true
    end

    function self:tick()
        if self.pending and options.clock() >= self.pending.deadline then finish(self.serial, false) end
    end

    function self:cancel(request_id)
        if not self.pending or self.pending.request_id ~= request_id then return false end
        finish(self.serial, false)
        self.serial = self.serial + 1
        return true
    end

    -- Only a trusted native bridge may deliver an acknowledged fresh grant.
    function self:authorize(generation, connection)
        if not self.active or self.pending or not identity(generation) or not identity(connection)
            or not greater(generation, self.lastGeneration) then return false end
        self.lastGeneration = generation
        self.grant = { generation = generation, connection = connection }
        return true
    end

    function self:canStart(generation, connection)
        return self.active and not self.pending and self.grant ~= nil
            and self.grant.generation == generation and self.grant.connection == connection
    end

    function self:invalidate()
        if self.pending then finish(self.serial, false) end
        self.grant = nil; self.serial = self.serial + 1
    end

    function self:stop()
        if self.pending then finish(self.serial, false) end
        self.active = false; self.grant = nil; self.serial = self.serial + 1
    end
    return self
end

return M
