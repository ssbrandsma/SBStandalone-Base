"""Drive the shared native playback harness with an actual HTTP MP3 stream."""
import http.server
from pathlib import Path
import socket
import subprocess
import sys
import threading

probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
probe.connect(("192.0.2.1", 9))
ip = probe.getsockname()[0]
probe.close()
media = Path(sys.argv[2]).read_bytes()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        assert self.path == "/music.mp3"
        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("Content-Length", str(len(media)))
        self.end_headers()
        self.wfile.write(media)


with http.server.ThreadingHTTPServer((ip, 0), Handler) as server:
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        result = subprocess.run([sys.argv[1], f"http://{ip}:{server.server_port}/music.mp3", sys.argv[2]], timeout=30, capture_output=True, text=True)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr)
        assert result.returncode == 0, result.returncode
    finally:
        server.shutdown()
