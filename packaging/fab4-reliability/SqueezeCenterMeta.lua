-- Opt-in persistent inhibition prototype, independent of Base and RAM.
local print=print
local oo=require('loop.simple')
local AppletMeta=require('jive.AppletMeta')
module(...)
oo.class(_M,AppletMeta)
function jiveVersion() return 1,1 end
function defaultSettings() return {mountedDevices={}} end
function registerApplet(meta)
 meta:registerService('isBuiltInSCRunning')
end
function configureApplet(meta)
 print('SB_TINYSC_INHIBIT: persistent overlay guard loaded')
end
return _M
