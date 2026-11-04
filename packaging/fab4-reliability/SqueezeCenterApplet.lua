-- No original startup/prefs/media code is loaded while this file is installed.
local oo=require('loop.simple')
local Applet=require('jive.Applet')
module(...)
oo.class(_M,Applet)
function init(self) self.inhibited=true; return self end
function isBuiltInSCRunning() return false end
function squeezecenterStartupCheck(...) return false end
function startSqueezeCenter(...) return false end
function restartServer(...) return false end
function _startServerWindow(...) return false end
function _writeSCPrefsFile(...) return false end
function _wipeRescan(...) return false end
function _wipeTimer(...) return false end
function switchServerToDifferentMedia(...) return false end
function _squeezecenterAction(...) return false end
function mmSqueezeCenterMenu(...) return false end
function settingsShow(...) return false end
function stopSqueezeCenter(...) return false end
function mmStopSqueezeCenter(...) return false end
return _M
