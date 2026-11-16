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

-- Execute the real applet under Lua 5.1's strict module environment. Plain
-- loadfile syntax checks cannot catch globals hidden by module(...).
local function startup(board)
 local noop=function()end
 local logged={}
 local logger={info=noop,warn=noop,error=function(_,...) logged[#logged+1]=table.concat({...}," ") end}
 local function file(content)return {read=function()return content or ""end,close=noop,write=noop}end
 local fakeio={open=function(path,mode)
  if mode=="w" then return file() end
  if path=="/etc/squeezeos.version" then return file(board=="baby" and "9.0.2 r17160" or "7.7.3 r16676") end
  if path:find("/usr/share/jive/applets/StandaloneBase/",1,true)==1 then return file() end
  if path:find("/tmp/standalonebase/",1,true)==1 then return file("1") end
  if path=="/mnt/storage/standalonebase/config.json" then return file("{}") end
 end}
 local modules={
  ["loop.simple"]={class=noop},["jive.Applet"]={},
  ["jive.ui.Framework"]={constants=noop},
  ["jive.utils.log"]={logger=function()return logger end},
  ["jive.slim.SlimServer"]={iterate=function()return function()end end},
  ["applets.StandaloneBase.Platform"]={detect=function()return {name=board} end,packageMatches=function()return true end,portConflicts=function()return {"9000"} end},
  ["applets.StandaloneBase.StorageManager"]={new=function()return {}end},
  ["applets.StandaloneBase.NetworkProfile"]={allowed=profile.allowed,runtime=function()return true end},
  ["applets.StandaloneBase.PlaybackFenceV6"]={install=function()return nil,"fixture"end},
  ["applets.StandaloneBase.InstallerCompat"]={apply=noop},
  ["applets.StandaloneBase.HttpTrace"]={apply=noop},
  ["applets.StandaloneBase.NetworkMusic"]={new=function()return {}end},
  ["applets.StandaloneBase.TimeSync"]={new=function()return {start=noop}end},
  ["applets.StandaloneBase.RemoteBridge"]=function()return {init=noop}end,
  ["applets.StandaloneBase.Vpn"]={new=function()error("VPN_FACTORY_REACHED")end},
 }
 local env=setmetatable({io=fakeio,os={execute=function()return 0 end,remove=noop},
  require=function(name)return modules[name] or {}end},{__index=_G})
 local chunk=assert(loadfile(arg[1].."/applet/StandaloneBaseApplet.lua"))
 setfenv(chunk,env)
 local name="standalone_startup_test_"..board
 chunk(name)
 local ok,message=pcall(package.loaded[name].init,{})
 return ok,tostring(message),logged
end
local ok,message=startup("baby")
assert(not ok and message:find("VPN_FACTORY_REACHED",1,true),message)
local touchOk,touchMessage,logged=startup("fab4")
assert(touchOk,touchMessage)
assert(logged[1] and logged[1]:find("9000",1,true),"Touch conflict diagnostics did not execute")
print("Real applet strict module: dynamic network/VPN loading and Touch conflict diagnostics passed")
