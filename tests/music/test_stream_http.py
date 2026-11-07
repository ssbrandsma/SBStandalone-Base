"""Existing sbwebserver, inherited private descriptor capabilities, host fixtures."""
import array
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

WIRE = struct.Struct('!4sBBBBQ32s16s' + 'Q' * 9)


def run(binary, production=False):
    with tempfile.TemporaryDirectory(prefix='sb-music-stream-http-') as directory:
        root = Path(directory)
        (root / 'web').mkdir()
        (root / 'web' / 'index.html').write_text('fixture')
        (root / 'config').mkdir()
        (root / 'config' / 'settings.conf').write_text('readonly=1\n')
        payload = bytes(range(256)) * 4096
        path = root / 'encoded.mp3'
        path.write_bytes(payload)
        fd = os.open(path, os.O_RDONLY)
        big = root / 'large.flac'
        with big.open('wb') as f:
            f.seek(2**32)
            f.write(b'FLAC64!!')
        bigfd = os.open(big, os.O_RDONLY)
        peer, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        peer.settimeout(3)
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
        listener.close()
        env = os.environ | {'SBWEBSERVER_CONFIG_DIR': str(root / 'config')}
        with tempfile.TemporaryFile() as log:
            p = subprocess.Popen([binary, '--port', str(port), '--web-root', str(root / 'web'),
                                  '--music-ipc' if production else '--test-music-ipc', str(child.fileno())],
                                 pass_fds=(child.fileno(),), env=env, stdout=log, stderr=log)
            child.close()
            sequence = 0
            sockets = []

            def message(operation, token, descriptor=-1, size=0, codec='m', expires=None):
                nonlocal sequence
                sequence += 1
                st = os.fstat(descriptor) if descriptor >= 0 else None
                raw = WIRE.pack(b'SBM4', 2, operation, ord(codec), 3, sequence, token, b'\1' + bytes(15),
                                1, 2, 3, expires or int(time.monotonic() * 1000) + 60000, size,
                                st.st_dev if st else 0, st.st_ino if st else 0,
                                st.st_mtime_ns // 1000000000 if st else 0, st.st_mtime_ns % 1000000000 if st else 0)
                ancillary = [] if descriptor < 0 else [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', [descriptor]))]
                assert peer.sendmsg([raw], ancillary) == WIRE.size
                ack = WIRE.unpack(peer.recv(WIRE.size))
                assert ack[2] == 4 and ack[5] == sequence and ack[4] == 0, ack

            def request(token, method='GET', headers=None):
                c = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
                c.request(method, '/music/stream/' + token.hex(), headers=headers or {})
                response = c.getresponse()
                result = response.status, dict(response.getheaders()), response.read()
                c.close()
                return result

            try:
                for _ in range(200):
                    try:
                        probe = socket.create_connection(('127.0.0.1', port), timeout=.1)
                        probe.close()
                        break
                    except OSError:
                        if p.poll() is not None:
                            log.seek(0)
                            raise AssertionError(log.read().decode(errors='replace'))
                        time.sleep(.02)
                else:
                    raise AssertionError('webserver startup deadline')
                baseline_fds = len(list(Path(f'/proc/{p.pid}/fd').iterdir()))
                token, large = os.urandom(32), os.urandom(32)
                message(1, token, fd, len(payload))
                message(1, large, bigfd, 2**32 + 8, codec='f')
                start = time.monotonic()
                status, headers, data = request(token)
                wall = time.monotonic() - start
                assert status == 200 and data == payload and headers['Content-Type'] == 'audio/mpeg'
                assert int(headers['Content-Length']) == len(payload)
                status, headers, data = request(token, 'HEAD')
                assert status == 200 and not data and int(headers['Content-Length']) == len(payload)
                status, headers, data = request(token, headers={'Range': 'bytes=17-100'})
                assert status == 206 and data == payload[17:101]
                assert headers['Content-Range'] == f'bytes 17-100/{len(payload)}'
                assert request(token, headers={'Range': 'bytes=-10'})[2] == payload[-10:]
                assert request(token, headers={'Range': 'bytes=1048570-'})[2] == payload[1048570:]
                status, headers, data = request(token, headers={'Range': 'bytes=99-98'})
                assert status == 416 and headers['Content-Range'] == f'bytes */{len(payload)}' and not data
                assert request(large, headers={'Range': 'bytes=4294967296-'})[2] == b'FLAC64!!'
                assert request(os.urandom(32))[0] == 404
                assert request(token, 'POST')[0] == 405
                with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                    results = list(pool.map(lambda _: request(token, headers={'Range': 'bytes=0-1023'}), range(4)))
                assert all(x[0] == 206 and x[2] == payload[:1024] for x in results)
                # Four stopped readers pin the admission slots; the fifth is bounded.
                for _ in range(4):
                    s = socket.socket()
                    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
                    s.settimeout(5)
                    s.connect(('127.0.0.1', port))
                    s.sendall(f'GET /music/stream/{large.hex()} HTTP/1.1\r\nHost: fixture\r\n\r\n'.encode())
                    assert b'200' in s.recv(512).split(b'\r\n', 1)[0]
                    sockets.append(s)
                assert request(token, 'HEAD')[0] == 503
                message(2, large)
                for s in sockets:
                    s.close()
                sockets.clear()
                assert request(large)[0] == 410
                message(2, token)
                assert request(token)[0] == 410
                expiring = os.urandom(32)
                message(1, expiring, fd, len(payload), expires=int(time.monotonic() * 1000) + 80)
                time.sleep(.12)
                assert request(expiring)[0] == 410
                missing = os.urandom(32)
                message(1, missing, fd, len(payload))
                path.unlink()
                assert request(missing)[0] == 410
                peer.close()
                assert request(missing)[0] == 410
                time.sleep(.05)
                final_fds = len(list(Path(f'/proc/{p.pid}/fd').iterdir()))
                assert final_fds <= baseline_fds + 1, (baseline_fds, final_fds)
                print(json.dumps({'http_bytes': len(payload), 'http_seconds': wall,
                                  'bytes_per_second': len(payload) / wall,
                                  'fds_before': baseline_fds, 'fds_after': final_fds,
                                  'simultaneous_streams': 4, 'buffer_bytes': 16384}))
            finally:
                for s in sockets:
                    s.close()
                p.terminate()
                p.wait(timeout=5)
                peer.close()
                os.close(fd)
                os.close(bigfd)


if __name__ == '__main__':
    run(sys.argv[1], '--production' in sys.argv[2:])
