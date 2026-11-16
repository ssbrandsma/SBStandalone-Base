-- Exact admitted network-playback profiles. Decoder/ownership probes remain mandatory.
local M={VERSION="0.7.5"}
function M.allowed(board,firmware,arbitration)
    if board~="baby" and board~="fab4" then return false,"unknown_board" end
    if board=="fab4" and not arbitration then return false,"tinysc_arbitration_required" end
    if firmware=="7.7.3 r16676" then return true end
    if board=="baby" and firmware=="9.0.2 r17160" then return true end
    return false,"unsupported_firmware"
end
function M.compatible(value,board)
    return type(value)=="table" and value.schema=="SB_NETWORK_MUSIC_1"
        and value.version==M.VERSION and value.target==board
        and value.resource_wire==3 and value.upnp_ipc==1
        and value.ownership_wire==1 and value.queue_limit==256
        and value.renderer_default==false
end
function M.runtime(root,board)
    local file=io.open(root.."/network-music.compat.json","r")
    if not file then return false end
    local data=file:read(4097);file:close()
    if not data or #data>4096 then return false end
    local ok,value=pcall(function()return require("json").decode(data)end)
    return ok and M.compatible(value,board)
end
return M
