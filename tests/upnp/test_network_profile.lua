local profile=assert(loadfile(arg[1].."/applet/NetworkProfile.lua"))()
assert(profile.allowed("baby","7.7.3 r16676",false))
assert(profile.allowed("baby","9.0.2 r17160",false))
assert(profile.allowed("fab4","7.7.3 r16676",true))
assert(not profile.allowed("fab4","7.7.3 r16676",false))
for _,firmware in ipairs({"9.0.2","9.0.2 r17157","9.0.1 r17084","7.7.3 r166760","unknown",""}) do
 assert(not profile.allowed("baby",firmware,true))
end
assert(not profile.allowed("fab4","9.0.2 r17160",true))
assert(not profile.allowed("unknown","7.7.3 r16676",true))
print("Network profile: exact firmware/board admission and Touch arbitration passed")

local contract={schema="SB_NETWORK_MUSIC_1",version="0.7.5",target="baby",resource_wire=3,upnp_ipc=1,ownership_wire=1,queue_limit=256,renderer_default=false}
assert(profile.compatible(contract,"baby"))
assert(not profile.compatible(contract,"fab4"))
contract.version="0.7.4-rc1";assert(not profile.compatible(contract,"baby"))
contract.version="0.7.5";contract.resource_wire=2;assert(not profile.compatible(contract,"baby"))
assert(not profile.compatible(nil,"baby"))
