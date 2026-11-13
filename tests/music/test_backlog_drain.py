"""CometD backlog drain: oversized Jive replay batches must drain, not 400/413-loop.

Jive's request sink never drops pending_reqs, so after any error it
replays its whole backlog. Batches beyond the JSON token guard or the
256-message batch cap must be truncated to their head (first 32
answered, overflow noted) so the backlog drains progressively.
Genuinely malformed bodies and non-/slim oversized batches keep the
strict 400. Uses disposable fixtures; no device/audio.
"""
import http.client
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PLAYER = '02:00:00:00:00:01'


def run(base, fixture):
    with tempfile.TemporaryDirectory(prefix='sb-music-browse-') as directory:
        root = Path(directory)
        db = root / 'library.db'
        (root / 'WP3B_TEST_FIXTURE').write_text('WP3B disposable fixture\n')
        subprocess.run([fixture, 'create', str(db), '200'], check=True, timeout=180)
        with tempfile.TemporaryFile() as log:
            process = subprocess.Popen([base, '--test-music-db', str(db)], stderr=log)
            sockets = []

            def req_raw(raw, timeout=10):
                c = http.client.HTTPConnection('127.0.0.1', 9000, timeout=timeout)
                c.request('POST', '/cometd', raw, {'Content-Type': 'application/json'})
                r = c.getresponse()
                data = r.read()
                status = r.status
                c.close()
                return status, data

            def req(payload, timeout=10):
                status, data = req_raw(json.dumps(payload).encode(), timeout)
                return status, json.loads(data)

            def message(command, number, cid='drain'):
                return {'channel': '/slim/request', 'id': str(number), 'clientId': cid,
                        'data': {'request': [PLAYER, command],
                                 'response': f'/{cid}/reply'}}
            try:
                for _ in range(150):
                    try:
                        c = http.client.HTTPConnection('127.0.0.1', 9000, timeout=.2)
                        c.request('GET', '/health')
                        ok = c.getresponse().status == 200
                        c.close()
                        if ok:
                            break
                    except OSError:
                        if process.poll() is not None:
                            log.seek(0)
                            raise AssertionError(log.read().decode(errors='replace'))
                        time.sleep(.02)
                else:
                    raise AssertionError('fixture Base did not start')
                slim = socket.create_connection(('127.0.0.1', 3483), timeout=3)
                sockets.append(slim)
                hello = bytes([7, 0]) + bytes.fromhex(PLAYER.replace(':', '')) + bytes(26) + b'ENModel=fab4,mp3'
                slim.sendall(b'HELO' + struct.pack('!I', len(hello)) + hello)
                raw = b''
                while len(raw) < 30:
                    raw += slim.recv(128)

                # 1. 600-message backlog (>4096 tokens): must 200 with head
                # answers + overflow note, never 400 malformed_json.
                big = [message(['menu', 0, 10, 'direct:1'], n) for n in range(600)]
                status, texts = req_raw(json.dumps(big).encode())
                assert status == 200, f'600-batch status={status} body={texts[:120]}'
                texts = json.loads(texts)
                assert texts[0]['id'] == '0' and texts[0]['successful'], texts[:2]
                assert any(m.get('error') == 'response_too_large' for m in texts), 'no overflow note'
                answered = [m['id'] for m in texts if m.get('channel', '').endswith('/reply')]
                assert answered[0] == '0' and len(answered) <= 32, answered[:4]

                # 2. 300-message batch (>256 cap): must 200 partial, not 413.
                mid = [message(['menu', 0, 10, 'direct:1'], n) for n in range(300)]
                status, texts = req(mid)
                assert status == 200, f'300-batch status={status}'
                assert texts[0]['id'] == '0' and texts[0]['successful'], texts[:2]
                assert any(m.get('error') == 'response_too_large' for m in texts), 'no overflow note'

                # 3. Genuinely malformed bodies keep the strict 400.
                status, texts = req_raw(b'{oops')
                assert status == 400 and b'malformed_json' in texts, (status, texts)
                status, texts = req_raw(b'[{"channel":"/slim/request"')
                assert status == 400 and b'malformed_json' in texts, (status, texts)

                # 4. Oversized non-/slim batch keeps the strict 400.
                meta = [{'channel': '/meta/connect', 'clientId': 'x'} for _ in range(2500)]
                status, texts = req_raw(json.dumps(meta).encode())
                assert status == 400 and b'malformed_json' in texts, (status, texts[:80])

                # 5. Subscribe ack echoes the requested subscription.
                sock = socket.create_connection(('127.0.0.1', 9000), timeout=5)
                sockets.append(sock)
                hs = json.dumps([{'channel': '/meta/handshake'}]).encode()
                sock.sendall(b'POST /cometd HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: '
                              + str(len(hs)).encode() + b'\r\nConnection: keep-alive\r\n\r\n' + hs)
                received = b''
                while b'\r\n\r\n' not in received:
                    received += sock.recv(4096)
                header, rest = received.split(b'\r\n\r\n', 1)
                length = int(next(line.split(b':', 1)[1] for line in header.split(b'\r\n')
                                    if line.lower().startswith(b'content-length:')))
                while len(rest) < length:
                    rest += sock.recv(4096)
                cid = json.loads(rest[:length])[0]['clientId']
                conn = json.dumps([{'channel': '/meta/connect', 'clientId': cid},
                                   {'channel': '/meta/subscribe', 'clientId': cid,
                                    'subscription': f'/{cid}/**'}]).encode()
                sock.sendall(b'POST /cometd HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: '
                              + str(len(conn)).encode() + b'\r\nConnection: keep-alive\r\n\r\n' + conn)
                ack = b''
                while b'/meta/subscribe' not in ack:
                    ack += sock.recv(4096)
                assert f'"subscription":"/{cid}/**"'.encode() in ack, ack

                # 6. Pipelined POSTs on one socket are all answered in
                # order (used=0 used to swallow the tail and desync
                # the socket into a MALFORMED storm).
                def framed(payload, keep=True):
                    conn = b'keep-alive' if keep else b'close'
                    return (b'POST /cometd HTTP/1.1\r\nHost: x\r\n'
                            b'Content-Type: application/json\r\nContent-Length: '
                            + str(len(payload)).encode() + b'\r\nConnection: '
                            + conn + b'\r\n\r\n' + payload)
                psock = socket.create_connection(('127.0.0.1', 9000), timeout=5)
                sockets.append(psock)
                p_hs = json.dumps([{'channel': '/meta/handshake'}]).encode()
                p_m1 = json.dumps([message(['menu', 0, 10, 'direct:1'], 11, cid='pipe')]).encode()
                p_m2 = json.dumps([message(['menu', 0, 10, 'direct:1'], 12, cid='pipe')]).encode()
                psock.sendall(framed(p_hs) + framed(p_m1) + framed(p_m2, keep=False))
                blob = b''
                while blob.count(b'HTTP/1.1 200 OK') < 3:
                    more = psock.recv(65536)
                    if not more:
                        break
                    blob += more
                assert blob.count(b'HTTP/1.1 200 OK') == 3, blob[:300]
                assert b'400 Bad Request' not in blob, blob[:300]
                p11, p12 = blob.find(b'"id":"11"'), blob.find(b'"id":"12"')
                assert 0 < p11 < p12, blob[-600:]

                # 7. A coalesced partial second request completes on
                # the next recv instead of poisoning the socket.
                fsock = socket.create_connection(('127.0.0.1', 9000), timeout=5)
                sockets.append(fsock)
                f_m1 = json.dumps([message(['menu', 0, 10, 'direct:1'], 21, cid='frag')]).encode()
                f_m2 = json.dumps([message(['menu', 0, 10, 'direct:1'], 22, cid='frag')]).encode()
                wire = framed(f_m1) + framed(f_m2, keep=False)
                fsock.sendall(wire[:-25])
                time.sleep(0.3)
                fsock.sendall(wire[-25:])
                blob = b''
                while blob.count(b'HTTP/1.1 200 OK') < 2:
                    more = fsock.recv(65536)
                    if not more:
                        break
                    blob += more
                assert blob.count(b'HTTP/1.1 200 OK') == 2, blob[:300]
                assert b'"id":"21"' in blob and b'"id":"22"' in blob, blob[-600:]

                # 8. Truncated head (suffix starting mid-message, as
                # Jive's pump emits under backpressure): resync past
                # the break and answer the tail instead of 400.
                batch = [message(['menu', 0, 10, 'direct:1'], n, cid='rs')
                         for n in range(3)]
                wire = json.dumps(batch).encode()
                assert wire[:7] == b'[{"chan'
                status, texts = req_raw(wire[7:])
                assert status == 200, f'suffix status={status} body={texts[:120]}'
                texts = json.loads(texts)
                assert any(m.get('id') == '1' and m.get('successful') for m in texts), texts
                assert not any(m.get('id') == '0' for m in texts), texts

                # 9. Valid batch after 8+ garbage bytes resyncs (graceful).
                status, texts = req_raw(b'XXXXXXX' + json.dumps(
                    [message(['menu', 0, 10, 'direct:1'], 5, cid='rs')]).encode())
                assert status == 200, f'skipped=8 status={status}'

                # 10. Aligned-but-corrupt (1 stray byte) keeps the 400.
                status, texts = req_raw(b'X' + json.dumps(
                    [message(['menu', 0, 10, 'direct:1'], 6, cid='rs')]).encode())
                assert status == 400 and b'malformed_json' in texts, (status, texts[:80])

                # 11. WP4a.6 Dreams regression: a playerstatus
                # subscribe lost in a truncated head (the startup
                # drain skipped Jive's 206-byte subscribe) must heal:
                # any later live request from that client refreshes
                # the subscription, so a same-id reconnect replay
                # delivers playerstatus again.
                hss = json.dumps([{'channel': '/meta/handshake',
                                   'mac': PLAYER}]).encode()
                status, texts = req_raw(hss)
                assert status == 200, (status, texts[:80])
                scid = json.loads(texts)[0]['clientId']

                class Stream:
                    def __init__(self, cid):
                        self.sock = socket.create_connection(
                            ('127.0.0.1', 9000), timeout=5)
                        sockets.append(self.sock)
                        body = json.dumps([
                            {'channel': '/meta/connect',
                             'clientId': cid}]).encode()
                        self.sock.sendall(
                            b'POST /cometd HTTP/1.1\r\nHost: x\r\n'
                            b'Content-Type: application/json\r\n'
                            b'Content-Length: ' + str(len(body)).encode()
                            + b'\r\nConnection: keep-alive\r\n\r\n' + body)
                        self.buf = b''
                        while b'\r\n\r\n' not in self.buf:
                            self.buf += self.sock.recv(4096)
                        head, self.buf = self.buf.split(b'\r\n\r\n', 1)
                        assert b'200 OK' in head, head
                        assert b'chunked' in head, head
                        assert self.chunk(3)[0]['channel'] == (
                            '/meta/connect')

                    def chunk(self, timeout):
                        self.sock.settimeout(timeout)
                        while b'\r\n' not in self.buf:
                            self.buf += self.sock.recv(4096)
                        line, self.buf = self.buf.split(b'\r\n', 1)
                        size = int(line, 16)
                        while len(self.buf) < size + 2:
                            self.buf += self.sock.recv(4096)
                        out, self.buf = (self.buf[:size],
                                         self.buf[size + 2:])
                        return json.loads(out)

                    def playerstatus(self, timeout=3, want=None):
                        deadline = time.time() + timeout
                        while time.time() < deadline:
                            for event in self.chunk(
                                    max(0.1,
                                        deadline - time.time())):
                                ch = event.get('channel', '')
                                if '/slim/playerstatus/' in ch and (
                                        want is None or
                                        f'/{want}/' in ch):
                                    return event['data']
                        raise AssertionError(
                            'no playerstatus replay received')

                    def no_playerstatus(self, timeout=0.6,
                                        want=None):
                        # Broadcast reaches every live stream; only a
                        # push ADDRESSED to our CID counts. Earlier
                        # cases' CIDs echo here harmlessly.
                        deadline = time.time() + timeout
                        try:
                            while time.time() < deadline:
                                for event in self.chunk(
                                        max(0.05,
                                            deadline - time.time())):
                                    ch = event.get('channel', '')
                                    if '/slim/playerstatus/' in ch and (
                                            want is None or
                                            f'/{want}/' in ch):
                                        raise AssertionError(event)
                        except socket.timeout:
                            pass

                stream = Stream(scid)
                sub = {'channel': '/slim/subscribe', 'id': '40',
                       'clientId': scid,
                       'data': {'request': [PLAYER, ['status', '-', 10,
                                                    'subscribe:600']],
                                'response': f'/{scid}/slim/playerstatus/{PLAYER}'}}
                batch = [sub,
                         message(['menu', 0, 10, 'direct:1'], 41,
                                 cid='pad'),
                         message(['menu', 0, 10, 'direct:1'], 42,
                                 cid='pad')]
                wire = json.dumps(batch).encode()
                status, texts = req_raw(wire[7:])
                assert status == 200, (status, texts[:80])
                # Lost subscribe documented: nothing registered for
                # us, so a same-id reconnect replays nothing addressed
                # to us (earlier cases' broadcasts may echo).
                probe = Stream(scid)
                probe.no_playerstatus(want=scid)
                # Any live request from that client heals it.
                status, texts = req(
                    [message(['menu', 0, 10, 'direct:1'], 43, cid=scid)])
                assert status == 200, (status, texts[:80])
                healed = Stream(scid)
                snap = healed.playerstatus(want=scid)
                assert snap.get('mode') in (
                    'stop', 'play', 'pause'), snap
            finally:
                for s in sockets:
                    try:
                        s.close()
                    except OSError:
                        pass
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()


if __name__ == '__main__':
    run(sys.argv[1], sys.argv[2])
    print('backlog drain ok')
