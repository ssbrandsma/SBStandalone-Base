"""Oversized CometD batches must keep partial replies, never wipe (Fab4 WP4a.6).

Jive replays an entirely unanswered batch forever through its CometD sink. If
the base answers an oversized batch with only response_too_large (wiping the
replies it already assembled), the sink never drains: every retry rebuilds the
same oversized batch, saturates the ARM CPU, and collapses into a permanent
reconnect storm. The base must keep prior replies and stop the batch so the
sink drains progressively.

Run: test_browse_overflow.py <sbbase-binary> <browse-fixture-binary>
"""
import http.client
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def main():
    base, fixture = sys.argv[1], sys.argv[2]
    with tempfile.TemporaryDirectory(prefix="sb-music-browse-") as directory:
        root = Path(directory)
        db = root / "library.db"
        (root / "WP3B_TEST_FIXTURE").write_text("WP3B disposable fixture\n")
        subprocess.run([fixture, "create", str(db), "3000", "long"],
                       check=True, timeout=180)
        log = open(root / "base.log", "w")
        proc = subprocess.Popen([base, "--test-music-db", str(db)], stderr=log)
        try:
            time.sleep(2)
            import socket as _socket
            import struct as _struct
            slim = _socket.create_connection(("127.0.0.1", 3483), timeout=5)
            hello = (bytes([7, 0]) +
                     bytes.fromhex("020000000001") + bytes(26) +
                     b"ENModel=fab4,mp3,flc,ogg")
            slim.sendall(b"HELO" + _struct.pack("!I", len(hello)) + hello)
            raw = b""
            while len(raw) < 30:
                raw += slim.recv(128)
            assert raw[6:7] == b"t", f"no strm-t answer: {raw!r}"
            batch = []
            for i in range(24):
                batch.append({
                    "channel": "/slim/request", "id": str(i),
                    "clientId": "overflow",
                    "data": {
                        "request": ["02:00:00:00:00:01",
                                    ["browselibrary", "items", 0, 50,
                                     "mode:albums", "menu:1"]],
                        "response": "/overflow/reply"}})
            body = json.dumps(batch).encode()
            conn = http.client.HTTPConnection("127.0.0.1", 9000, timeout=60)
            conn.request("POST", "/cometd", body,
                         {"Content-Type": "application/json"})
            resp = conn.getresponse()
            data = resp.read().decode()
            assert resp.status == 200, f"status {resp.status}"
            # The wiped shape answers nothing usable: the sink can never
            # drain and Jive retries the same batch forever.
            assert data != ('[{"successful":false,'
                            '"error":"response_too_large"}]'), \
                "oversized batch wiped all partial replies"
            assert '"item_loop"' in data, \
                f"no partial browse data kept: {data[:200]}"
            print(f"PASS: partial batch kept ({len(data)}B, "
                  f"{data.count('item_loop')} pages)")
        finally:
            try:
                slim.close()
            except Exception:
                pass
            proc.terminate()
            proc.wait(timeout=10)
            log.close()


if __name__ == "__main__":
    main()
