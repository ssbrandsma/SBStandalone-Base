#!/usr/bin/env python3
"""WP4a.6 fence V6 build 10: alternate carrier via SpotifyConnect.

Same transient-carrier mechanism as wp4a6-fence-v6b.py (ImageViewer),
but spliced into SpotifyConnectApplet.lua for sessions where the
ImageViewer menu is unreachable. The carrier runs once when Jive
fresh-loads the Spotify applet, installs the repo PlaybackFenceV6
(build 10), and silences older instances.

Usage:
  wp4a6-fence-v6c.py          upload V6 + splice carrier, print next step
  wp4a6-fence-v6c.py restore  restore pristine Spotify file after install

Never restarts Jive. Extends receipt. Restores Spotify pristine
immediately after a verified install (the install persists in memory).
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
vspec = importlib.util.spec_from_file_location(
    "v6b", str(REPO / "scripts/wp4a6-fence-v6b.py"))
v6b = importlib.util.module_from_spec(vspec)
vspec.loader.exec_module(v6b)

DEVICE = os.environ.get("SB_DEVICE_HOST", "192.168.1.244")
BASE_APPLET = "/usr/share/jive/applets/StandaloneBase"
RECEIPT = "/root/wp4a6-fence-receipt.json"
SPOTIFY = "/usr/share/jive/applets/SpotifyConnect/SpotifyConnectApplet.lua"
BACKUP = "/root/wp4a6-backup"
MARKER = "/var/run/standalonebase/music-fence.enabled"
SPLICE_MARK = b"SB-WP4A6-FENCE-BEGIN"


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
            or "M.BUILD = 10" not in text:
        raise SystemExit("repo V6 is not build 10; refusing")
    luac_check(src)
    d = life.Device(DEVICE)

    def call(cmd, timeout=90):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read().decode(errors="replace")

    def call_bytes(cmd, timeout=120):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read()

    restore = len(sys.argv) > 1 and sys.argv[1] == "restore"
    try:
        receipt = json.loads(call(f"cat {RECEIPT}"))
        if call(f"test -f {MARKER} && echo yes").strip() != "yes":
            raise SystemExit("fence marker absent; refusing")
        pristine = f"{BACKUP}/SpotifyConnectApplet.lua.orig"
        if restore:
            want = receipt["files"][SPOTIFY]["live_md5"]
            orig = call_bytes(f"cat {pristine}")
            if md5_bytes(orig) != want:
                raise SystemExit("carrier backup is not pristine; aborting")
            d.call(f"cat > {SPOTIFY}", data=orig)
            got = call(f"md5sum {SPOTIFY}").split()[0]
            if got != want:
                raise SystemExit("Spotify restore md5 mismatch")
            receipt.setdefault("steps", []).append("fence_v6c_restore")
            receipt["v6c_restored_at"] = datetime.now(
                timezone.utc).isoformat()
            blob = json.dumps(receipt, indent=2)
            d.call(f"cat > {RECEIPT}", data=blob.encode())
            (work / "receipt.json").write_text(blob)
            print(f"restored Spotify pristine {got[:12]}")
            return
        data = src.read_bytes()
        d.call(f"cat > {BASE_APPLET}/PlaybackFenceV6.lua", data=data)
        got = call(f"md5sum {BASE_APPLET}/PlaybackFenceV6.lua").split()[0]
        if got != md5_bytes(data):
            raise SystemExit("V6c upload md5 mismatch")
        receipt["files"][f"{BASE_APPLET}/PlaybackFenceV6.lua"] = {
            "uploaded_md5": got, "uploaded_size": len(data)}
        print(f"uploaded PlaybackFenceV6 build 10 {got[:12]}")
        live = call_bytes(f"cat {SPOTIFY}")
        if SPLICE_MARK in live:
            raise SystemExit("Spotify already spliced; restore first")
        have_backup = call(
            f"test -f {pristine} && md5sum {pristine}").split()
        if have_backup:
            if have_backup[0] != md5_bytes(live):
                raise SystemExit("live Spotify differs from backup; aborting")
        else:
            d.call(f"cat > {pristine}", data=live)
        orig = live
        eol = b"\r\n" if orig.count(b"\r\n") > orig.count(b"\n") / 2 \
            else b"\n"
        eol_s = eol.decode()
        anchors = [eol + b"return _M" + eol, b"\nreturn _M\n"]
        anchor = next((a for a in anchors if orig.count(a) == 1), None)
        if anchor is None:
            raise SystemExit("Spotify return anchor not unique")
        splice = v6b.LOUD_SPLICE_V6B.replace("\n", eol_s).encode()
        patched = orig.replace(anchor, eol + splice + anchor)
        tmp = work / "SpotifyConnectApplet.lua.v6c"
        tmp.write_bytes(patched)
        luac_check(tmp)
        d.call(f"cat > {SPOTIFY}", data=patched)
        got = call(f"md5sum {SPOTIFY}").split()[0]
        if got != md5_bytes(patched):
            raise SystemExit("V6c splice mismatch")
        receipt["files"][SPOTIFY] = {
            "live_md5": md5_bytes(orig), "v6c_md5": got}
        receipt.setdefault("steps", []).append("fence_v6c")
        receipt["v6c_at"] = datetime.now(timezone.utc).isoformat()
        blob = json.dumps(receipt, indent=2)
        d.call(f"cat > {RECEIPT}", data=blob.encode())
        (work / "receipt.json").write_text(blob)
        print(f"spliced SpotifyConnectApplet -> V6c {got[:12]}")
        print("V6c ready: open Spotify Connect once to install, "
              "then run restore")
    finally:
        d.close()


main()
