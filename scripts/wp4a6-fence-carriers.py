#!/usr/bin/env python3
"""WP4a.6 loud carriers: replace the silent skin splice with a logging one
and add splices to three rarely-opened menu applets (transient carriers,
restored after verified install).

Rationale: the large-skin switch produced no fence log lines. Either the
skin was already evaluated (no reload) or the silent pcall swallowed an
error. Loud splices distinguish the cases: every carrier execution logs
ok=/err= through the StandaloneBase logger.

Never restarts Jive. Extends /root/wp4a6-backup + receipt.
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
BACKUP = "/root/wp4a6-backup"
RECEIPT = "/root/wp4a6-fence-receipt.json"

SKINS = [
    "/usr/share/jive/applets/WQVGAsmallSkin/WQVGAsmallSkinApplet.lua",
    "/usr/share/jive/applets/WQVGAlargeSkin/WQVGAlargeSkinApplet.lua",
]
CARRIERS = [
    "/usr/share/jive/applets/ImageViewer/ImageViewerApplet.lua",
    "/usr/share/jive/applets/SetupWallpaper/SetupWallpaperApplet.lua",
    "/usr/share/jive/applets/CustomizeHomeMenu/CustomizeHomeMenuApplet.lua",
]

OLD_SPLICE = ('-- SB-WP4A6-FENCE-BEGIN (transient carrier; see receipt)\n'
              'pcall(function()\n'
              '    require("applets.StandaloneBase.PlaybackFence").install()\n'
              'end)\n'
              '-- SB-WP4A6-FENCE-END\n')

LOUD_SPLICE = ('-- SB-WP4A6-FENCE-BEGIN (transient carrier; see receipt)\n'
               'local _sbFenceOk, _sbFenceResult = pcall(function()\n'
               '    return require("applets.StandaloneBase.PlaybackFence").install()\n'
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
        if not isinstance(receipt.get("files"), dict):
            raise SystemExit("deploy receipt missing; run deploy first")

        # Backup + splice the three new carriers.
        for remote in CARRIERS:
            data = call_bytes(f"cat {remote}")
            assert data, f"empty download: {remote}"
            (work / Path(remote).name).write_bytes(data)
            digest = md5_bytes(data)
            if b"SB-WP4A6-FENCE" in data:
                raise SystemExit(f"splice already present in {remote}")
            name = Path(remote).name
            print(call(f"cp -p {remote} {BACKUP}/{name}.orig && "
                       f"md5sum {BACKUP}/{name}.orig").strip())
            receipt["files"][remote] = {"live_md5": digest,
                                        "live_size": len(data)}
            eol = b"\r\n" if data.count(b"\r\n") > data.count(b"\n") / 2 \
                else b"\n"
            if data.count(ANCHOR) != 1:
                raise SystemExit(f"anchor not unique in {remote}")
            splice = LOUD_SPLICE.replace("\n", eol.decode()).encode()
            patched = data.replace(ANCHOR, splice + ANCHOR)
            tmp = work / (name + ".spliced")
            tmp.write_bytes(patched)
            # Carrier files may use device-extended syntax (proven good
            # on-device); validate the self-contained splice snippet.
            snippet = work / (name + ".snippet")
            snippet.write_bytes(splice)
            luac_check(snippet)
            d.call(f"cat > {remote}", data=patched)
            got = call(f"md5sum {remote}").split()[0]
            if got != md5_bytes(patched):
                raise SystemExit(f"splice upload mismatch for {remote}")
            receipt["files"][remote]["spliced_md5"] = got
            print(f"spliced {name} {got[:12]}")

        # Replace the silent skin splice with the loud one.
        for remote in SKINS:
            data = call_bytes(f"cat {remote}")
            eol = b"\r\n" if data.count(b"\r\n") > data.count(b"\n") / 2 \
                else b"\n"
            old = OLD_SPLICE.replace("\n", eol.decode()).encode()
            if data.count(old) != 1:
                raise SystemExit(f"old splice not found once in {remote}")
            loud = LOUD_SPLICE.replace("\n", eol.decode()).encode()
            patched = data.replace(old, loud)
            tmp = work / (Path(remote).name + ".loud")
            tmp.write_bytes(patched)
            luac_check(tmp)
            d.call(f"cat > {remote}", data=patched)
            got = call(f"md5sum {remote}").split()[0]
            if got != md5_bytes(patched):
                raise SystemExit(f"loud splice upload mismatch {remote}")
            receipt["files"][remote]["loud_md5"] = got
            print(f"loud {Path(remote).stem} {got[:12]}")

        receipt.setdefault("steps", []).append("loud_carriers")
        receipt["loud_at"] = datetime.now(timezone.utc).isoformat()
        blob = json.dumps(receipt, indent=2)
        d.call(f"cat > {RECEIPT}", data=blob.encode())
        (work / "receipt.json").write_text(blob)
        print("carriers ready")
    finally:
        d.close()


if __name__ == "__main__":
    main()
