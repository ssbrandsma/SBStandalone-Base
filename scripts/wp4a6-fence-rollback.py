#!/usr/bin/env python3
"""WP4a.6 fence rollback: instant passive + byte-exact restore.

  1. Remove the arm marker (fence wrappers pass through immediately).
  2. Restore *.orig backups over spliced skins + dormant Radio file,
     verifying md5 against the deploy receipt.
  3. Remove the uploaded StandaloneBase Lua modules (they did not exist
     before deploy; verified absent in the pre-deploy listing).
  4. Write /root/wp4a6-fence-rollback.json (+ local copy).

Never restarts Jive. Installed wrappers stay in the loaded VM but are
passive without the marker (documented residual; full removal needs a
future authorized Jive restart).
"""
import importlib.util
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))
spec = importlib.util.spec_from_file_location(
    "life", str(REPO / "scripts/fab4-lifecycle.py"))
life = importlib.util.module_from_spec(spec)
spec.loader.exec_module(life)

DEVICE = os.environ.get("SB_DEVICE_HOST", "192.168.1.244")
BASE_APPLET = "/usr/share/jive/applets/StandaloneBase"
BACKUP = "/root/wp4a6-backup"
MARKER = "/var/run/standalonebase/music-fence.enabled"

RESTORE = [
    ("/usr/share/jive/applets/WQVGAsmallSkin/WQVGAsmallSkinApplet.lua",
     "WQVGAsmallSkinApplet.lua.orig"),
    ("/usr/share/jive/applets/WQVGAlargeSkin/WQVGAlargeSkinApplet.lua",
     "WQVGAlargeSkinApplet.lua.orig"),
    ("/usr/share/jive/applets/ImageViewer/ImageViewerApplet.lua",
     "ImageViewerApplet.lua.orig"),
    ("/usr/share/jive/applets/SetupWallpaper/SetupWallpaperApplet.lua",
     "SetupWallpaperApplet.lua.orig"),
    ("/usr/share/jive/applets/CustomizeHomeMenu/CustomizeHomeMenuApplet.lua",
     "CustomizeHomeMenuApplet.lua.orig"),
    ("/usr/share/jive/applets/SetupSoundEffects/SetupSoundEffectsApplet.lua",
     "SetupSoundEffectsApplet.lua.orig"),
    ("/usr/share/jive/applets/StandaloneRadio/StreamPlayer.lua",
     "StreamPlayer.lua.orig"),
]
REMOVE = ["PlaybackFence.lua", "PlaybackFenceV2.lua", "PlaybackFenceV3.lua",
          "PlaybackFenceV4.lua", "OwnershipClient.lua",
          "OwnershipHttp.lua", "OwnershipRuntime.lua", "OwnershipHooks.lua"]


def main():
    d = life.Device(DEVICE)

    def call(cmd, timeout=90):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read().decode(errors="replace")

    try:
        log = {"tool": "wp4a6-fence-rollback.py", "device": DEVICE,
               "started": datetime.now(timezone.utc).isoformat(),
               "steps": []}
        print(call(f"rm -f {MARKER}; ls {MARKER} 2>&1 || echo MARKER_GONE"))
        log["steps"].append("marker_removed")
        receipt_raw = call(f"cat /root/wp4a6-fence-receipt.json")
        receipt = json.loads(receipt_raw) if receipt_raw.strip() else {}
        want = {k: v.get("live_md5")
                for k, v in receipt.get("files", {}).items()}
        for remote, orig in RESTORE:
            if remote not in want:
                print(f"skip (never deployed): {remote}")
                log["steps"].append(f"skipped:{orig}")
                continue
            out = call(f"cp -p {BACKUP}/{orig} {remote} && "
                       f"md5sum {remote}").strip()
            print(out)
            got = out.split()[0]
            if want.get(remote) and got != want[remote]:
                raise SystemExit(f"restore md5 mismatch for {remote}: "
                                 f"{got} != {want[remote]}")
            log["steps"].append(f"restored:{orig}:{got[:12]}")
        for name in REMOVE:
            out = call(f"rm -f {BASE_APPLET}/{name} && "
                       f"ls {BASE_APPLET}/{name} 2>&1 || echo GONE:{name}")
            print(out.strip().splitlines()[-1])
            log["steps"].append(f"removed:{name}")
        log["finished"] = datetime.now(timezone.utc).isoformat()
        (REPO / ".local/wp4a6/deploy/rollback.json").write_text(
            json.dumps(log, indent=2))
        print("rollback complete")
    finally:
        d.close()


if __name__ == "__main__":
    main()
