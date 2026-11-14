#!/usr/bin/env python3
"""WP4a.6 fence V4: silencer for the superseded V2 instance.

Uploads PlaybackFenceV4.lua and fresh-splices ImageViewer (reusable:
freed on close). V4 retires V2's tick/adapter/music-gate; V2's class
wraps stay as consistent inner layers and V3 is untouched. Never
restarts Jive. Extends receipt.
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
IMAGEVIEWER = "/usr/share/jive/applets/ImageViewer/ImageViewerApplet.lua"

LOUD_SPLICE_V4 = ('-- SB-WP4A6-FENCE-BEGIN (transient carrier; see receipt)\n'
                  'local _sbFenceOk, _sbFenceResult = pcall(function()\n'
                  '    return require("applets.StandaloneBase.PlaybackFenceV4").install()\n'
                  'end)\n'
                  'pcall(function()\n'
                  '    require("jive.utils.log").logger("StandaloneBase"):info(\n'
                  '        "SB-WP4A6 splice ok=", tostring(_sbFenceOk),\n'
                  '        " result=", tostring(_sbFenceResult))\n'
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
        src = REPO / "applet/PlaybackFenceV4.lua"
        luac_check(src)
        data = src.read_bytes()
        d.call(f"cat > {BASE_APPLET}/PlaybackFenceV4.lua", data=data)
        got = call(f"md5sum {BASE_APPLET}/PlaybackFenceV4.lua").split()[0]
        if got != md5_bytes(data):
            raise SystemExit("V4 upload md5 mismatch")
        receipt["files"][f"{BASE_APPLET}/PlaybackFenceV4.lua"] = {
            "uploaded_md5": got, "uploaded_size": len(data)}
        print(f"uploaded PlaybackFenceV4 {got[:12]}")

        data = call_bytes(f"cat {IMAGEVIEWER}")
        name = Path(IMAGEVIEWER).name
        if b"SB-WP4A6-FENCE" in data:
            raise SystemExit("splice already present in ImageViewer")
        if md5_bytes(data) != receipt["files"][IMAGEVIEWER]["live_md5"]:
            raise SystemExit("ImageViewer not pristine; aborting")
        eol = b"\r\n" if data.count(b"\r\n") > data.count(b"\n") / 2 \
            else b"\n"
        if data.count(ANCHOR) != 1:
            raise SystemExit("ImageViewer anchor not unique")
        splice = LOUD_SPLICE_V4.replace("\n", eol.decode()).encode()
        patched = data.replace(ANCHOR, splice + ANCHOR)
        tmp = work / (name + ".v4")
        tmp.write_bytes(patched)
        snippet = work / (name + ".v4.snippet")
        snippet.write_bytes(splice)
        luac_check(snippet)
        d.call(f"cat > {IMAGEVIEWER}", data=patched)
        got = call(f"md5sum {IMAGEVIEWER}").split()[0]
        if got != md5_bytes(patched):
            raise SystemExit("V4 splice mismatch")
        receipt["files"][IMAGEVIEWER]["v4_md5"] = got
        print(f"spliced {name} -> V4 {got[:12]}")

        receipt.setdefault("steps", []).append("fence_v4")
        receipt["v4_at"] = datetime.now(timezone.utc).isoformat()
        blob = json.dumps(receipt, indent=2)
        d.call(f"cat > {RECEIPT}", data=blob.encode())
        (work / "receipt.json").write_text(blob)
        print("V4 ready")
    finally:
        d.close()


if __name__ == "__main__":
    main()
