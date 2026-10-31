local root=arg[1]
local P=dofile(root.."/applet/Platform.lua")
assert(P.detect("Hardware : Logitech Fab4 Board\nCPU architecture: 6TEJ\n").name=="fab4")
for _,b in ipairs({"Baby","Logitech MX25 Baby Board","Logitech Baby Board"}) do
 assert(P.detect("Hardware : "..b.."\n").name=="baby")
end
assert(P.detect("Processor : ARMv6\nHardware : Other\n").name=="unknown")
assert(not P.detect("Hardware : Logitech Fab4 Board\n").extended_storage)
print("Platform identity tests passed")

local function fixture(path)
 if path=="/proc/net/tcp" then return " 0: 00000000:2328 00000000:0000 0A\n 1: 00000000:0D9B 00000000:0000 01\n" end
 if path=="/proc/net/udp6" then return " 0: 00000000000000000000000000000000:0D9B 00000000000000000000000000000000:0000 07\n" end
 return ""
end
local conflicts=P.portConflicts(fixture)
assert(#conflicts==2 and conflicts[1]=="tcp:9000" and conflicts[2]=="udp6:3483")
assert(#P.portConflicts(function() return "" end)==0)
