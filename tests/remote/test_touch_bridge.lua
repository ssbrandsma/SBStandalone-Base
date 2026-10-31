local root=arg[1]
local events={}
local board="fab4"
package.preload["applets.StandaloneBase.Platform"]=function() return {detect=function() return {name=board} end} end
package.preload["loop.simple"]=function() return {class=function() end} end
local fw={getScreenSize=function() return 480,272 end,getTicks=function() return 100 end,
 pushEvent=function(self,e) events[#events+1]=e end,
 constants=function(m) for _,n in ipairs({"DOWN","UP","PRESS","HOLD","DRAG"}) do m["EVENT_MOUSE_"..n]=n end end}
package.preload["jive.ui.Framework"]=function() return fw end
package.preload["jive.ui.Event"]=function() return {new=function(self,t,x,y,f,w,p) assert(type(w)=="number" and type(p)=="number"); return {t=t,x=x,y=y,f=f} end} end
for _,n in ipairs({"Timer","Surface"}) do package.preload["jive.ui."..n]=function() return {} end end
for _,n in ipairs({"SocketHttp","RequestHttp"}) do package.preload["jive.net."..n]=function() return {} end end
package.preload.json=function() return {} end
local realio=io;io={open=function() return nil end}
local bridge=assert(loadfile(root.."/applet/RemoteBridge.lua"))("touch_test")
io=realio
assert(bridge.require==nil)
local object={}
bridge.response(object,"touch_479_271\tdown\n@protected\t0\n")
bridge.response(object,"touch_479_271\ttap\n")
assert(#events==3 and events[1].t=="DOWN" and events[2].t=="PRESS" and events[3].t=="UP")
assert(events[1].x==479 and events[1].y==271 and events[1].f==1)
bridge.response(object,"touch_480_271\tdown\ntouch_0_272\tdown\n")
assert(#events==3)
bridge.response(object,"touch_10_20\tdrag\ntouch_10_20\thold\ntouch_10_20\tup\n")
assert(#events==6 and events[4].t=="DRAG" and events[5].t=="HOLD" and events[6].t=="UP")
object.firmwareLock=true;bridge.response(object,"touch_1_1\tdown\n");assert(#events==6)
print("Fab4 touch translation and restricted-module tests passed")
