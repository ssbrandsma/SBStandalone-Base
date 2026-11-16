-- Native frontend contract: backend truth, grouped profiles, no shell commands.
local root=arg[1];local requests,windows,timers={}, {}, {};local body
local status={available=true,enabled=false,state="DISCONNECTED",selected_profile_id=string.rep("a",32),profile_name="Example",configured_country="United Kingdom",public_ip_verified=false,last_error=""}
local settings={startup=false};local profiles={{id=string.rep("a",32),name="One",country="United Kingdom"},{id=string.rep("b",32),name="Two",country="United Kingdom"}}
local originalOpen=io.open
io.open=function(path,mode)
 if path:find("remote.capability",1,true)then return {read=function()return string.rep("c",64)end,close=function()end}end
 if path:find("settings.conf",1,true)then return {lines=function()local done=false;return function()if not done then done=true;return "port=80"end end end,close=function()end}end
 return originalOpen(path,mode)
end
os.execute=function()error("VPN frontend must not execute commands")end
_G.jnt={}
package.preload['json']=function()return {encode=function(value)body=value;return "payload"end,decode=function(value)return value end}end
package.preload['jive.ui.Framework']=function()return {constants={EVENT_WINDOW_POP=9,EVENT_UNUSED=0}}end
package.preload['jive.ui.Window']=function()return function(style,title)local w={title=title,listeners={}};function w:addWidget(m)self.menu=m end;function w:addListener(event,fn)self.listeners[event]=fn end;function w:show()end;function w:hide()if self.listeners[9]then self.listeners[9]()end end;windows[#windows+1]=w;return w end end
package.preload['jive.ui.SimpleMenu']=function()return function()local m={items={}};function m:addItem(item)self.items[#self.items+1]=item end;function m:setHeaderWidget(h)self.header=h end;function m:setText(item,text)item.text=text end;return m end end
package.preload['jive.ui.Textarea']=function()return function(style,value)return {value=value,setValue=function(self,text)self.value=text end}end end
package.preload['jive.ui.Timer']=function()return function(interval,callback)local t={interval=interval,callback=callback};function t:start()self.active=true end;function t:stop()self.active=false end;timers[#timers+1]=t;return t end end
package.preload['jive.net.RequestHttp']=function()return function(callback,method,path,opts)return {callback=callback,method=method,path=path,opts=opts}end end
package.preload['jive.net.SocketHttp']=function()return function()return {fetch=function(self,r)
 requests[#requests+1]=r;assert(r.opts.headers['X-Remote-Capability']==string.rep('c',64));local value={}
 if r.method=='POST' and r.path:find('/connect',1,true)then status.enabled=true;status.state='CONNECTING' end
 if r.method=='PUT' and r.path:find('/settings',1,true)then if body.selected then status.selected_profile_id=body.selected end;if body.startup~=nil then settings.startup=body.startup end end
 if r.path:find('/status',1,true)then value=status elseif r.method=='GET'and r.path:find('/settings',1,true)then value=settings elseif r.path:find('/profiles',1,true)then value=profiles end
 r.callback(value)
 end}end end
local Vpn=assert(loadfile(root..'/applet/Vpn.lua'))();local vpn=Vpn.new({string=function(self,key)return key end});vpn:menu()
assert(vpn.header.value:find('VPN_STATE_DISCONNECTED',1,true));vpn.toggle.callback();assert(vpn.header.value:find('VPN_STATE_CONNECTING',1,true));assert(not vpn.header.value:find('VPN_STATE_CONNECTED',1,true));assert(vpn.toggle.text=='VPN_DISABLE')
vpn:locations();local country=windows[#windows];assert(#country.menu.items==1);country.menu.items[1].callback();local child=windows[#windows];assert(#child.menu.items==2);child.menu.items[2].callback();assert(status.selected_profile_id==profiles[2].id)
vpn:startup();assert(settings.startup);assert(vpn.startupItem.text:find('VPN_ON',1,true));assert(timers[1].interval==5000)
vpn.window:hide();assert(not timers[1].active and vpn.window==nil)
print('VPN Jive: backend state, grouping, selection, startup and polling cleanup passed')
