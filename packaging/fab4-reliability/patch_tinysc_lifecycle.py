"""Generate the minimal opt-in overlay from qualified stock 7.7.3 sources.

Build-only: writes the supplied output directory, never deploys or runs services.
"""
import argparse
import hashlib
from pathlib import Path

APPLET_SHA256 = "526973a57d2587cd86359dd0eded3a42744d142f2dbff3a1b199f5a77fec9dc6"
SERVICE_SHA256 = "ae913ec4ba84d7b5003f08fddc2c3df15a453071784482e35f0dae2faf335c0d"
SQUEEZEPLAY_SHA256 = "8c412da36a2b0404a923fd70e6413eda4bced055fdeea444bb24c4a64dd9694c"
GUARD = '''-- Standalone Base lifecycle quarantine, independent of Music activation.
local function standaloneBaseOwnsStorage()
    local latch = io.open("/var/run/standalonebase/tinysc.latch", "r")
    if not latch then return false end
    latch:close()
    log:info("TinySC request suppressed: Standalone Base lifecycle quarantine")
    return true
end

'''


def patch_applet(raw):
    if hashlib.sha256(raw).hexdigest() != APPLET_SHA256:
        raise ValueError("Unsupported SqueezeCenter applet hash")
    text = raw.decode("utf-8")
    anchor = "function init(self)"
    if text.count(anchor) != 1:
        raise ValueError("Ambiguous applet init")
    text = text.replace(anchor, GUARD + anchor)
    functions = (
        "squeezecenterStartupCheck(self)",
        "switchServerToDifferentMedia(self, devName)",
        "udevEventHandler(self, evt, msg)",
        "_wipeRescan(self)", "_wipeTimer(self, scDrive)",
        "_startServerWindow(self)", "restartServer(self, silent)",
        "_writeSCPrefsFile(self, devName)",
    )
    for function in functions:
        anchor = "function " + function + "\n"
        if text.count(anchor) != 1:
            raise ValueError("Missing/ambiguous function: " + function)
        text = text.replace(anchor, anchor + "    if standaloneBaseOwnsStorage() then return EVENT_UNUSED end\n")
    anchor = "function _squeezecenterAction(self, icon, text, subtext, time, action, silent)\n"
    if text.count(anchor) != 1:
        raise ValueError("Missing service action boundary")
    text = text.replace(anchor, anchor + '''    if standaloneBaseOwnsStorage() then
        -- Preserve stop while avoiding stock's unchecked killByPidFile path.
        if action == "stop" then os.execute("/etc/init.d/squeezecenter stop") end
        return EVENT_UNUSED
    end
''')
    return text.encode("utf-8")


def patch_service(raw):
    if hashlib.sha256(raw).hexdigest() != SERVICE_SHA256:
        raise ValueError("Unsupported TinySC service hash")
    text = raw.decode("utf-8")
    if not text.startswith("#!/bin/sh\n") or text.count("BASE=/usr/squeezecenter\n") != 1:
        raise ValueError("Unsupported stock TinySC service")
    # Background restart must reenter the guard after its stock delay. The
    # saved original must never recursively bypass lifecycle serialization.
    return text.replace("$0 ", "/etc/init.d/squeezecenter ").encode("utf-8")


def patch_squeezeplay(raw):
    if hashlib.sha256(raw).hexdigest() != SQUEEZEPLAY_SHA256:
        raise ValueError("Unsupported SqueezePlay init hash")
    text = raw.decode("utf-8")
    guard = '''# Standalone Base maintenance preserves the firmware watchdog daemon.
SB_MAINTENANCE=/usr/share/jive/applets/StandaloneBase/sb-maintenance-helper
case "$1" in
 maintenance-stop)
    exec "$SB_MAINTENANCE" maintenance-stop "$2" "$3" ;;
 restart)
    if test -f /mnt/storage/standalonebase/tinysc-arbitration.enabled; then
        exec "$SB_MAINTENANCE" launch-restart
    fi ;;
 start|stop|stopwdog)
    if test -x "$SB_MAINTENANCE" && "$SB_MAINTENANCE" maintenance-active >/dev/null 2>&1; then
        echo "SqueezePlay maintenance is supervised; stock action refused" >&2
        exit 1
    fi
    if test "$1" = stopwdog && test -f /mnt/storage/standalonebase/tinysc-arbitration.enabled; then
        echo "Use the watchdog-safe restart coordinator" >&2
        exit 1
    fi ;;
esac

'''
    return text.replace("#!/bin/sh\n", "#!/bin/sh\n" + guard, 1).encode("utf-8")


def patch_base(raw, recovery=False):
    """Patch the installed Base applet, preserving its unrelated exact source."""
    text = raw.decode("utf-8")

    def replace(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise ValueError("Unsupported/ambiguous installed Base applet anchor: " + old[:70])
        text = text.replace(old, new, 1)

    anchor = 'local AlarmPlayback = require("applets.StandaloneBase.AlarmPlayback")\n'
    replace(anchor, anchor + 'local LifecycleHeartbeat = require("applets.StandaloneBase.LifecycleHeartbeat")\n')
    if not recovery:
        anchor = '    if platform.name == "fab4" then\n        local conflicts=Platform.portConflicts()'
        replace(anchor, '''    local arbitration=false
    if platform.name == "fab4" then
        local policy=io.open("/mnt/storage/standalonebase/tinysc-arbitration.enabled","r")
        if policy then policy:close(); arbitration=true end
    end
    if platform.name == "fab4" and not arbitration then
        local conflicts=Platform.portConflicts()''')
        replace('    started=true\n', '''    started=true
    if arbitration then
        os.execute("mkdir -m 700 /var/run/standalonebase >/dev/null 2>&1")
        os.remove("/var/run/standalonebase/heartbeat.stop")
    end
''')
    anchor = '    self.monitor=Timer(15000,function() self:supervise() end); self.monitor:start()\n'
    addition = '''        self.lifecycleHeartbeat=LifecycleHeartbeat.new(Framework,Timer)
        self.lifecycleHeartbeat:start()
'''
    if not recovery:
        addition = '    if arbitration then\n' + addition + '''        self.lifecycleGuardTimer=Timer(1000,function()
            self.lifecycleGuardTimer=nil
            local latch=io.open("/var/run/standalonebase/tinysc.latch","r")
            if latch then
                latch:close()
                pcall(function() appletManager:callService("SCGuardianTimer","stop") end)
            end
        end,true)
        self.lifecycleGuardTimer:start()
    end
'''
    replace(anchor, anchor + addition)
    replace('function free(self)\n', '''function free(self)
    if self.lifecycleHeartbeat then self.lifecycleHeartbeat:stop(); self.lifecycleHeartbeat=nil end
    if self.lifecycleGuardTimer then self.lifecycleGuardTimer:stop(); self.lifecycleGuardTimer=nil end
''')
    return text.encode("utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--applet", type=Path, required=True)
    parser.add_argument("--service", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--squeezeplay", type=Path)
    args = parser.parse_args()
    applet = patch_applet(args.applet.read_bytes())
    service = patch_service(args.service.read_bytes())
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "SqueezeCenterApplet.lua").write_bytes(applet)
    (args.out / "squeezecenter.stock").write_bytes(service)
    (args.out / "squeezecenter").write_text(
        '#!/bin/sh\nexec /usr/share/jive/applets/StandaloneBase/sb-tinysc-control service "$@"\n',
        encoding="utf-8", newline="\n")
    if args.squeezeplay:
        (args.out / "squeezeplay").write_bytes(patch_squeezeplay(args.squeezeplay.read_bytes()))


if __name__ == "__main__":
    main()
