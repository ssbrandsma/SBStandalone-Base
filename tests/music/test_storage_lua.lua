local path = assert(arg[1])
local M = dofile(path .. "/applet/MusicStorage.lua")
assert(M.start(nil)==nil)
local called, callback, hints = 0, nil, {}
local bridge={hint=function(_,seq,action,dev) hints[#hints+1]={seq,action,dev}; return true end,
    state=function() return {available=false} end}
local options={enabled=false,platform="fab4",bridge=bridge,addUeventListener=function(_,fn) called=called+1;callback=fn end}
assert(M.start(options)==nil and called==0)
options.enabled=true;options.platform="baby"
assert(M.start(options)==nil and called==0)
options.platform="fab4";local a=assert(M.start(options));assert(called==1 and #hints==1 and hints[1][2]=="reconcile")
callback(nil,{SUBSYSTEM="net",ACTION="add"});assert(#hints==1)
callback(nil,{SUBSYSTEM="block",ACTION="add",DEVPATH="/devices/usb/sda1"});assert(#hints==2)
callback(nil,{SUBSYSTEM="block",ACTION="remove",DEVPATH="/devices/usb/sda1"});assert(#hints==3)
callback(nil,{SUBSYSTEM="block",ACTION="add",DEVPATH="/devices/usb/sda1",SEQNUM="100"});assert(#hints==4 and hints[4][1]==4 and hints[4][3]=="sda1")
callback(nil,{SUBSYSTEM="block",ACTION="remove",DEVPATH="/devices/usb/sda1",SEQNUM="99"});assert(#hints==4)
assert(a:state().available==false);a:stop();callback(nil,{SUBSYSTEM="block",ACTION="add"});assert(#hints==4)
local b=assert(M.start(options));assert(#hints==5)
callback(nil,{SUBSYSTEM="block",ACTION="add",DEVPATH="/devices/usb/sda1",SEQNUM="1"});assert(#hints==6 and hints[6][1]==2)
callback(nil,{SUBSYSTEM="block",ACTION="remove",DEVPATH="/devices/usb/sda1",SEQNUM="1"});assert(#hints==6);b:stop()
print("WP6a Lua adapter: gating, notifications, no timers/UI/firmware actions OK")
