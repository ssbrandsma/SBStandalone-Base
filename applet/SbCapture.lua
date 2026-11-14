-- SB-WP4A6-CAPTURE (diagnostic, not production): one-shot capture of
-- live /cometd POST bodies + replies to diagnose the UI-flap loop.
-- Chains SocketHttp.fetch AFTER HttpTrace; wraps t_body/t_setResponseBody
-- per request (HttpTrace's own shadowing pattern), so consumption happens
-- exactly once at the natural time. Captures at most 3 POSTs, then unwires
-- itself. Bodies are Bayeux LMS traffic (no credentials).
local M = {}
local MAX_POSTS = 5
local BODY_CAP = 2000
local REPLY_CAP = 1200

function M.install()
    local ok, Socket = pcall(require, "jive.net.SocketHttp")
    if not ok or type(Socket) ~= "table"
        or type(Socket.fetch) ~= "function" then
        return nil, "no_socket"
    end
    if Socket._sbCaptureInstalled then return true end
    local lok, logModule = pcall(require, "jive.utils.log")
    local log = (lok and logModule.logger("StandaloneBase"))
        or { info = function() end, warn = function() end }
    local outer = Socket.fetch
    local count = 0
    local function clean(s)
        return tostring(s or ""):gsub("[%c]", "?")
    end
    Socket.fetch = function(self, request)
        if count < MAX_POSTS and type(request) == "table"
            and not request._sbCapWrapped then
            local spec = request.t_httpRequest
            local uri = (spec and spec.uri) or {}
            if spec and spec.method == "POST" and uri.path == "/cometd"
                and type(request.t_body) == "function"
                and type(request.t_setResponseBody) == "function" then
                request._sbCapWrapped = true
                local origBody = request.t_body
                request.t_body = function(req)
                    return origBody(req)
                end
                -- Capture the body lazily at first response chunk: by then
                -- the request was fully sent (cache populated, zero risk
                -- of disturbing consumption).
                local origResp = request.t_setResponseBody
                local chunks, bodyLogged = {}, false
                request.t_setResponseBody = function(req, data)
                    if not bodyLogged then
                        bodyLogged = true
                        count = count + 1
                        local bok, b = pcall(function()
                            return req.t_httpRequest.body
                        end)
                        if bok and type(b) == "string" then
                            log:warn("SB-CAPTURE #", count,
                                " reqlen=", #b)
                            local shown = clean(b:sub(1, BODY_CAP))
                            for i = 1, #shown, 800 do
                                log:warn("SB-CAPTURE #", count, " q=",
                                    shown:sub(i, i + 799))
                            end
                        else
                            log:warn("SB-CAPTURE #", count,
                                " no-body")
                        end
                    end
                    if type(data) == "string" then
                        chunks[#chunks + 1] = data
                    end
                    if data == nil then
                        local reply = clean(table.concat(chunks):sub(
                            1, REPLY_CAP))
                        for i = 1, #reply, 800 do
                            log:warn("SB-CAPTURE #", count, " r=",
                                reply:sub(i, i + 799))
                        end
                        if count >= MAX_POSTS then
                            Socket.fetch = outer
                            log:warn("SB-CAPTURE done")
                        end
                    end
                    return origResp(req, data)
                end
            end
        end
        return outer(self, request)
    end
    Socket._sbCaptureInstalled = true
    log:warn("SB-CAPTURE armed")
    return true
end
return M
