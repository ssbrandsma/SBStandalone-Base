-- Network sources extend the resident My Music node. All network operations
-- are asynchronous and window generations discard retired completions.
local M={}
local Timer=require("jive.ui.Timer")
local Window=require("jive.ui.Window")
local SimpleMenu=require("jive.ui.SimpleMenu")
local Framework=require("jive.ui.Framework")
local RequestHttp=require("jive.net.RequestHttp")
local SocketHttp=require("jive.net.SocketHttp")
local json=require("json")
function M.new(applet,options)
    local o=options or {}
    local self={active=true,nodes={},pending={},revision=nil}
    local main=o.main or jiveMain
    local function label(key) return tostring(applet:string("NETWORK_MUSIC_"..key)) end
    function self:request(args,done)
        local f=io.open(o.tokenPath or "/var/run/sb-upnp/token","r")
        local token=f and f:read(65);if f then f:close() end
        if not token or #token~=64 or not token:match("^[0-9a-f]+$") then done(nil,"unavailable");return function() end end
        local finished,sent,body=false,false,""
        local socket=SocketHttp(o.jnt or jnt,"127.0.0.1",9000,"StandaloneNetworkMusic")
        local timer
        local function finish(value,err)
            if finished then return end;finished=true
            if timer then timer:stop() end
            self.pending[socket]=nil
            if type(socket.close)=="function" then pcall(function() socket:close() end) end
            if self.active then done(value,err) end
        end
        local command={"networkmusic"};for key,value in pairs(args) do command[#command+1]=key..":"..(type(value)=="number" and string.format("%.0f",value) or tostring(value)) end
        local payload=json.encode({data={request={"",command}}})
        local request=RequestHttp(function(data,err)
            if finished then return end
            if err then finish(nil,"network_error");return end
            if data then
                body=body..data;if #body>131072 then finish(nil,"response_too_large");return end
                local ok,value=pcall(json.decode,body)
                if ok and type(value)=="table" then finish(value,value.error) end
            end
        end,"POST","/api/network-music",{headers={Connection="close",["X-Standalone-UPnP"]=token},
            t_bodySource=function() if not sent then sent=true;return payload end end})
        timer=Timer(30000,function() finish(nil,"timeout") end,true);timer:start()
        local cancel=function() if finished then return end;finished=true;timer:stop();self.pending[socket]=nil;if type(socket.close)=="function" then pcall(function() socket:close() end) end end
        self.pending[socket]=cancel
        local ok=pcall(function() socket:fetch(request) end);if not ok then finish(nil,"network_error") end
        return cancel
    end
    function self:browse(server,object,title,search)
        local window=Window("text_list",title)
        local menu=SimpleMenu("menu");window:addWidget(menu)
        local active,cancel,offset,update,rows=true,nil,0,nil,{}
        local actions,lastPage,sort={},nil,""
        local windowRequests={}
        local load
        local function display()
            local items={};for _,row in ipairs(rows) do items[#items+1]=row end
            for _,row in ipairs(actions) do items[#items+1]=row end
            if lastPage and lastPage.offset+lastPage.count<lastPage.total then
                items[#items+1]={text=label("MORE"),callback=function() offset=lastPage.offset+lastPage.count;load() end}
            end
            if #items==0 then items[1]={text=label("EMPTY"),style="item_info"} end
            local selected=type(menu.getSelectedIndex)=="function" and menu:getSelectedIndex()
            menu:setItems(items)
            if selected and type(menu.setSelectedIndex)=="function" then menu:setSelectedIndex(math.min(selected,#items)) end
        end
        load=function()
            if cancel then cancel() end
            local args={op=search and "search" or "browse",server=server.udn,generation=server.generation,object=object,offset=offset,limit=50}
            if search then args.field=search.field;args.text=search.text end
            if sort~="" then args.sort=sort end
            cancel=self:request(args,function(page,err)
                if not active then return end
                if err or not page or type(page.items)~="table" then
                    menu:setItems({{text=label("UNAVAILABLE"),callback=function() offset=0;update=nil;rows={};load() end}});return
                end
                if update and update~=page.update_id then rows={};offset=0;update=nil;load();return end
                update=page.update_id
                for _,item in ipairs(page.items) do
                    local value=item
                    rows[#rows+1]={id=item.id,text=item.title~="" and item.title or item.id,callback=function()
                        if value.container then self:browse(server,value.id,value.title)
                        elseif value.playable then self:track(server,value) end
                    end}
                end
                lastPage=page;display()
                -- Keep at most four pages in the resident menu. Further pages
                -- replace the window contents rather than retaining the library.
                if #rows>=200 then rows={} end
            end)
        end
        menu:setItems({{text=label("LOADING"),style="item_info"}})
        window:addListener(Framework.constants.EVENT_WINDOW_POP,function() active=false;if cancel then cancel() end;for _,retire in ipairs(windowRequests) do retire() end end)
        applet:tieAndShowWindow(window);load()
        if not search then
            windowRequests[#windowRequests+1]=self:request({op="metadata",server=server.udn,generation=server.generation,object=object,limit=1},function(meta,err)
                if not active or err or not meta or not meta.items or not meta.items[1] then return end
                actions[#actions+1]={text=label("PLAY_ALL"),callback=function() windowRequests[#windowRequests+1]=self:queue(server,object,"playcontainer",menu) end}
                actions[#actions+1]={text=label("ADD_ALL"),callback=function() windowRequests[#windowRequests+1]=self:queue(server,object,"addcontainer",menu) end}
                display()
                windowRequests[#windowRequests+1]=self:request({op="capabilities",server=server.udn,generation=server.generation},function(caps,error)
                    if not active or error or not caps then return end
                    local supported={};for cap in tostring(caps.search):gmatch("[^,]+") do supported[cap:match("^%s*(.-)%s*$")]=true end
                    local labels={["dc:title"]="TITLE",["upnp:artist"]="ARTIST",["upnp:album"]="ALBUM",["upnp:genre"]="GENRE"}
                    for _,field in ipairs({"dc:title","upnp:artist","upnp:album","upnp:genre"}) do
                        local selected=field
                        if meta.items[1].searchable and (supported[field] or supported["*"]) then actions[#actions+1]={text=label("SEARCH").." — "..label(labels[field]),callback=function() self:search(server,object,selected) end} end
                    end
                    for cap in tostring(caps.sort):gmatch("[^,]+") do
                        if cap:match("^%s*(.-)%s*$")=="dc:title" then
                            actions[#actions+1]={text=label("SORT_TITLE"),callback=function() sort="+dc:title";rows={};offset=0;update=nil;load() end}
                            actions[#actions+1]={text=label("SERVER_ORDER"),callback=function() sort="";rows={};offset=0;update=nil;load() end}
                        end
                    end
                    display()
                end)
            end)
        end
    end
    function self:track(server,item)
        local window=Window("text_list",item.title);local menu=SimpleMenu("menu")
        local cancel
        menu:addItem({text=item.artist or "",style="item_info"});menu:addItem({text=item.album or "",style="item_info"})
        menu:addItem({text=label("PLAY"),callback=function() if cancel then cancel() end;cancel=self:queue(server,item.id,"play",menu) end})
        menu:addItem({text=label("ADD"),callback=function() if cancel then cancel() end;cancel=self:queue(server,item.id,"add",menu) end})
        menu:addItem({text=label("PLAY_NEXT"),callback=function() if cancel then cancel() end;cancel=self:queue(server,item.id,"next",menu) end})
        window:addListener(Framework.constants.EVENT_WINDOW_POP,function() if cancel then cancel() end end)
        window:addWidget(menu);applet:tieAndShowWindow(window)
    end
    function self:queue(server,object,operation,menu)
        return self:request({op=operation,server=server.udn,generation=server.generation,object=object},function(value,err)
            if err then menu:addItem({text=label("UNAVAILABLE"),style="item_info"})
            elseif operation=="play" or operation=="playcontainer" then appletManager:callService("goNowPlaying") end
        end)
    end
    function self:search(server,object,field)
        local Textinput=require("jive.ui.Textinput");local Keyboard=require("jive.ui.Keyboard");local Group=require("jive.ui.Group")
        local window=Window("text_list",label("SEARCH"))
        local input=Textinput("textinput",Textinput.textValue("",0,128),function(_,value)
            if value and value~="" then self:browse(server,object,label("SEARCH"),{field=field,text=value}) end;return true
        end)
        local group=Group("keyboard_textinput",{textinput=input,backspace=Keyboard.backspace()})
        window:addWidget(group);window:addWidget(Keyboard("keyboard","qwerty",input));window:focusWidget(group);applet:tieAndShowWindow(window)
    end
    function self:configureRenderer(enabled,done)
        local settings=applet:getSettings()
        if not settings.networkRendererUUID then
            local player=require("jive.slim.Player"):getLocalPlayer()
            local mac=player and tostring(player:getId()):gsub("[^%x]",""):lower()
            if not mac or #mac~=12 then if done then done(false) end;return end
            settings.networkRendererUUID="uuid:8e9bff26-7f7d-4d0a-ae98-"..mac
        end
        local name=settings.networkRendererName or label("RENDERER_NAME")
        self:request({op="renderer",text=enabled and "1" or "0",server=settings.networkRendererUUID,object=name},function(value,err)
            if not err then settings.networkRendererEnabled=enabled;applet:storeSettings() end
            if done then done(not err and value~=nil) end
        end)
    end
    function self:settings()
        local settings=applet:getSettings()
        local window=Window("text_list",label("NETWORK_PLAYBACK"));local menu=SimpleMenu("menu")
        local function rows()
            menu:setItems({
                {text=label("ALLOW")..": "..label(settings.networkRendererEnabled and "ON" or "OFF"),callback=function()
                    self:configureRenderer(not settings.networkRendererEnabled,function(ok) if ok then rows() else menu:addItem({text=label("UNAVAILABLE"),style="item_info"}) end end)
                end},
                {text=settings.networkRendererName or label("RENDERER_NAME"),callback=function()
                    local Textinput=require("jive.ui.Textinput");local Keyboard=require("jive.ui.Keyboard");local Group=require("jive.ui.Group")
                    local editor=Window("text_list",label("DEVICE_NAME"))
                    local input=Textinput("textinput",Textinput.textValue(settings.networkRendererName or label("RENDERER_NAME"),1,64),function(_,value)
                        if value and value~="" then settings.networkRendererName=value;applet:storeSettings();if settings.networkRendererEnabled then self:configureRenderer(true) end;rows();editor:hide() end;return true
                    end)
                    local group=Group("keyboard_textinput",{textinput=input,backspace=Keyboard.backspace()});editor:addWidget(group);editor:addWidget(Keyboard("keyboard","qwerty",input));editor:focusWidget(group);applet:tieAndShowWindow(editor)
                end}
            })
        end
        rows();window:addWidget(menu);applet:tieAndShowWindow(window)
    end
    function self:refresh()
        if self.refreshing then return end;self.refreshing=true
        if type(applet.getSettings)=="function" and applet:getSettings().networkRendererEnabled then
            self:request({op="health"},function(value,err) if not err and value and value.renderer==false then self:configureRenderer(true) end end)
        end
        self:request({op="servers"},function(value,err)
            self.refreshing=false
            if err or not value or type(value.servers)~="table" then return end
            if value.revision==self.revision then return end;self.revision=value.revision
            local found={}
            for _,server in ipairs(value.servers) do
                if type(server.udn)=="string" and type(server.name)=="string" then
                    local key="standaloneNetworkMusic:"..server.udn;found[key]=true
                    local source=server
                    main:addItem({id=key,node="myMusic",text=server.name,weight=80,callback=function() self:browse(source,"0",source.name) end})
                end
            end
            for key in pairs(self.nodes) do if not found[key] then main:removeItemById(key) end end
            self.nodes=found
        end)
    end
    function self:stop()
        self.active=false;if self.timer then self.timer:stop() end
        local pending={};for _,cancel in pairs(self.pending) do pending[#pending+1]=cancel end;for _,cancel in ipairs(pending) do cancel() end
        for key in pairs(self.nodes) do main:removeItemById(key) end;self.nodes={}
    end
    self.timer=Timer(15000,function() self:refresh() end);self.timer:start();self:refresh()
    if type(applet.getSettings)=="function" and applet:getSettings().networkRendererEnabled then self:configureRenderer(true) end
    return self
end
return M
