#!/usr/bin/env python3
"""WP4a.6 fresh fence install for a rebooted (fresh-VM) Touch.

Overwrites the stale on-disk PlaybackFence.lua (never loaded in this
VM) with the tested chain-aware fence, uploads the matching
OwnershipHooks (reinstall/adopt), re-creates the marker gate, and
splices ImageViewer (uncached after reboot, so it loads on open) with
the canonical install. No silencing needed: no older instance exists.
Restores the carrier after the install is verified. Receipted.
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
MARKER = "/var/run/standalonebase/music-fence.enabled"
BACKUP = "/root/wp4a6-backup"
# Chain-aware fence (Phase J): adopts Radio's per-start re-wraps instead
# of error-spamming. Install path byte-identical in behavior to V5.
FENCE_MD5 = "6a778a40b28f03f04830f6b9f709ae35"
HOOKS_MD5 = "6741a0a142e63ae721b3a0a839fd9a18"

SPLICE = ('-- SB-WP4A6-FENCE-BEGIN (transient carrier; see receipt)\n'
          'local _sbFenceOk, _sbFenceResult = pcall(function()\n'
          '    return require("applets.StandaloneBase.PlaybackFence").install()\n'
          'end)\n'
          'pcall(function()\n'
          '    require("jive.utils.log").logger("StandaloneBase"):info(\n'
          '        "SB-WP4A6 fresh ok=", tostring(_sbFenceOk),\n'
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
    src = REPO / "applet/PlaybackFence.lua"
    if md5_bytes(src.read_bytes()) != FENCE_MD5:
        raise SystemExit("repo fence is not the tested chain-aware build; refusing")
    hooks = REPO / "applet/OwnershipHooks.lua"
    if md5_bytes(hooks.read_bytes()) != HOOKS_MD5:
        raise SystemExit("repo hooks lack reinstall; refusing")
    v5 = REPO / "applet/PlaybackFenceV5.lua"
    if md5_bytes(v5.read_bytes()) != FENCE_MD5:
        raise SystemExit("repo V5 drifted from the fence; refusing")
    d = life.Device(DEVICE)

    def call(cmd, timeout=90):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read().decode(errors="replace")

    def call_bytes(cmd, timeout=120):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read()

    try:
        receipt = json.loads(call(f"cat {RECEIPT}"))
        for name, path, want in (
                ("PlaybackFence.lua", src, FENCE_MD5),
                ("PlaybackFenceV5.lua", v5, FENCE_MD5),
                ("OwnershipHooks.lua", hooks, HOOKS_MD5)):
            luac_check(path)
            data = path.read_bytes()
            call(f"cp -p {BASE_APPLET}/{name} "
                 f"{BACKUP}/{name}.v5orig")
            d.call(f"cat > {BASE_APPLET}/{name}", data=data)
            got = call(f"md5sum {BASE_APPLET}/{name}").split()[0]
            if got != want:
                raise SystemExit(f"fence upload md5 mismatch for {name}")
            receipt["files"][f"{BASE_APPLET}/{name}"] = {
                "fresh_chain_md5": got, "uploaded_size": len(data)}
            print(f"uploaded fresh {name} {got[:12]}")
        call(f": > {MARKER}")
        if call(f"test -f {MARKER} && echo yes").strip() != "yes":
            raise SystemExit("marker not created")

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
        splice = SPLICE.replace("\n", eol.decode()).encode()
        patched = data.replace(ANCHOR, splice + ANCHOR)
        (work / (name + ".fresh")).write_bytes(patched)
        snippet = work / (name + ".fresh.snippet")
        snippet.write_bytes(splice)
        luac_check(snippet)
        d.call(f"cat > {IMAGEVIEWER}", data=patched)
        got = call(f"md5sum {IMAGEVIEWER}").split()[0]
        if got != md5_bytes(patched):
            raise SystemExit("fresh splice mismatch")
        receipt["files"][IMAGEVIEWER]["fresh_md5"] = got
        print(f"spliced {name} -> fresh {got[:12]}")

        receipt.setdefault("steps", []).append("fence_fresh")
        receipt["fresh_at"] = datetime.now(timezone.utc).isoformat()
        blob = json.dumps(receipt, indent=2)
        d.call(f"cat > {RECEIPT}", data=blob.encode())
        (work / "receipt.json").write_text(blob)
        print("fresh fence ready")
    finally:
        d.close()


if __name__ == "__main__":
    main()
