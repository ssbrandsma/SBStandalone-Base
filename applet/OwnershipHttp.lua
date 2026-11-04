-- Explicit opt-in runtime transport using the existing Base HTTP listener.
local M={}
function M.new(o)
    if not o or o.enabled~=true or type(o.tokenPath)~="string" then return nil end
    local f=io.open(o.tokenPath,"r");if not f then return nil end
    local token=f:read(65);f:close()
    if not token or #token~=64 or not token:match("^[0-9a-f]+$") then return nil end
    local RequestHttp=require("jive.net.RequestHttp")
    local SocketHttp=require("jive.net.SocketHttp")
    local json=require("json")
    return function(body,done)
        local sent,finished=false,false
        local request=RequestHttp(function(data,err)
            if finished then return end
            if err then finished=true;done(nil,"transport_error");return end
            if data and #data<=1024 then
                local ok,value=pcall(json.decode,data)
                if ok and type(value)=="table" then
                    finished=true
                    if value.error then done(nil,"bridge_rejected") else done(value) end
                end
            elseif data then finished=true;done(nil,"oversized_reply") end
        end,"POST","/api/music/ownership",{headers={Connection="close",["X-Music-Ownership"]=token,
            ["Content-Length"]=tostring(#body),["Content-Type"]="text/plain"},
            t_bodySource=function() if not sent then sent=true;return body end end})
        local socket=SocketHttp(o.jnt,"127.0.0.1",9000,"StandaloneOwnership")
        socket:fetch(request)
    end
end
return M
