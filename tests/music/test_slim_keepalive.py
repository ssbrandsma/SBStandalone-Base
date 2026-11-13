"""SlimProto idle poll must survive a chattering peer (Fab4 WP4a.6).

Jive drops a server-silent SlimProto connection after 35 s (READ_TIMEOUT) and
reconnects. While the decoder runs (radio, or any status traffic), Jive sends
STMt about once per second. The base must still emit its strm-t idle poll:
incoming traffic must not suppress outgoing keepalives, or the link flaps
forever and ownership never settles.

Run: test_slim_keepalive.py <sbbase-binary> <browse-fixture-binary>
"""
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def frame(op, payload):
    return op + struct.pack(">I", len(payload)) + payload


def helo_payload():
    p = bytearray(36)
    p[0] = 12
    p[2:8] = bytes((0x02, 0x00, 0x00, 0x00, 0x00, 0x01))
    p[8:24] = bytes(range(1, 17))
    p[34:36] = b"EN"
    return bytes(p) + b"Model=fab4,Firmware=7.7.3,mp3"


def stat_payload(code=b"STMt"):
    p = bytearray(51)
    p[0:4] = code
    return bytes(p)


def read_frames(sock, deadline, want):
    """Collect server frames (len16 + op4 + payload) until deadline."""
    ops = []
    buf = b""
    while time.monotonic() < deadline and len(ops) < want:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            break
        buf += chunk
        while len(buf) >= 6:
            total = struct.unpack(">H", buf[0:2])[0]
            if len(buf) < 2 + total:
                break
            ops.append(buf[2:6])
            buf = buf[2 + total:]
    return ops


def main():
    base, fixture = sys.argv[1], sys.argv[2]
    with tempfile.TemporaryDirectory(prefix="sb-music-browse-") as directory:
        root = Path(directory)
        db = root / "library.db"
        (root / "WP3B_TEST_FIXTURE").write_text("WP3B disposable fixture\n")
        subprocess.run([fixture, "create", str(db), "4"],
                       check=True, timeout=120)
        with tempfile.TemporaryFile() as log:
            proc = subprocess.Popen([base, "--test-music-db", str(db)],
                                    stderr=log)
            try:
                sock = None
                for _ in range(100):
                    try:
                        sock = socket.create_connection(("127.0.0.1", 3483),
                                                        timeout=2)
                        break
                    except OSError:
                        time.sleep(0.1)
                assert sock, "slim listener never came up"
                sock.settimeout(0.5)
                sock.sendall(frame(b"HELO", helo_payload()))
                ops = read_frames(sock, time.monotonic() + 5, 1)
                assert ops and ops[0] == b"strm", \
                    f"no strm-t answer to HELO: {ops}"
                # Chattering peer: STMt twice per second for 7 s. The 4 s
                # idle poll must still fire at least once in that window.
                deadline = time.monotonic() + 7
                while time.monotonic() < deadline:
                    sock.sendall(frame(b"STAT", stat_payload()))
                    ops += read_frames(sock, time.monotonic() + 0.5,
                                       len(ops) + 1)
                strm = [o for o in ops if o == b"strm"]
                assert len(strm) >= 2, \
                    f"idle poll suppressed by peer chatter: {len(strm)} strm"
                print(f"PASS: {len(strm)} strm polls despite STMt chatter")
            finally:
                try:
                    sock.close()
                except Exception:
                    pass
                proc.terminate()
                proc.wait(timeout=10)


if __name__ == "__main__":
    main()
