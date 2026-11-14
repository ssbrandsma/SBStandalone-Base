#!/usr/bin/env python3
"""WP4a.6 fence V6 build 2: in-place V6 reload with full silencing.

Re-uploads PlaybackFenceV6.lua (build 2: media-action watcher,
per-tick status stamp, silenced-layer emitter guards) and refreshes
the ImageViewer carrier. The carrier captures the old V6 instance,
busts its package.loaded entry for a fresh load, installs the new
build, and only then silences V6-old, V5, V3 and the plain
PlaybackFence instance (the fresh-install path the V6 carrier
missed). If the new build refuses, every older instance is left
untouched.

Never restarts Jive. Extends receipt.
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
BACKUP = "/root/wp4a6-backup"
MARKER = "/var/run/standalonebase/music-fence.enabled"

LOUD_SPLICE_V6B = ('-- SB-WP4A6-FENCE-BEGIN (transient carrier; see receipt)\n'
                   'local _sbFenceOk, _sbFenceResult = pcall(function()\n'
                   '    local olds = {}\n'
                   '    for _, mod in ipairs({"applets.StandaloneBase.PlaybackFence",\n'
                   '        "applets.StandaloneBase.PlaybackFenceV5",\n'
                   '        "applets.StandaloneBase.PlaybackFenceV3",\n'
                   '        "applets.StandaloneBase.PlaybackFenceV6"}) do\n'
                   '        local okm, Vm = pcall(require, mod)\n'
                   '        if okm and type(Vm) == "table" and type(Vm.install) == "function" then\n'
                   '            local okI, inst = pcall(function() return Vm.install() end)\n'
                   '            if okI and type(inst) == "table" then\n'
                   '                olds[#olds + 1] = inst\n'
                   '            end\n'
                   '        end\n'
                   '    end\n'
                   '    package.loaded["applets.StandaloneBase.PlaybackFenceV6"] = nil\n'
                   '    local V6 = require("applets.StandaloneBase.PlaybackFenceV6")\n'
                   '    local inst6, err6 = V6.install()\n'
                   '    if not inst6 then\n'
                   '        return nil, "v6b_refused:" .. tostring(err6)\n'
                   '    end\n'
                   '    for _, inst in ipairs(olds) do\n'
                   '        if inst ~= inst6 then\n'
                   '            inst.timerOk = false\n'
                   '            inst.joining = false\n'
                   '            if inst.client and type(inst.client.shutdown) == "function" then\n'
                   '                pcall(function() inst.client:shutdown() end)\n'
                   '            end\n'
                   '            inst.client = nil\n'
                   '        end\n'
                   '    end\n'
                   '    return true\n'
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
    src = REPO / "applet/PlaybackFenceV6.lua"
    text = src.read_text()
    if "M.VERSION = 6" not in text or "watchActions" not in text \
            or "M.BUILD = 14" not in text:
        raise SystemExit("repo V6 is not build 14; refusing")
    luac_check(src)
    d = life.Device(DEVICE)

    def call(cmd, timeout=90):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read().decode(errors="replace")

    def call_bytes(cmd, timeout=120):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read()

    try:
        receipt = json.loads(call(f"cat {RECEIPT}"))
        if call(f"test -f {MARKER} && echo yes").strip() != "yes":
            raise SystemExit("fence marker absent; refusing")
        data = src.read_bytes()
        d.call(f"cat > {BASE_APPLET}/PlaybackFenceV6.lua", data=data)
        got = call(f"md5sum {BASE_APPLET}/PlaybackFenceV6.lua").split()[0]
        if got != md5_bytes(data):
            raise SystemExit("V6b upload md5 mismatch")
        receipt["files"][f"{BASE_APPLET}/PlaybackFenceV6.lua"] = {
            "uploaded_md5": got, "uploaded_size": len(data)}
        print(f"uploaded PlaybackFenceV6 build 2 {got[:12]}")
        pristine = f"{BACKUP}/ImageViewerApplet.lua.orig"
        orig = call_bytes(f"cat {pristine}")
        want_live = receipt["files"][IMAGEVIEWER]["live_md5"]
        if md5_bytes(orig) != want_live:
            raise SystemExit("carrier backup is not pristine; aborting")
        eol = b"\r\n" if orig.count(b"\r\n") > orig.count(b"\n") / 2 \
            else b"\n"
        if orig.count(ANCHOR) != 1:
            raise SystemExit("ImageViewer anchor not unique")
        splice = LOUD_SPLICE_V6B.replace("\n", eol.decode()).encode()
        patched = orig.replace(ANCHOR, splice + ANCHOR)
        name = Path(IMAGEVIEWER).name
        tmp = work / (name + ".v6b")
        tmp.write_bytes(patched)
        snippet = work / (name + ".v6b.snippet")
        snippet.write_bytes(splice)
        luac_check(snippet)
        d.call(f"cat > {IMAGEVIEWER}", data=patched)
        got = call(f"md5sum {IMAGEVIEWER}").split()[0]
        if got != md5_bytes(patched):
            raise SystemExit("V6b splice mismatch")
        receipt["files"][IMAGEVIEWER]["v6b_md5"] = got
        print(f"spliced {name} -> V6b {got[:12]}")

        receipt.setdefault("steps", []).append("fence_v6b")
        receipt["v6b_at"] = datetime.now(timezone.utc).isoformat()
        blob = json.dumps(receipt, indent=2)
        d.call(f"cat > {RECEIPT}", data=blob.encode())
        (work / "receipt.json").write_text(blob)
        print("V6b ready: open ImageViewer once to install")
    finally:
        d.close()


if __name__ == "__main__":
    main()
