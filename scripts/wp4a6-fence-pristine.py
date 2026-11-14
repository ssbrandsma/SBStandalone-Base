#!/usr/bin/env python3
"""WP4a.6: restore factory carrier bytes after the live fence install is
verified in device logs. The fence persists in the running Jive VM;
on-disk factory files return to pristine (receipted). Keeps the fence
Lua modules, dormant Radio guards and arm marker untouched.
"""
import importlib.util
import json
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))
spec = importlib.util.spec_from_file_location(
    "life", str(REPO / "scripts/fab4-lifecycle.py"))
life = importlib.util.module_from_spec(spec)
spec.loader.exec_module(life)

CARRIERS = [
    "/usr/share/jive/applets/WQVGAsmallSkin/WQVGAsmallSkinApplet.lua",
    "/usr/share/jive/applets/WQVGAlargeSkin/WQVGAlargeSkinApplet.lua",
    "/usr/share/jive/applets/ImageViewer/ImageViewerApplet.lua",
    "/usr/share/jive/applets/SetupWallpaper/SetupWallpaperApplet.lua",
    "/usr/share/jive/applets/CustomizeHomeMenu/CustomizeHomeMenuApplet.lua",
    "/usr/share/jive/applets/SetupSoundEffects/SetupSoundEffectsApplet.lua",
]


def main():
    d = life.Device(os.environ.get("SB_DEVICE_HOST", "192.168.1.244"))

    def call(cmd, timeout=90):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read().decode(errors="replace")

    try:
        receipt = json.loads(call("cat /root/wp4a6-fence-receipt.json"))
        want = {k: v.get("live_md5")
                for k, v in receipt.get("files", {}).items()}
        for remote in CARRIERS:
            if remote not in want:
                print(f"skip (not a carrier): {remote}")
                continue
            orig = Path(remote).name + ".orig"
            out = call(f"cp -p /root/wp4a6-backup/{orig} {remote} && "
                       f"md5sum {remote}").strip()
            print(out)
            if out.split()[0] != want[remote]:
                raise SystemExit(f"pristine md5 mismatch for {remote}")
        print("factory carriers pristine")
    finally:
        d.close()


if __name__ == "__main__":
    main()
