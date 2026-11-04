"""Run qualified stock Lua through instrumented APIs; never invoke firmware."""
import importlib.util
from pathlib import Path
import sys
from lupa.lua51 import LuaRuntime

root, stock = map(Path, sys.argv[1:3])
spec = importlib.util.spec_from_file_location(
    "lifecycle_patch", root / "packaging/fab4-reliability/patch_tinysc_lifecycle.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
raw = stock.read_bytes()
modified = patch.patch_applet(raw)
try:
    patch.patch_applet(raw + b"\n")
except ValueError:
    pass
else:
    raise AssertionError("unknown firmware accepted")

setup = '''
blocked=false; calls={}; suppressions=0
local function unexpected() error("unexpected UI/filesystem operation") end
local fakeio={open=function(path,mode)
    assert(path=="/var/run/standalonebase/tinysc.latch" and mode=="r")
    if blocked then return {close=function() end} end
end}
local fakeos={execute=function(cmd) calls[#calls+1]=cmd;return 0 end}
package.loaded.io=fakeio;package.loaded.os=fakeos
package.loaded["loop.simple"]={class=function() end}
package.loaded["jive.ui.Framework"]={constants=function(m) m.EVENT_UNUSED=42 end}
package.loaded["jive.utils.table"]=table
package.loaded["jive.utils.string"]=string
for _,name in ipairs({"json","jive.utils.squeezeos","jive.utils.debug","lfs",
    "jive.Applet","jive.ui.Icon","jive.ui.Label","jive.ui.Popup",
    "jive.ui.SimpleMenu","jive.ui.Textarea","jive.ui.Task","jive.ui.Timer",
    "jive.ui.Window","jive.slim.SlimServer"}) do
    package.loaded[name]=setmetatable({}, {__call=unexpected})
end
'''


def load(source):
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(setup)
    lua.execute('assert(loadstring(...))("fixture.SC")', source.decode())
    lua.execute('''
sc=package.loaded["fixture.SC"]
sc.log={info=function() suppressions=suppressions+1 end,
        debug=function() end,warn=function() end,error=function() end}
fake={mountedDriveCheck=function() return {sda1=true} end,
      mediaType=function() return "USB" end,
      readSCPrefsFile=function() return {mountpath="/media/sda1"} end,
      restartServer=function() calls[#calls+1]="restart" end}
''')
    return lua


original = load(raw)
updated = load(modified)
for lua in (original, updated):
    lua.execute("sc.squeezecenterStartupCheck(fake)")
    assert list(lua.globals().calls.values()) == ["restart"]
updated.execute('''
blocked=true;calls={}
for repeat_event=1,2 do
    assert(sc.squeezecenterStartupCheck(fake)==42)
    assert(sc.switchServerToDifferentMedia(fake,"sda1")==42)
    assert(sc.udevEventHandler(fake,nil,{ACTION="add",DEVTYPE="partition"})==42)
    assert(sc._wipeRescan(fake)==42)
    assert(sc._wipeTimer(fake,"sda1")==42)
    assert(sc._startServerWindow(fake)==42)
    assert(sc.restartServer(fake,true)==42)
    assert(sc._writeSCPrefsFile(fake,"sda1")==42)
    assert(sc._squeezecenterAction(fake,nil,nil,nil,nil,"restart",true)==42)
end
assert(#calls==0)
sc._squeezecenterAction(fake,nil,nil,nil,nil,"stop",true)
assert(#calls==1 and calls[1]=="/etc/init.d/squeezecenter stop")
assert(suppressions==19)
''')
print("Qualified firmware overlay: stock startup retained when off; 19 guarded startup/USB/late/wipe/prefs/service callbacks passed")
