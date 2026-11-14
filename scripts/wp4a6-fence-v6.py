#!/usr/bin/env python3
"""WP4a.6 fence V6: live upgrade over the running V5 instance.

Uploads PlaybackFenceV6.lua (V5 + tested pause bridge: Player
pause/unpause wraps, Bayeux slim pause forward, grant hold across the
pause disconnect, stop-while-paused reap), restores the ImageViewer
carrier to pristine from backup, and fresh-splices it with the V6
carrier. The carrier installs V6 first (outer wraps + fresh adapter
join) and only then silences V5/V3 (tick off, adapter faulted, wraps
kept as consistent inner layers). If V6 refuses, older instances are
left untouched.

V6 proves itself live by stamping /tmp/standalonebase/fence.version
(version=6) at install; the deploy prober checks that stamp plus the
SBOWN1 incarnation bump. Never restarts Jive. Extends receipt.
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
# Known-good live V5 (repo + device verified identical before upgrade).
V5_MD5 = "6a778a40b28f03f04830f6b9f709ae35"

LOUD_SPLICE_V6 = ('-- SB-WP4A6-FENCE-BEGIN (transient carrier; see receipt)\n'
                  'local _sbFenceOk, _sbFenceResult = pcall(function()\n'
                  '    local V6 = require("applets.StandaloneBase.PlaybackFenceV6")\n'
                  '    local inst6, err6 = V6.install()\n'
                  '    if not inst6 then\n'
                  '        return nil, "v6_refused:" .. tostring(err6)\n'
                  '    end\n'
                  '    for _, mod in ipairs({"applets.StandaloneBase.PlaybackFenceV5",\n'
                  '        "applets.StandaloneBase.PlaybackFenceV3"}) do\n'
                  '        local okm, Vm = pcall(require, mod)\n'
                  '        if okm and type(Vm) == "table" and type(Vm.install) == "function" then\n'
                  '            local okI, inst = pcall(function() return Vm.install() end)\n'
                  '            if okI and type(inst) == "table" and inst ~= inst6 then\n'
                  '                inst.timerOk = false\n'
                  '                inst.joining = false\n'
                  '                if inst.client and type(inst.client.shutdown) == "function" then\n'
                  '                    pcall(function() inst.client:shutdown() end)\n'
                  '                end\n'
                  '                inst.client = nil\n'
                  '            end\n'
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
    if "M.VERSION = 6" not in text or "playerPause" not in text:
        raise SystemExit("repo V6 is not the pause-bridge build; refusing")
    v5 = REPO / "applet/PlaybackFenceV5.lua"
    if md5_bytes(v5.read_bytes()) != V5_MD5:
        raise SystemExit("repo V5 drifted; refusing")
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
        # Guard: live V5 must be the known build.
        live_v5 = call(f"md5sum {BASE_APPLET}/PlaybackFenceV5.lua").split()[0]
        if live_v5 != V5_MD5:
            raise SystemExit(f"live V5 {live_v5[:12]} is not {V5_MD5[:12]}")
        if call(f"test -f {MARKER} && echo yes").strip() != "yes":
            raise SystemExit("fence marker absent; refusing")
        # Upload V6.
        data = src.read_bytes()
        d.call(f"cat > {BASE_APPLET}/PlaybackFenceV6.lua", data=data)
        got = call(f"md5sum {BASE_APPLET}/PlaybackFenceV6.lua").split()[0]
        if got != md5_bytes(data):
            raise SystemExit("V6 upload md5 mismatch")
        receipt["files"][f"{BASE_APPLET}/PlaybackFenceV6.lua"] = {
            "uploaded_md5": got, "uploaded_size": len(data)}
        print(f"uploaded PlaybackFenceV6 {got[:12]}")
        # Restore carrier to pristine, then splice V6.
        pristine = f"{BACKUP}/ImageViewerApplet.lua.orig"
        orig = call_bytes(f"cat {pristine}")
        want_live = receipt["files"][IMAGEVIEWER]["live_md5"]
        if md5_bytes(orig) != want_live:
            raise SystemExit("carrier backup is not pristine; aborting")
        d.call(f"cat > {IMAGEVIEWER}", data=orig)
        got = call(f"md5sum {IMAGEVIEWER}").split()[0]
        if got != want_live:
            raise SystemExit("carrier restore mismatch")
        print(f"restored carrier pristine {got[:12]}")
        eol = b"\r\n" if orig.count(b"\r\n") > orig.count(b"\n") / 2 \
            else b"\n"
        if orig.count(ANCHOR) != 1:
            raise SystemExit("ImageViewer anchor not unique")
        splice = LOUD_SPLICE_V6.replace("\n", eol.decode()).encode()
        patched = orig.replace(ANCHOR, splice + ANCHOR)
        name = Path(IMAGEVIEWER).name
        tmp = work / (name + ".v6")
        tmp.write_bytes(patched)
        snippet = work / (name + ".v6.snippet")
        snippet.write_bytes(splice)
        luac_check(snippet)
        d.call(f"cat > {IMAGEVIEWER}", data=patched)
        got = call(f"md5sum {IMAGEVIEWER}").split()[0]
        if got != md5_bytes(patched):
            raise SystemExit("V6 splice mismatch")
        receipt["files"][IMAGEVIEWER]["v6_md5"] = got
        print(f"spliced {name} -> V6 {got[:12]}")

        receipt.setdefault("steps", []).append("fence_v6")
        receipt["v6_at"] = datetime.now(timezone.utc).isoformat()
        blob = json.dumps(receipt, indent=2)
        d.call(f"cat > {RECEIPT}", data=blob.encode())
        (work / "receipt.json").write_text(blob)
        print("V6 ready: open ImageViewer once to install")
    finally:
        d.close()


if __name__ == "__main__":
    main()
