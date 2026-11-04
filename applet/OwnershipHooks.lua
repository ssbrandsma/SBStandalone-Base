-- Exact wrapper ownership, independent of decoder or metadata leases.
local M = {}
function M.new()
    local self = { records = {}, live = true }
    function self:install(target, key, make)
        if not self.live or type(target[key]) ~= "function" then return false end
        for _, r in ipairs(self.records) do
            if r.target == target and r.key == key then return target[key] == r.wrapper end
        end
        local original = target[key]
        local wrapper = make(original)
        if type(wrapper) ~= "function" then return false end
        self.records[#self.records + 1] = {target=target, key=key, original=original, wrapper=wrapper}
        target[key] = wrapper
        return true
    end
    function self:record(target,key,original)
        if not self.live or type(original)~="function" or type(target[key])~="function" then return false end
        for _,r in ipairs(self.records) do if r.target==target and r.key==key then return false end end
        self.records[#self.records+1]={target=target,key=key,original=original,wrapper=target[key]}
        return true
    end
    function self:restore()
        -- Verify every slot before changing any. Never clobber a newer owner.
        for _, r in ipairs(self.records) do
            if r.target[r.key] ~= r.wrapper and r.target[r.key] ~= r.original then return false end
        end
        for i=#self.records,1,-1 do
            local r=self.records[i]; r.target[r.key]=r.original
        end
        self.records={}; self.live=false
        return true
    end
    return self
end
return M
