#!/usr/bin/env python3
"""WP4a.6 remote radio stop: upload RadioStop.lua, splice three carriers.

ImageViewer is cached in the VM (capture splice never fired), so splice
SetupSoundEffects + SetupWallpaper + CustomizeHomeMenu; whichever applet
the owner opens first fires the stop (idempotent). Restores carriers after
the stop is verified. Never restarts Jive. Receipted under steps/radiostop.
"""
import hashlib
import importlib.util
import json
import os
import subprocess
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
RECEIPT = "/root/wp4a6-fence-receipt.json"
CARRIERS = [
    "/usr/share/jive/applets/SetupSoundEffects/SetupSoundEffectsApplet.lua",
    "/usr/share/jive/applets/SetupWallpaper/SetupWallpaperApplet.lua",
    "/usr/share/jive/applets/CustomizeHomeMenu/CustomizeHomeMenuApplet.lua",
]

SPLICE = ('-- SB-WP4A6-FENCE-BEGIN (transient carrier; see receipt)\n'
          'local _sbStopOk, _sbStopResult = pcall(function()\n'
          '    return require("applets.StandaloneBase.RadioStop").install()\n'
          'end)\n'
          'pcall(function()\n'
          '    require("jive.utils.log").logger("StandaloneBase"):info(\n'
          '        "SB-WP4A6 radiostop ok=", tostring(_sbStopOk),\n'
          '        " result=", tostring(_sbStopResult))\n'
          'end)\n'
          '-- SB-WP4A6-FENCE-END\n')

ANCHOR = b"module(..., Framework.constants)"


def luac_check(path):
    for exe in ("luac5.1", "luac"):
        try:
            r = subprocess.run([exe, "-p", str(path)], capture_output=True,
                               text=True, timeout=60)
            if r.returncode == 0:
                return
            raise SystemExit(f"{exe} -p failed for {path}:\n{r.stderr}")
        except FileNotFoundError:
            continue
    raise SystemExit("no luac available for syntax check")


def md5_bytes(data):
    return hashlib.md5(data).hexdigest()


def main():
    work = REPO / ".local/wp4a6/deploy"
    work.mkdir(parents=True, exist_ok=True)
    d = life.Device(DEVICE)

    def call(cmd, timeout=90):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read().decode(errors="replace")

    def call_bytes(cmd, timeout=120):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read()

    try:
        receipt = json.loads(call(f"cat {RECEIPT}"))
        src = REPO / "applet/RadioStop.lua"
        luac_check(src)
        data = src.read_bytes()
        d.call(f"cat > {BASE_APPLET}/RadioStop.lua", data=data)
        got = call(f"md5sum {BASE_APPLET}/RadioStop.lua").split()[0]
        if got != md5_bytes(data):
            raise SystemExit("radiostop upload md5 mismatch")
        receipt["files"][f"{BASE_APPLET}/RadioStop.lua"] = {
            "uploaded_md5": got, "uploaded_size": len(data)}
        print(f"uploaded RadioStop {got[:12]}")

        for remote in CARRIERS:
            name = Path(remote).name
            data = call_bytes(f"cat {remote}")
            if b"SB-WP4A6-FENCE" in data:
                raise SystemExit(f"splice already present in {name}")
            if md5_bytes(data) != receipt["files"][remote]["live_md5"]:
                raise SystemExit(f"{name} not pristine; aborting")
            eol = b"\r\n" if data.count(b"\r\n") > data.count(b"\n") / 2 \
                else b"\n"
            if data.count(ANCHOR) != 1:
                raise SystemExit(f"{name} anchor not unique")
            splice = SPLICE.replace("\n", eol.decode()).encode()
            patched = data.replace(ANCHOR, splice + ANCHOR)
            (work / (name + ".radiostop")).write_bytes(patched)
            snippet = work / (name + ".radiostop.snippet")
            snippet.write_bytes(splice)
            luac_check(snippet)
            d.call(f"cat > {remote}", data=patched)
            got = call(f"md5sum {remote}").split()[0]
            if got != md5_bytes(patched):
                raise SystemExit(f"{name} splice mismatch")
            receipt["files"][remote]["radiostop_md5"] = got
            print(f"spliced {name} -> radiostop {got[:12]}")

        receipt.setdefault("steps", []).append("radiostop_armed")
        receipt["radiostop_at"] = datetime.now(timezone.utc).isoformat()
        blob = json.dumps(receipt, indent=2)
        d.call(f"cat > {RECEIPT}", data=blob.encode())
        (work / "receipt.json").write_text(blob)
        print("radiostop ready")
    finally:
        d.close()


if __name__ == "__main__":
    main()
