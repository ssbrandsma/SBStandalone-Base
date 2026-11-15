local root=assert(arg[1])
local requests,timers,windows,nodes={},{},{},{localUSB={text="USB Music",node="myMusic"}}
local replies={}
local savedOpen=io.open
io.open=function(path,...) if path=="/var/run/sb-upnp/token" then return {read=function()return string.rep("a",64)end,close=function()end} end return savedOpen(path,...) end
package.preload["json"]=function()return {encode=function(value)return value end,decode=function(value)assert(replies[value],value);return replies[value]end}end
package.preload["jive.ui.Timer"]=function()return function(_,callback)return {start=function(self)timers[#timers+1]=self;self.callback=callback end,stop=function(self)self.stopped=true end}end end
package.preload["jive.ui.Framework"]=function()return {constants={EVENT_WINDOW_POP=1}}end
package.preload["jive.ui.Window"]=function()return function(_,title)local w={title=title};function w:addWidget(menu)self.menu=menu end;function w:addListener(_,cb)self.pop=cb end;return w end end
package.preload["jive.ui.SimpleMenu"]=function()return function()local m={items={}};function m:setItems(items)self.items=items end;function m:addItem(item)self.items[#self.items+1]=item end;return m end end
package.preload["jive.net.RequestHttp"]=function()return function(sink,method,path,options)return {sink=sink,method=method,path=path,options=options}end end
package.preload["jive.net.SocketHttp"]=function()return function()return {fetch=function(self,request)self.request=request;requests[#requests+1]=request end,close=function(self)self.closed=true end}end end
local main={addItem=function(_,item)nodes[item.id]=item end,removeItemById=function(_,id)nodes[id]=nil end}
local applet={string=function(_,key)return key end,tieAndShowWindow=function(_,window)windows[#windows+1]=window end}
local M=dofile(root.."/applet/NetworkMusic.lua")
local service=M.new(applet,{main=main,jnt={}})
local function answer(index,key,value)replies[key]=value;requests[index].sink(key)end
answer(1,"servers",{revision=1,servers={{udn="uuid:one",name="NAS",generation=1},{udn="uuid:two",name="NAS",generation=2}}})
assert(nodes.localUSB and nodes["standaloneNetworkMusic:uuid:one"].node=="myMusic" and nodes["standaloneNetworkMusic:uuid:two"])
assert(not nodes.upnp and not nodes.dlna)
nodes["standaloneNetworkMusic:uuid:one"].callback()
assert(#windows==1 and #requests==3)
answer(2,"browse",{offset=0,count=1,total=2,update_id=7,items={{id="opaque:/&id",title="Server folder",container=true}}})
assert(windows[1].menu.items[1].text=="Server folder" and windows[1].menu.items[2].text=="NETWORK_MUSIC_MORE")
answer(3,"metadata",{items={{id="0",searchable=true}}})
answer(4,"caps",{search="dc:title",sort=""})
assert(#windows[1].menu.items==5 and windows[1].menu.items[4].text:find("NETWORK_MUSIC_TITLE",1,true))
windows[1].menu.items[1].callback()
assert(#windows==2)
windows[2].pop()
answer(5,"retired",{offset=0,count=1,total=1,update_id=1,items={{id="late",title="Late callback"}}})
assert(windows[2].menu.items[1].text=="NETWORK_MUSIC_LOADING")
service:refresh()
answer(7,"removed",{revision=2,servers={}})
assert(nodes.localUSB and not nodes["standaloneNetworkMusic:uuid:one"] and not nodes["standaloneNetworkMusic:uuid:two"])
service:stop();for _,timer in ipairs(timers)do assert(timer.stopped)end
io.open=savedOpen
print("network music: original My Music node, local preservation, duplicate names, async browse, opaque IDs, pagination, scoped search and retired windows passed")
