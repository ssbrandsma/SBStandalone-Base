-- Board identity, never instruction-set inference. No writes or subprocesses.
local M = {}
local function read(path)
    local f=io.open(path,"r"); if not f then return "" end
    local s=f:read("*a"); f:close(); return s
end
function M.detect(cpu)
    cpu=cpu or read("/proc/cpuinfo")
    local board=cpu:match("Hardware%s*:%s*([^\n\r]+)")
    local name="unknown"
    if board=="Baby" or board=="Logitech MX25 Baby Board" or board=="Logitech Baby Board" then name="baby"
    elseif board=="Logitech Fab4 Board" then name="fab4" end
    return {name=name, cpu_arch=cpu:match("CPU architecture%s*:%s*([^\n\r]+)"),
        memory_total=tonumber(read("/proc/meminfo"):match("MemTotal:%s*(%d+)")),
        has_usb=name=="fab4", has_sd=name=="fab4",
        display_width=name=="fab4" and 480 or 320,
        display_height=name=="fab4" and 272 or 240,
        display_bpp=tonumber(read("/sys/class/graphics/fb0/bits_per_pixel")),
        storage_root="/mnt/storage/standalonebase", extended_storage=name=="baby"}
end
-- /proc sockets avoids depending on firmware-specific netstat switches.
function M.packageMatches(name)
    local target=read("/usr/share/jive/applets/StandaloneBase/build-target"):match("(%S+)")
    return not target or target==name
end
function M.portConflicts(reader)
    reader=reader or read
    local conflicts={}
    for _,proto in ipairs({"tcp","tcp6","udp","udp6"}) do
        for line in reader("/proc/net/"..proto):gmatch("[^\n]+") do
            local port,state=line:match("^%s*%d+:%s+%x+:(%x+)%s+%S+%s+(%x+)")
            if port and (proto:sub(1,3)=="udp" or state=="0A") then
                local n=tonumber(port,16)
                if n==9000 or n==3483 then conflicts[#conflicts+1]=proto..":"..n end
            end
        end
    end
    return conflicts
end
return M
