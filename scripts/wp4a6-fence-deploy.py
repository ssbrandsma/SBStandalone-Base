#!/usr/bin/env python3
"""WP4a.6 fence deploy: backup, upload, splice skins, dormant Radio patch.

Steps (all verified, all receipted; aborts on any mismatch):
  1. Download live skins + Radio StreamPlayer; verify md5 against the
     reviewed local copies (refuses to patch unexpected bytes).
  2. Back up every touched file to /root/wp4a6-backup/ as *.orig.
  3. Upload PlaybackFence.lua + Ownership{Client,Http,Runtime,Hooks}.lua
     to the StandaloneBase applet dir (new files; requireable without a
     Jive restart). Syntax-checked with luac5.1 before upload.
  4. Splice a 5-line pcall loader before the `module(` line of BOTH skin
     applets (transient carrier; restored after verified install).
     Spliced files are syntax-checked before upload.
  5. Apply the dormant on-disk Radio ownership guards (take effect on a
     future authorized Jive restart; the live fence covers the running VM).
  6. Write /root/wp4a6-fence-receipt.json (+ local copy).

Does NOT create the arm marker and does NOT ask the owner to switch skins;
those are separate verified steps. Never restarts Jive.
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

SKINS = [
    "/usr/share/jive/applets/WQVGAsmallSkin/WQVGAsmallSkinApplet.lua",
    "/usr/share/jive/applets/WQVGAlargeSkin/WQVGAlargeSkinApplet.lua",
]
RADIO_PLAYER = "/usr/share/jive/applets/StandaloneRadio/StreamPlayer.lua"

# Reviewed live copies (downloaded 2026-11-07, see .local/wp4a6/).
EXPECTED_MD5 = {
    SKINS[0]: "cedf6b06bafac56b092e8a308116420a",
    SKINS[1]: "bfbd7d134f7cce0f0d7cc2633836c55c",
}
RADIO_LOCAL = REPO / ".local/wp4a6/radio-live/StreamPlayer.lua"

LUA_FILES = ["PlaybackFence.lua", "OwnershipClient.lua", "OwnershipHttp.lua",
             "OwnershipRuntime.lua", "OwnershipHooks.lua"]

SPLICE = ('-- SB-WP4A6-FENCE-BEGIN (transient carrier; see receipt)\n'
          'pcall(function()\n'
          '    require("applets.StandaloneBase.PlaybackFence").install()\n'
          'end)\n'
          '-- SB-WP4A6-FENCE-END\n')

# Dormant on-disk Radio guards (CRLF). Each anchor must occur exactly once.
DORMANT = [
    # _handleDisconnect: only recover owned streams.
    (b'\tif self.intentionalStop or flush or not reason or not self.desiredStation then\r\n\t\treturn\r\n\tend\r\n',
     b'\tif self.intentionalStop or flush or not reason or not self.desiredStation then\r\n\t\treturn\r\n\tend\r\n\t-- WP4a.6: only recover streams this player owns; a foreign\r\n\t-- (Music) decoder stop must never trigger a Radio reconnect.\r\n\tif not self.ownsPlayback then\r\n\t\treturn\r\n\tend\r\n'),
    # _scheduleReconnect: same gate (covers watchdog follow-ups).
    (b'\tif not self:_isCurrent(generation, station) or self.reconnectTimer then\r\n\t\treturn\r\n\tend\r\n',
     b'\tif not self:_isCurrent(generation, station) or self.reconnectTimer then\r\n\t\treturn\r\n\tend\r\n\t-- WP4a.6: no reconnect unless this player owns the decoder.\r\n\tif not self.ownsPlayback then\r\n\t\treturn\r\n\tend\r\n'),
    # Headers hook: only claim PLAYING for owned streams.
    (b'\t\tif self.desiredStation and not self.intentionalStop then\r\n',
     b'\t\t-- WP4a.6: only the decoder owner may claim a stream.\r\n\t\tif self.ownsPlayback and self.desiredStation and not self.intentionalStop then\r\n'),
    # Audio recovery: never touch a foreign stream.
    (b'\tif not self.playbackActive or self.intentionalStop or self.paused then\r\n',
     b'\t-- WP4a.6: never resume a stream this player does not own.\r\n\tif not self.playbackActive or self.intentionalStop or self.paused or not self.ownsPlayback then\r\n'),
    # Metadata: only for owned streams.
    (b'\tif not self.desiredStation or not self.playbackActive or type(data) ~= "string" then\r\n',
     b'\t-- WP4a.6: only the decoder owner consumes stream metadata.\r\n\tif not self.ownsPlayback or not self.desiredStation or not self.playbackActive or type(data) ~= "string" then\r\n'),
    # State notifications: foreign claims must not reach the applet/UI.
    (b'function StreamPlayer:_notifyState(station, state, show)\r\n\tself.state = state\r\n\tself.callbacks.onState(station, state, show, self.stationGeneration)\r\nend\r\n',
     b'function StreamPlayer:_notifyState(station, state, show)\r\n\tself.state = state\r\n\t-- WP4a.6: foreign-stream claims must not reach the applet/UI.\r\n\tif not self.ownsPlayback and (state == "PLAYING" or state == "CONNECTION_LOST" or state == "RECONNECTING") then\r\n\t\treturn\r\n\tend\r\n\tself.callbacks.onState(station, state, show, self.stationGeneration)\r\nend\r\n'),
]


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
    receipt = {"tool": "wp4a6-fence-deploy.py", "device": DEVICE,
               "started": datetime.now(timezone.utc).isoformat(),
               "files": {}, "steps": []}
    d = life.Device(DEVICE)

    def call(cmd, timeout=90):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read().decode(errors="replace")

    def call_bytes(cmd, timeout=120):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read()

    def put_bytes(data, remote):
        d.call(f"cat > {remote}", data=data)

    try:
        # 1. Download + verify live bytes.
        live = {}
        for remote in SKINS + [RADIO_PLAYER]:
            data = call_bytes(f"cat {remote}")
            assert data, f"empty download: {remote}"
            (work / Path(remote).name).write_bytes(data)
            live[remote] = data
            digest = md5_bytes(data)
            receipt["files"][remote] = {"live_md5": digest,
                                        "live_size": len(data)}
        for remote, want in EXPECTED_MD5.items():
            got = receipt["files"][remote]["live_md5"]
            if got != want:
                raise SystemExit(
                    f"live {remote} md5 {got} != reviewed {want}; aborting")
        radio_want = md5_bytes(RADIO_LOCAL.read_bytes())
        if receipt["files"][RADIO_PLAYER]["live_md5"] != radio_want:
            raise SystemExit("live StreamPlayer.lua differs from the "
                             "reviewed copy; aborting")
        if b"SB-WP4A6-FENCE" in live[SKINS[0]] + live[SKINS[1]]:
            raise SystemExit("splice already present; refusing double-deploy")
        print("verified live md5:", {k.split("/")[-2]: v["live_md5"][:12]
                                     for k, v in receipt["files"].items()})

        # 2. Back up originals on device.
        call(f"mkdir -p {BACKUP}")
        for remote in SKINS + [RADIO_PLAYER]:
            name = Path(remote).name
            print(call(f"cp -p {remote} {BACKUP}/{name}.orig && "
                       f"md5sum {BACKUP}/{name}.orig").strip())
        receipt["steps"].append("backup")

        # 3. Upload Lua modules (syntax-checked first).
        for name in LUA_FILES:
            src = REPO / "applet" / name
            luac_check(src)
            data = src.read_bytes()
            put_bytes(data, f"{BASE_APPLET}/{name}")
            got = call(f"md5sum {BASE_APPLET}/{name}").split()[0]
            want = md5_bytes(data)
            if got != want:
                raise SystemExit(f"upload md5 mismatch for {name}")
            receipt["files"][f"{BASE_APPLET}/{name}"] = {
                "uploaded_md5": got, "uploaded_size": len(data)}
            print(f"uploaded {name} {got[:12]}")
        receipt["steps"].append("upload")

        # 4. Splice skins locally, syntax-check, upload.
        for remote in SKINS:
            data = live[remote]
            eol = b"\r\n" if data.count(b"\r\n") > data.count(b"\n") / 2 \
                else b"\n"
            anchor = b"module(..., Framework.constants)"
            if data.count(anchor) != 1:
                raise SystemExit(f"splice anchor not unique in {remote}")
            splice = SPLICE.replace("\n", eol.decode()).encode()
            patched = data.replace(anchor, splice + anchor)
            tmp = work / (Path(remote).name + ".spliced")
            tmp.write_bytes(patched)
            luac_check(tmp)
            put_bytes(patched, remote)
            got = call(f"md5sum {remote}").split()[0]
            if got != md5_bytes(patched):
                raise SystemExit(f"splice upload mismatch for {remote}")
            receipt["files"][remote]["spliced_md5"] = got
            receipt["files"][remote]["splice_lines"] = \
                len(SPLICE.splitlines())
            print(f"spliced {remote.split('/')[-2]} {got[:12]}")
        receipt["steps"].append("splice")

        # 5. Dormant Radio patch (byte-exact anchors, CRLF preserved).
        patched = live[RADIO_PLAYER]
        for old, new in DORMANT:
            if patched.count(old) != 1:
                raise SystemExit("dormant anchor not unique; aborting")
            patched = patched.replace(old, new)
        tmp = work / "StreamPlayer.lua.dormant"
        tmp.write_bytes(patched)
        # The device Lua parses C-style bitwise ops (proven by stock
        # firmware files); host luac5.1 does not. Validate structure by
        # placeholder-substituting the 3 known bitwise lines in a COPY.
        check = patched
        swaps = [
            (b"local DECODE_UNDERRUN = (1 << 1)",
             b"local DECODE_UNDERRUN = 2"),
            (b'        if status.decodeState and status.decodeState & ((1 << 2) | (1 << 3)) ~= 0 then self:stop("FAILED");return end',
             b'        if false then self:stop("FAILED");return end'),
            (b"\tlocal audioUnderrun = status.audioState & DECODE_UNDERRUN ~= 0",
             b"\tlocal audioUnderrun = false"),
        ]
        for old, new in swaps:
            if check.count(old) != 1:
                raise SystemExit(
                    "dormant bitwise line drifted; aborting")
            check = check.replace(old, new)
        tmp_check = work / "StreamPlayer.lua.dormant.check"
        tmp_check.write_bytes(check)
        luac_check(tmp_check)
        put_bytes(patched, RADIO_PLAYER)
        got = call(f"md5sum {RADIO_PLAYER}").split()[0]
        if got != md5_bytes(patched):
            raise SystemExit("dormant upload mismatch")
        receipt["files"][RADIO_PLAYER]["dormant_md5"] = got
        receipt["steps"].append("dormant")
        print(f"radio dormant {got[:12]}")

        receipt["finished"] = datetime.now(timezone.utc).isoformat()
        receipt_json = json.dumps(receipt, indent=2)
        put_bytes(receipt_json.encode(), RECEIPT)
        (work / "receipt.json").write_text(receipt_json)
        print("receipt written")
    finally:
        d.close()


if __name__ == "__main__":
    main()
