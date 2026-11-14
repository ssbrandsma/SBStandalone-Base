#!/usr/bin/env python3
"""WP4a.6 fence V2: fixed class resolution (instance-metatable first).

V1 refused on-device with unsupported_radio_revision: the live Radio
module uses module() so its methods hide on a local class table. The V1
chunk is cached in the running VM under its module name, so the fixed
code deploys as a NEW module (PlaybackFenceV2) and the reusable
ImageViewer carrier is re-pointed at it. SetupSoundEffects is added as
a second carrier. Never restarts Jive. Extends receipt.
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
BACKUP = "/root/wp4a6-backup"
RECEIPT = "/root/wp4a6-fence-receipt.json"

IMAGEVIEWER = "/usr/share/jive/applets/ImageViewer/ImageViewerApplet.lua"
SOUNDFX = ("/usr/share/jive/applets/SetupSoundEffects/"
           "SetupSoundEffectsApplet.lua")

LOUD_SPLICE_V2 = ('-- SB-WP4A6-FENCE-BEGIN (transient carrier; see receipt)\n'
                  'local _sbFenceOk, _sbFenceResult = pcall(function()\n'
                  '    return require("applets.StandaloneBase.PlaybackFenceV2").install()\n'
                  'end)\n'
                  'pcall(function()\n'
                  '    require("jive.utils.log").logger("StandaloneBase"):info(\n'
                  '        "SB-WP4A6 splice ok=", tostring(_sbFenceOk),\n'
                  '        " result=", tostring(_sbFenceResult))\n'
                  'end)\n'
                  '-- SB-WP4A6-FENCE-END\n')

ANCHOR = b"module(..., Framework.constants)"
V1_REQUIRE = b"applets.StandaloneBase.PlaybackFence"
V2_REQUIRE = b"applets.StandaloneBase.PlaybackFenceV2"


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
        if not isinstance(receipt.get("files"), dict):
            raise SystemExit("deploy receipt missing; run deploy first")

        # 1. Upload fixed fence under the V2 module name.
        src = REPO / "applet/PlaybackFence.lua"
        luac_check(src)
        data = src.read_bytes()
        d.call(f"cat > {BASE_APPLET}/PlaybackFenceV2.lua", data=data)
        got = call(f"md5sum {BASE_APPLET}/PlaybackFenceV2.lua").split()[0]
        if got != md5_bytes(data):
            raise SystemExit("V2 upload md5 mismatch")
        receipt["files"][f"{BASE_APPLET}/PlaybackFenceV2.lua"] = {
            "uploaded_md5": got, "uploaded_size": len(data)}
        print(f"uploaded PlaybackFenceV2 {got[:12]}")

        # 2. Re-point ImageViewer (freed on close; reopens load fresh).
        data = call_bytes(f"cat {IMAGEVIEWER}")
        if data.count(V1_REQUIRE) != 1 or b"SB-WP4A6-FENCE" not in data:
            raise SystemExit("ImageViewer V1 splice not found once")
        patched = data.replace(V1_REQUIRE, V2_REQUIRE)
        tmp = work / "ImageViewerApplet.lua.v2"
        tmp.write_bytes(patched)
        eol = b"\r\n" if data.count(b"\r\n") > data.count(b"\n") / 2 \
            else b"\n"
        snippet = work / "ImageViewerApplet.lua.v2.snippet"
        snippet.write_bytes(
            LOUD_SPLICE_V2.replace("\n", eol.decode()).encode())
        luac_check(snippet)
        d.call(f"cat > {IMAGEVIEWER}", data=patched)
        got = call(f"md5sum {IMAGEVIEWER}").split()[0]
        if got != md5_bytes(patched):
            raise SystemExit("ImageViewer V2 splice mismatch")
        receipt["files"][IMAGEVIEWER]["v2_md5"] = got
        print(f"imageviewer -> V2 {got[:12]}")

        # 3. Fresh V2 splice into SetupSoundEffects.
        data = call_bytes(f"cat {SOUNDFX}")
        assert data, "empty soundfx download"
        (work / Path(SOUNDFX).name).write_bytes(data)
        if b"SB-WP4A6-FENCE" in data:
            raise SystemExit("splice already present in soundfx")
        name = Path(SOUNDFX).name
        print(call(f"cp -p {SOUNDFX} {BACKUP}/{name}.orig && "
                   f"md5sum {BACKUP}/{name}.orig").strip())
        receipt["files"][SOUNDFX] = {"live_md5": md5_bytes(data),
                                     "live_size": len(data)}
        eol = b"\r\n" if data.count(b"\r\n") > data.count(b"\n") / 2 \
            else b"\n"
        if data.count(ANCHOR) != 1:
            raise SystemExit("soundfx anchor not unique")
        splice = LOUD_SPLICE_V2.replace("\n", eol.decode()).encode()
        patched = data.replace(ANCHOR, splice + ANCHOR)
        tmp = work / (name + ".spliced")
        tmp.write_bytes(patched)
        snippet = work / (name + ".snippet")
        snippet.write_bytes(splice)
        luac_check(snippet)
        d.call(f"cat > {SOUNDFX}", data=patched)
        got = call(f"md5sum {SOUNDFX}").split()[0]
        if got != md5_bytes(patched):
            raise SystemExit("soundfx splice mismatch")
        receipt["files"][SOUNDFX]["spliced_md5"] = got
        print(f"spliced soundfx {got[:12]}")

        receipt.setdefault("steps", []).append("fence_v2")
        receipt["v2_at"] = datetime.now(timezone.utc).isoformat()
        blob = json.dumps(receipt, indent=2)
        d.call(f"cat > {RECEIPT}", data=blob.encode())
        (work / "receipt.json").write_text(blob)
        print("V2 ready")
    finally:
        d.close()


if __name__ == "__main__":
    main()
