-- Lightweight frontend only: all VPN policy and process ownership live in sbvpn.
local Window=require("jive.ui.Window")
local SimpleMenu=require("jive.ui.SimpleMenu")
local Textarea=require("jive.ui.Textarea")
local Timer=require("jive.ui.Timer")
local Framework=require("jive.ui.Framework")
local SocketHttp=require("jive.net.SocketHttp")
local RequestHttp=require("jive.net.RequestHttp")
local json=require("json")
local M={};M.__index=M
local function one(path)local f=io.open(path,"r");if not f then return nil end;local s=f:read("*l");f:close();return s end
function M.new(applet)
 local port=80;local f=io.open("/mnt/storage/standalonebase/settings.conf","r")
 if f then for line in f:lines() do local p=line:match("^port=(%d+)$");if p then port=tonumber(p) end end;f:close() end
 return setmetatable({applet=applet,http=SocketHttp(jnt,"127.0.0.1",port,"StandaloneVPN"),pending=false},M)
end
function M:text(key)return tostring(self.applet:string(key)) end
function M:request(method,path,body,callback)
 local token=one("/mnt/storage/standalonebase/remote.capability")
 if not token then callback(nil,self:text("VPN_UNAVAILABLE"));return end
 local encoded=body and json.encode(body) or "";local delivered=false;local sent=false
 local request=RequestHttp(function(data,err)
  if delivered or (not data and not err) then return end;delivered=true
  if err or not data then callback(nil,self:text("VPN_UNAVAILABLE"));return end
  local ok,value=pcall(json.decode,data)
  if not ok or type(value)~="table" then callback(nil,self:text("VPN_UNAVAILABLE"));return end
  if value.error then callback(nil,tostring(value.error));return end
  callback(value)
 end,method,"/api/internal/vpn"..path,{headers={Connection="close",["X-Remote-Capability"]=token,["Content-Type"]="application/json",["Content-Length"]=tostring(#encoded)},t_bodySource=function()if sent then return nil end;sent=true;return #encoded>0 and encoded or nil end})
 if not pcall(function()self.http:fetch(request)end) then callback(nil,self:text("VPN_UNAVAILABLE")) end
end
function M:action(method,path,body)
 if self.pending then return end;self.pending=true
 self:request(method,path,body,function(_,err)self.pending=false;self.error=err;self:refresh()end)
end
function M:refresh()
 if not self.window or self.polling then return end;self.polling=true
 self:request("GET","/status",nil,function(status,err)
  self.polling=false;if not self.window then return end
  if not status then self.header:setValue(err or self:text("VPN_UNAVAILABLE"));return end
  self.status=status;self.header:setValue(self:text("VPN_STATE_"..status.state).."\n"..(status.profile_name or "").."\n"..(status.configured_country or "").."\n"..(status.public_ip_verified and status.public_ip or self:text("VPN_IP_UNVERIFIED")).."\n"..(self.error or status.last_error or ""))
  self.menuWidget:setText(self.toggle,status.enabled and self:text("VPN_DISABLE") or self:text("VPN_ENABLE"))
  if status.available and (not status.selected_profile_id or status.selected_profile_id=="") then self.header:setValue(self:text("VPN_NO_PROFILES")) end
  self:request("GET","/settings",nil,function(settings)if settings and self.window then self.menuWidget:setText(self.startupItem,self:text("VPN_STARTUP")..": "..self:text(settings.startup and "VPN_ON" or "VPN_OFF")) end end)
 end)
end
function M:locations()
 self:request("GET","/profiles",nil,function(profiles,err)
  if not profiles then self.error=err;self:refresh();return end
  local window=Window("text_list",self:text("VPN_LOCATION"));local menu=SimpleMenu("menu");local groups={}
  for _,profile in ipairs(profiles)do local country=profile.country or profile.country_code;groups[country]=groups[country] or {};table.insert(groups[country],profile)end
  local countries={};for country in pairs(groups)do table.insert(countries,country)end;table.sort(countries)
  local function choose(profile)self:action("PUT","/settings",{selected=profile.id});window:hide()end
  if #countries==0 then menu:setHeaderWidget(Textarea("help_text",self:text("VPN_NO_PROFILES")))end
  for _,country in ipairs(countries)do local countryName=country;local entries=groups[countryName]
   menu:addItem({text=countryName,callback=function()
    if #entries==1 then choose(entries[1]);return end
    local child=Window("text_list",countryName);local choices=SimpleMenu("menu")
    for _,profile in ipairs(entries)do local item=profile;choices:addItem({text=item.name,callback=function()choose(item);child:hide()end})end
    child:addWidget(choices);child:show()
   end})
  end
  window:addWidget(menu);window:show()
 end)
end
function M:startup()
 self:request("GET","/settings",nil,function(settings,err)
  if not settings then self.error=err;self:refresh();return end
  self:action("PUT","/settings",{startup=not settings.startup})
 end)
end
function M:menu()
 if self.window then self.window:show();return end
 local window=Window("text_list",self:text("VPN_TITLE"));local menu=SimpleMenu("menu")
 self.window=window;self.menuWidget=menu;self.header=Textarea("help_text",self:text("VPN_LOADING"));menu:setHeaderWidget(self.header)
 self.toggle={text=self:text("VPN_ENABLE"),callback=function()self:action("POST",self.status and self.status.enabled and "/disconnect" or "/connect",{})end};menu:addItem(self.toggle)
 menu:addItem({text=self:text("VPN_LOCATION"),callback=function()self:locations()end})
 self.startupItem={text=self:text("VPN_STARTUP"),callback=function()self:startup()end};menu:addItem(self.startupItem)
 menu:addItem({text=self:text("VPN_STATUS"),callback=function()self:refresh()end})
 menu:addItem({text=self:text("VPN_PROVISION"),callback=function()self.header:setValue(self:text("VPN_NO_PROFILES"))end})
 window:addWidget(menu);window:addListener(Framework.constants.EVENT_WINDOW_POP,function()self:stop();return Framework.constants.EVENT_UNUSED end);window:show()
 self.timer=Timer(5000,function()self:refresh()end);self.timer:start();self:refresh()
end
function M:stop()if self.timer then self.timer:stop();self.timer=nil end;self.window=nil;self.menuWidget=nil;self.header=nil;self.polling=false end
return M
