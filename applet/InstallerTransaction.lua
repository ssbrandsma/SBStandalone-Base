-- Transactional filesystem boundary for the existing stock Applet Installer.
-- Download/ZIP handling and the normal firmware completion lifecycle stay in
-- InstallerCompat. Native images are renamed, never overwritten in place.
local M={}
local function safeName(name) return type(name)=="string" and name:match("^[%w_]+$") and #name<=64 end
function M.new(parent,names,options)
    local fs,files,system=options.fs,options.io,options.os or os
    local json=options.json or require("json")
    local hasher=options.hasher or function()return require("sha1"):new()end
    local self={parent=parent,rows={},fs=fs,files=files,system=system}
    local attr=fs.symlinkattributes or fs.attributes
    local function directory(path)
        local value=attr(path)
        if value then assert(value.mode=="directory","Unsafe installer directory: "..path)
        else assert(fs.mkdir(path),"Cannot create installer directory: "..path) end
    end
    assert(type(parent)=="string" and parent:sub(1,1)=="/" and not parent:find("[\r\n%z]") and not parent:find("/../",1,true),"Invalid applet directory")
    local named=0;for name in pairs(names) do named=named+1;assert(named<=32 and safeName(name),"Invalid applet selection") end
    self.root=parent.."/.standalone-install"
    local recovered
    if attr(self.root) then
        local file=assert(files.open(self.root.."/receipt.json","rb"),"Missing previous installer receipt")
        local previous=json.decode(file:read(262145));file:close()
        assert(previous and previous.parent==parent and previous.schema=="SB_APPLET_INSTALL_1","Invalid installer receipt")
        assert(previous.state=="PREPARED" or previous.state=="COMMITTING" or previous.state=="COMMITTED_ON_DISK" or previous.state=="ROLLED_BACK","Invalid installer state")
        if options.recover then
            assert(previous.state~="COMMITTED_ON_DISK" or (type(options.quiescent)=="function" and options.quiescent()),"Committed rollback requires independent quiescence proof")
            recovered=previous
        else
            assert(previous.state=="COMMITTED_ON_DISK" or previous.state=="ROLLED_BACK","A previous installer transaction needs explicit recovery")
            assert(not attr(parent.."/.standalone-previous"),"Previous rollback slot must be archived before another installation")
            assert(system.rename(self.root,parent.."/.standalone-previous"))
        end
    end
    if not recovered then assert(fs.mkdir(self.root),"Installer transaction already active") end
    local function quote(path)return "'"..path:gsub("'","'\\''").."'"end
    local function setMode(path,mode,kind)
        local result=(system.execute or os.execute)("chmod "..mode.." "..quote(path))
        -- LuaFileSystem 1.2 has no permissions attribute. Establish actual
        -- mode/ownership before downloading, while a shell can still fork.
        local probe,reason=(files.popen or io.popen)("LC_ALL=C ls -ldn "..quote(path).." 2>/dev/null","r")
        assert(probe,"Cannot inspect installer permissions: "..tostring(reason))
        local line=probe:read(1024);probe:close()
        local permissions,uid,gid=(line or ""):match("^(%S+)%s+%d+%s+(%d+)%s+(%d+)%s+")
        local expected=kind..(mode=="700" and "rwx------" or "rwxr-xr-x")
        assert(permissions==expected and uid=="0" and gid=="0","Cannot set installer mode "..mode..": "..path.." (status "..tostring(result)..", observed "..tostring(permissions)..")")
    end
    setMode(self.root,"700","d")
    directory(self.root.."/new");directory(self.root.."/old")
    local record=recovered or {schema="SB_APPLET_INSTALL_1",parent=parent,state="PREPARED",applets={}}
    local function journal()
        local file=assert(files.open(self.root.."/receipt.json.new","wb"))
        assert(file:write(json.encode(record)));assert(file:close())
        assert(system.rename(self.root.."/receipt.json.new",self.root.."/receipt.json"))
    end
    local function inventory(root)
        local result,total,count={},0,0
        local function visit(path,prefix,depth)
            assert(depth<=16,"Installer tree too deep")
            for name in fs.dir(path) do if name~="." and name~=".." then
                assert(not name:find("[/%z\r\n]"),"Invalid installed member")
                local relative=prefix..name;local value=assert(attr(path.."/"..name))
                if value.mode=="directory" then visit(path.."/"..name,relative.."/",depth+1)
                else
                    assert(value.mode=="file","Installer refuses symlinks and special files")
                    count=count+1;assert(count<=4096,"Too many installer members")
                    local file=assert(files.open(path.."/"..name,"rb"));local digest=hasher()
                    while true do
                        local data=file:read(16384);if not data then break end
                        total=total+#data;assert(total<=67108864,"Installer tree exceeds 64 MiB");digest:update(data)
                        -- Do not retain megabytes of dead read buffers until
                        -- Lua's next automatic cycle on the 64 MB Baby.
                        collectgarbage("step",64)
                    end
                    assert(file:close());result[relative]=digest:digest()
                end
            end end
        end
        if attr(root) then visit(root,"",0) end
        return result
    end
    local function same(a,b)
        for key,value in pairs(a) do if b[key]~=value then return false end end
        for key,value in pairs(b) do if a[key]~=value then return false end end
        return true
    end
    for name in pairs(recovered and record.applets or names) do
        assert(safeName(name),"Invalid receipt applet name")
        local target=parent.."/"..name;local value=attr(target)
        assert(not value or value.mode=="directory","Unsafe applet target")
        if recovered then
            local row=record.applets[name]
            assert(type(row.old)=="table" and (row.new==nil or type(row.new)=="table"),"Invalid receipt inventory")
            row.saved=attr(self.root.."/old/"..name)~=nil
            if row.saved then assert(same(inventory(self.root.."/old/"..name),row.old),"Corrupt rollback applet") end
            row.installed=value and row.new and same(inventory(target),row.new) or false
            if value and not row.installed then assert(not row.saved and same(inventory(target),row.old),"Unknown current applet revision") end
            if row.existed and not value then assert(row.saved,"Original applet is unavailable") end
        else record.applets[name]={existed=value~=nil,old=inventory(target)} end
        self.rows[#self.rows+1]=name
    end
    table.sort(self.rows);journal()
    local executables={"sbbase","sbwebserver","sbproxy","sbvpn","sb-tinysc-control","sb-storage-helper","sb-storage-updater","svgpngrenderer","sbupnp","sbbase-network","sbwebserver-network","storage-setup.sh","storage-boot.sh","patch-squeezeplay-artwork.sh"}
    local prepared={}
    local function sameFile(a,b)
        return a and b and a.mode=="file" and b.mode=="file" and a.ino~=nil and a.dev~=nil
            and a.ino==b.ino and a.dev==b.dev and b.uid==0 and b.gid==0
    end
    function self:planned(name)return record.applets[name]~=nil end
    function self:destination(name)
        assert(record.applets[name],"Unplanned installer applet")
        local path=self.root.."/new/"..name;directory(path)
        if name=="StandaloneBase" and not prepared[name] then
            collectgarbage("collect")
            prepared[name]={}
            for _,member in ipairs(executables) do
                local target=path.."/"..member
                assert(not attr(target),"Unexpected staged executable")
                local file=assert(files.open(target,"wb"));assert(file:close())
                setMode(target,"755","-")
                local value=assert(attr(target));assert(sameFile(value,value),"Cannot attest staged executable inode")
                prepared[name][member]=value
            end
        end
        return path
    end
    function self:verify(name)
        local path=self.root.."/new/"..name
        -- The ZIP sink truncates existing files; it never renames them or
        -- changes permissions. Retain those prequalified inodes. No fork is
        -- needed after extraction on the 64 MB Radio. Remove unused empty
        -- placeholders so older/Base-only archives retain their exact scope.
        for member,value in pairs(prepared[name] or {}) do
            local target=path.."/"..member;local current=attr(target)
            assert(sameFile(value,current),"Staged executable inode/owner changed: "..member)
            if current.size==0 then assert((system.remove or os.remove)(target),"Cannot remove unused executable placeholder") end
        end
        local entries=inventory(path)
        assert(next(entries),"Empty applet archive")
        assert(entries[name.."Meta.lua"],"Missing applet metadata")
        for relative in pairs(entries) do
            if relative:match("%.lua$") then assert((options.compile or loadfile)(path.."/"..relative),"Invalid Lua member: "..relative) end
        end
        if name=="StandaloneBase" and entries.sbupnp then
            assert(entries["sbbase-network"] and entries["sbwebserver-network"] and entries["NetworkMusic.lua"],"Incomplete network runtime")
            for _,module in ipairs({"PlaybackFenceV6.lua","NetworkProfile.lua","OwnershipHooks.lua","OwnershipRuntime.lua","OwnershipClient.lua","OwnershipHttp.lua"}) do assert(entries[module],"Missing network runtime module: "..module) end
            local file=assert(files.open(path.."/network-music.compat.json","rb"));local compat=json.decode(file:read(4097));file:close()
            local versionFile=assert(files.open(path.."/build-version","rb"));local version=versionFile:read(64);versionFile:close()
            assert(version and compat and compat.version==version:gsub("[\r\n].*",""),"Inconsistent network package version")
            assert(compat and compat.schema=="SB_NETWORK_MUSIC_1" and compat.resource_wire==3 and compat.upnp_ipc==1 and compat.ownership_wire==1 and compat.queue_limit==256 and compat.renderer_default==false,"Incompatible network runtime")
            local platform=options.platform or require("applets.StandaloneBase.Platform").detect().name
            assert((platform=="baby" or platform=="fab4") and compat.target==platform,"Wrong installation target")
            for _,binary in ipairs({"sbbase","sbwebserver","sbproxy","sbvpn","sb-tinysc-control","sb-storage-helper","sb-storage-updater","svgpngrenderer","sbupnp","sbbase-network","sbwebserver-network"}) do
                assert(entries[binary],"Missing native runtime member: "..binary)
                local image=assert(files.open(path.."/"..binary,"rb"));local header=image:read(52)
                local function little(raw,offset,width)local value=0;for i=width-1,0,-1 do local byte=raw:byte(offset+i+1);assert(byte,"Truncated ELF");value=value*256+byte end;return value end
                assert(header and #header==52 and header:sub(1,6)=="\127ELF\1\1" and little(header,16,2)==2 and little(header,18,2)==40,"Wrong native architecture")
                assert(math.floor(little(header,36,4)/1024)%2==0,"Hard-float binary refused")
                local offset,bytes,count=little(header,28,4),little(header,42,2),little(header,44,2)
                assert(offset>=52 and offset<=1048576 and bytes==32 and count>0 and count<=32,"Invalid ELF program table")
                assert(image:seek("set",offset));local loaded=false
                for _=1,count do local program=image:read(32);assert(program and #program==32,"Truncated ELF program table");local kind=little(program,0,4);assert(kind~=2 and kind~=3,"Dynamic native binary refused");if kind==1 then loaded=true end end
                assert(image:close());assert(loaded,"No ELF load segment")
                assert(prepared[name] and sameFile(prepared[name][binary],attr(path.."/"..binary)),"Unqualified executable inode: "..binary)
            end
            for _,script in ipairs({"storage-setup.sh","storage-boot.sh","patch-squeezeplay-artwork.sh"}) do
                assert(entries[script],"Missing runtime script")
                assert(prepared[name] and sameFile(prepared[name][script],attr(path.."/"..script)),"Unqualified executable inode: "..script)
            end
        end
        record.applets[name].new=entries;journal()
    end
    function self:rollback()
        for i=#self.rows,1,-1 do
            local name=self.rows[i];local row=record.applets[name];local target=parent.."/"..name
            if row.installed then
                assert(same(inventory(target),row.new),"Refuse rollback over changed applet")
                assert(system.rename(target,self.root.."/new/"..name));row.installed=false
            end
            if row.saved then
                assert(same(inventory(self.root.."/old/"..name),row.old),"Corrupt rollback applet")
                assert(not attr(target),"Rollback destination occupied")
                assert(system.rename(self.root.."/old/"..name,target));row.saved=false
            end
        end
        local function discard(path)
            local value=attr(path);if not value then return end
            assert(value.mode=="directory","Unsafe staged cleanup")
            for name in fs.dir(path) do if name~="." and name~=".." then
                local child=path.."/"..name;local entry=assert(attr(child))
                if entry.mode=="directory" then discard(child)
                else assert(entry.mode=="file" and (system.remove or os.remove)(child),"Cannot remove staged member") end
            end end
            assert(fs.rmdir(path),"Cannot remove staged directory")
        end
        -- Release failed-download space only after all original trees have
        -- been restored. Never discard committed rollback backups.
        for _,name in ipairs(self.rows) do
            inventory(self.root.."/new/"..name) -- refuse symlinks before cleanup
            discard(self.root.."/new/"..name)
        end
        record.state="ROLLED_BACK";journal()
    end
    function self:commit(downloads)
        for _,name in ipairs(self.rows) do
            local row=record.applets[name]
            assert(same(inventory(parent.."/"..name),row.old),"Applet changed during download")
            if downloads[name] then assert(row.new,"Unverified staged applet") end
            for member,value in pairs(prepared[name] or {}) do
                if row.new and row.new[member] then
                    assert(sameFile(value,attr(self.root.."/new/"..name.."/"..member)),"Executable inode changed before commit: "..member)
                end
            end
        end
        record.state="COMMITTING";journal()
        local ok,err=pcall(function()
            for _,name in ipairs(self.rows) do
                local row=record.applets[name];local target=parent.."/"..name
                if attr(target) then assert(system.rename(target,self.root.."/old/"..name));row.saved=true;journal() end
                if downloads[name] then assert(system.rename(self.root.."/new/"..name,target));row.installed=true;journal() end
            end
        end)
        if not ok then self:rollback();error(err) end
        record.state="COMMITTED_ON_DISK";journal()
        -- Backup stays available independently of the new native services.
        -- Runtime activation is the stock firmware completion/reboot step.
    end
    return self
end
function M.recover(parent,options)
    local attr=options.fs.symlinkattributes or options.fs.attributes
    assert(attr(parent.."/.standalone-install"),"No installer transaction to recover")
    local selected={};for key,value in pairs(options) do selected[key]=value end;selected.recover=true
    local transaction=M.new(parent,{},selected);transaction:rollback();return true
end
return M
