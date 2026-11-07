"""Jive -> real native queue/HTTP bytes -> mocked firmware events. No device/audio."""
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import queue
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import threading
import time
from wp2_fixtures import mp3, compact_flac, MEDIA, TITLE

PLAYER = '02:00:00:00:00:01'


def run(base, web, scanner, helper, initial_owner=None, library_size=0, fixture=None):
    base, web, scanner, helper = (str(Path(x).resolve()) for x in (base, web, scanner, helper))
    with tempfile.TemporaryDirectory(prefix='sb-music-browse-') as db_dir, \
            tempfile.TemporaryDirectory(prefix='sb-music-playback-', dir='/dev/shm') as music_dir:
        root, media = Path(db_dir), Path(music_dir)
        (root / 'WP3B_TEST_FIXTURE').write_text('WP3B disposable fixture\n')
        (media / 'WP4_TEST_FIXTURE').write_text('WP4 disposable fixture\n')
        data = {'first.mp3': mp3(title='First track'),
                'second.flac': compact_flac(True).replace(b'DISCNUMBER=1/2', b'DISCNUMBER=2/2'),
                'indexed.ogg': (MEDIA / 'comments.ogg').read_bytes()}
        for name, content in data.items():
            (media / name).write_bytes(content)
        db = root / 'library.db'
        result = subprocess.run([scanner, 'scan', str(media), str(db), helper, '1'],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (result.stdout, result.stderr)
        with sqlite3.connect(db) as c:
            # Controlled fixture identity translation, before any service opens it.
            c.execute('UPDATE media SET source=?', (b'\1' + bytes(15),))
            tracks = {path: (id_, album) for id_, album, path in c.execute('SELECT id,album_id,path FROM tracks')}
            assert tracks['first.mp3'][1] == tracks['second.flac'][1]
            outside = root / 'outside.mp3'
            outside.write_bytes(data['first.mp3'])
            stamp = (media / 'first.mp3').stat()
            os.utime(outside, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
            (media / 'escape.mp3').symlink_to(outside)
            columns = [x[1] for x in c.execute('PRAGMA table_info(tracks)') if x[1] != 'id']
            copied = dict(zip(columns, c.execute('SELECT ' + ','.join(columns) + ' FROM tracks WHERE id=?',
                                                (tracks['first.mp3'][0],)).fetchone()))
            copied.update(path='escape.mp3', title='Escape', album_id=tracks['indexed.ogg'][1])
            escaped = c.execute('INSERT INTO tracks(' + ','.join(columns) + ') VALUES(' + ','.join('?' for _ in columns) + ')',
                                [copied[x] for x in columns]).lastrowid
            tracks['escape.mp3'] = escaped, tracks['indexed.ogg'][1]
            originals = list(c.execute('SELECT path,title,codec,size,mtime,duration FROM tracks ORDER BY path'))
        if library_size:
            big_db = root / 'large.db'
            subprocess.run([fixture, 'create', str(big_db), str(library_size)], check=True, timeout=180)
            os.replace(big_db, db)  # only this disposable fixture, all workers exited
            with sqlite3.connect(db) as c:
                tracks = {}
                for index, values in enumerate(originals, 1):
                    c.execute('UPDATE tracks SET path=?,title=?,codec=?,size=?,mtime=?,duration=? WHERE id=?', (*values, index))
                    tracks[values[0]] = (index, c.execute('SELECT album_id FROM tracks WHERE id=?', (index,)).fetchone()[0])
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
        listener.close()
        (root / 'config').mkdir()
        (root / 'config' / 'settings.conf').write_text('readonly=1\n')
        (root / 'web').mkdir()
        left, right = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        env = os.environ | {'SBWEBSERVER_CONFIG_DIR': str(root / 'config')}
        with tempfile.TemporaryFile() as log:
            wp = subprocess.Popen([web, '--port', str(port), '--web-root', str(root / 'web'),
                                   '--test-music-ipc', str(right.fileno())],
                                  pass_fds=(right.fileno(),), env=env, stdout=log, stderr=log)
            bp = subprocess.Popen([base, '--test-music-db', str(db), '--test-music-root', str(media),
                                   '--test-playback-ipc', str(left.fileno()), '--test-playback-http-port', str(port),
                                   '--test-music-mock-adapters'], pass_fds=(left.fileno(),), stdout=log, stderr=log)
            left.close()
            right.close()
            players = []
            number = 0
            observed_commands, observed_starts, observed_events = [], [], []

            def request(path, payload=None, timeout=25):
                c = http.client.HTTPConnection('127.0.0.1', 9000, timeout=timeout)
                c.request('GET' if payload is None else 'POST', path,
                          None if payload is None else json.dumps(payload), {'Content-Type': 'application/json'})
                r = c.getresponse()
                body = r.read()
                c.close()
                return r.status, json.loads(body)

            def command(cmd):
                nonlocal number
                number += 1
                request_id = str(number)
                status, result = request('/cometd', [{'channel': '/slim/request', 'clientId': 'wp4', 'id': request_id,
                                                     'data': {'request': [PLAYER, cmd], 'response': '/wp4/reply'}}])
                assert status == 200 and len(result) == 2, result
                assert result[0]['id'] == request_id and result[1]['id'] == request_id
                if os.environ.get('SB_WP4_EXAMPLES') and len(observed_commands) < 64:
                    observed_commands.append({'command': cmd, 'request_id': request_id, 'comet_response': result})
                return result[0]['successful'], result[1]['data']

            def wait(predicate, seconds=5):
                until = time.monotonic() + seconds
                while time.monotonic() < until:
                    found = predicate()
                    if found:
                        return found
                    time.sleep(.01)
                raise AssertionError('condition deadline')

            class Player:
                def __init__(self):
                    self.socket = socket.create_connection(('127.0.0.1', 3483), timeout=5)
                    self.socket.settimeout(25)
                    self.starts = queue.Queue()
                    self.errors = []
                    self.jiffies = 0
                    self.lock = threading.Lock()
                    self.thread = threading.Thread(target=self.reader, daemon=True)
                    self.thread.start()
                    self.send(b'HELO', bytes([7, 0, 2, 0, 0, 0, 0, 1]) + bytes(26) + b'ENModel=fab4,mp3,flc,ogg')
                    players.append(self)

                def send(self, opcode, body):
                    with self.lock:
                        self.socket.sendall(opcode + struct.pack('!I', len(body)) + body)

                def stat(self, code, elapsed=0):
                    self.jiffies += 1
                    b = bytearray(51)
                    b[:4] = code.encode()
                    struct.pack_into('!I', b, 25, self.jiffies)
                    struct.pack_into('!I', b, 43, elapsed)
                    if os.environ.get('SB_WP4_EXAMPLES') and len(observed_events) < 64:
                        observed_events.append({'kind': 'mocked firmware STAT', 'code': code,
                                                'jiffies': self.jiffies, 'elapsed_ms': elapsed, 'payload_hex': b.hex()})
                    self.send(b'STAT', b)

                def read(self, n):
                    result = bytearray()
                    while len(result) < n:
                        part = self.socket.recv(n - len(result))
                        if not part:
                            raise EOFError()
                        result += part
                    return bytes(result)

                def reader(self):
                    try:
                        while True:
                            n = struct.unpack('!H', self.read(2))[0]
                            frame = self.read(n)
                            if frame[:4] != b'strm':
                                continue
                            payload = frame[4:]
                            if payload[0] == ord('s'):
                                if os.environ.get('SB_WP4_EXAMPLES') and len(observed_starts) < 16:
                                    observed_starts.append({'kind': 'native serialized strm-s', 'frame_hex': (struct.pack('!H', n) + frame).hex()})
                                path = payload[24:].decode().split(' ')[1]
                                c = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
                                c.request('GET', path)
                                response = c.getresponse()
                                content = response.read()
                                assert response.status == 200 and content in data.values(), (response.status, content[:50])
                                c.close()
                                self.send(b'RESP', b'HTTP/1.0 200 OK\r\nContent-Length: ' + str(len(content)).encode() + b'\r\n\r\n')
                                self.starts.put((path, chr(payload[2]), content))
                            elif payload[0] == ord('p'):
                                self.stat('STMp', 100)
                            elif payload[0] == ord('u'):
                                self.stat('STMr', 100)
                            elif payload[0] == ord('q'):
                                self.stat('STMf')  # deliberately not physical release proof
                    except (EOFError, ConnectionResetError, BrokenPipeError):
                        pass
                    except Exception as exc:
                        self.errors.append(repr(exc))

                def close(self):
                    self.socket.close()

            try:
                for _ in range(200):
                    try:
                        if request('/health')[0] == 200:
                            break
                    except OSError:
                        if bp.poll() is not None or wp.poll() is not None:
                            log.seek(0)
                            raise AssertionError(log.read().decode(errors='replace'))
                        time.sleep(.02)
                else:
                    raise AssertionError('Base startup deadline')
                player = Player()
                wait(lambda: request('/test/music/owner')[1]['connection'] == 1)
                wait(lambda: request('/test/music/adapter/idle')[0] == 200)
                if initial_owner:
                    assert request('/test/music/adapter/owner/' + initial_owner)[0] == 200
                    _, lease = request('/api/nowplaying/claim', {'source': initial_owner, 'name': initial_owner})
                    assert not command(['playlistcontrol', 'cmd:load', 'track_id:999999', f'queue_context:{request("/test/music/owner")[1]["context"]}'])[0]
                    assert request('/test/music/owner')[1]['owner'] in (1, 2, 3)
                success, albums = command(['browselibrary', 'items', 0, 50,
                                            'mode:tracks' if library_size else 'mode:albums', 'menu:1'] +
                                           ([f'album_id:{tracks["first.mp3"][1]}'] if library_size else []))
                assert success
                playable = next(x for x in albums['item_loop'] if x['id'] == str(tracks['first.mp3'][0 if library_size else 1]))
                action = playable['actions']['play']
                context = action['params']['queue_context']
                actual = [*action['cmd'], 0, 200, *[f'{k}:{v}' for k, v in action['params'].items()], 'useContextMenu:1']
                if initial_owner:
                    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
                        pending = pool.submit(command, actual)
                        wait(lambda: request('/test/music/owner')[1]['requested'] == 4)
                        # Metadata release has no physical decoder authority.
                        assert request('/api/nowplaying/release', {'session_id': lease['session_id']})[0] == 200
                        assert request('/test/music/owner')[1]['owner'] in (1, 2, 3)
                        assert player.starts.empty() and not pending.done()
                        assert request('/test/music/adapter/yield/accept')[0] == 200
                        completed = pending.result(timeout=10)
                        assert completed[0] and completed[1]['count'] == 2
                else:
                    completed = command(actual)
                    assert completed[0] and completed[1]['count'] == (1 if library_size else 2)
                first = player.starts.get(timeout=5)
                assert first[1:] == ('m', data['first.mp3'])
                assert request('/api/nowplaying')[1]['track']['title'] == ''
                buffering = command(['status', 0, 50])[1]
                assert 'item_loop' not in buffering and len(buffering['playlist_loop']) == (1 if library_size else 2)
                player.stat('STMs')
                wait(lambda: request('/api/nowplaying')[1]['track']['title'] == 'First track')
                if library_size:
                    stop = threading.Event()
                    health, stream_times, errors = [], [], []

                    def observer():
                        while not stop.is_set():
                            before = time.monotonic()
                            try:
                                assert request('/health', timeout=2)[0] == 200
                                health.append((time.monotonic() - before) * 1000)
                            except Exception as exc:
                                errors.append(repr(exc))
                            stop.wait(.005)

                    def streaming():
                        while not stop.is_set():
                            before = time.monotonic()
                            try:
                                c = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
                                c.request('GET', first[0])
                                r = c.getresponse()
                                assert r.status == 200 and r.read() == data['first.mp3']
                                c.close()
                                stream_times.append((time.monotonic() - before) * 1000)
                            except Exception as exc:
                                errors.append(repr(exc))
                                break

                    threads = [threading.Thread(target=observer), threading.Thread(target=streaming)]
                    for thread in threads:
                        thread.start()
                    latencies = []
                    start = time.monotonic()
                    try:
                        for i in range(120):
                            before = time.monotonic()
                            ok, page = command(['browselibrary', 'items', (i % 3) * 50, 200,
                                                'mode:' + ('artists', 'albums', 'tracks')[i % 3], 'menu:1'])
                            assert ok and len(page['item_loop']) <= 200
                            latencies.append((time.monotonic() - before) * 1000)
                    finally:
                        stop.set()
                        for thread in threads:
                            thread.join(timeout=6)
                    assert not errors and health and stream_times, errors

                    def percentiles(values):
                        ordered = sorted(values)
                        return {key: ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction))]
                                for key, fraction in (('p50', .5), ('p95', .95), ('p99', .99))}

                    def process_metrics(pid):
                        status = Path(f'/proc/{pid}/status').read_text()
                        rss = int(next(x for x in status.splitlines() if x.startswith('VmHWM:')).split()[1])
                        fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
                        thread_cpu = {}
                        for task in Path(f'/proc/{pid}/task').iterdir():
                            task_fields = (task / 'stat').read_text().rsplit(')', 1)[1].split()
                            thread_cpu[task.name] = (int(task_fields[11]) + int(task_fields[12])) / os.sysconf('SC_CLK_TCK')
                        return {'peak_rss_kib': rss, 'fds': len(list(Path(f'/proc/{pid}/fd').iterdir())), 'thread_cpu_seconds': thread_cpu,
                                'cpu_seconds_total': (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')}

                    print(json.dumps({'tracks': library_size, 'browse_ms': percentiles(latencies),
                                      'health_ms': percentiles(health), 'stream_get_ms': percentiles(stream_times),
                                      'stream_get_count': len(stream_times), 'browse_count': len(latencies),
                                      'wall_seconds': time.monotonic() - start,
                                      'base': process_metrics(bp.pid), 'web': process_metrics(wp.pid),
                                      'kind': 'Mock decoder playing plus repeated real authorized HTTP GETs'}))
                    return
                if not initial_owner:
                    with sqlite3.connect(db) as lock:
                        lock.execute('BEGIN EXCLUSIVE')
                        pending_socket = socket.create_connection(('127.0.0.1', 9000), timeout=5)
                        body = json.dumps([{'channel': '/slim/request', 'clientId': 'cancel', 'id': 'cancel',
                                            'data': {'request': [PLAYER, ['playlistcontrol', 'cmd:load',
                                                                         f'track_id:{tracks["first.mp3"][0]}', f'queue_context:{context}']],
                                                     'response': '/cancel/reply'}}]).encode()
                        pending_socket.sendall(b'POST /cometd HTTP/1.1\r\nHost: fixture\r\nContent-Length: ' +
                                               str(len(body)).encode() + b'\r\n\r\n' + body)
                        time.sleep(.03)
                        assert not command(['playlistcontrol', 'cmd:add', f'track_id:{tracks["first.mp3"][0]}', f'queue_context:{context}'])[0]
                        before = time.monotonic()
                        assert request('/health', timeout=2)[0] == 200
                        assert time.monotonic() - before < .5
                        pending_socket.close()
                        lock.rollback()
                    wait(lambda: request('/test/music/owner')[1]['error'] == 'action_cancelled')
                    assert request('/test/music/owner')[1]['count'] == 2
                success, status = command(['status', 0, 50])
                assert success and status['playlist_tracks'] == 2 and status['playlist_cur_index'] == 0
                assert status['item_loop'][0]['params']['track_id'] == str(tracks['first.mp3'][0])
                assert 'remote' not in status and 'can_seek' not in status and status['rate'] == 1
                success, page = command(['status', 1, 1])
                assert success and page['count'] == 1 and page['offset'] == 1
                assert page['playlist_loop'][0]['track_id'] == str(tracks['second.flac'][0])
                assert command(['pause', 1])[0]
                wait(lambda: command(['status', 0, 50])[1]['mode'] == 'pause')
                frozen = request('/api/nowplaying')[1]['position_ms']
                time.sleep(.03)
                assert request('/api/nowplaying')[1]['position_ms'] == frozen
                assert command(['pause', 0])[0]
                wait(lambda: command(['status', 0, 50])[1]['mode'] == 'play')
                # No start/advance at decoder-ready alone.
                player.stat('STMd', 200)
                time.sleep(.02)
                assert request('/test/music/owner')[1]['intended'] == 0
                player.stat('STMu', 200)
                wait(lambda: request('/test/music/owner')[1]['intended'] == 1)
                assert player.starts.empty()
                second_player = Player()
                wait(lambda: request('/test/music/owner')[1]['connection'] == 2)
                wait(lambda: request('/test/music/adapter/idle')[0] == 200)
                second = second_player.starts.get(timeout=8)
                assert second[1:] == ('f', data['second.flac'])
                second_player.stat('STMs')
                wait(lambda: request('/api/nowplaying')[1]['track']['title'] == TITLE)
                # Add/insert are metadata transactions and do not restart audio.
                assert command(['playlistcontrol', 'cmd:add', f'track_id:{tracks["first.mp3"][0]}', f'queue_context:{context}'])[0]
                assert command(['playlistcontrol', 'cmd:insert', f'track_id:{tracks["first.mp3"][0]}', f'queue_context:{context}'])[0]
                assert request('/test/music/owner')[1]['count'] == 4 and second_player.starts.empty()
                assert not command(['playlistcontrol', 'cmd:load', f'track_id:{tracks["indexed.ogg"][0]}', f'queue_context:{context}'])[0]
                assert request('/test/music/owner')[1]['count'] == 4
                assert not command(['playlistcontrol', 'cmd:load', f'track_id:{tracks["escape.mp3"][0]}', f'queue_context:{context}'])[0]
                assert request('/test/music/owner')[1]['count'] == 4
                assert not command(['playlistcontrol', 'cmd:load', 'track_id:999999', f'queue_context:{context}'])[0]
                assert request('/test/music/owner')[1]['count'] == 4
                verified = [first[2], second[2]]
                if not initial_owner:
                    assert command(['playlist', 'delete', 3])[0]
                    assert request('/test/music/owner')[1]['count'] == 3

                    def reconnect(expected_codec, expected_bytes):
                        previous_connection = request('/test/music/owner')[1]['connection']
                        current = Player()
                        wait(lambda: request('/test/music/owner')[1]['connection'] > previous_connection)
                        wait(lambda: request('/test/music/adapter/idle')[0] == 200)
                        try:
                            stream = current.starts.get(timeout=8)
                        except queue.Empty:
                            raise AssertionError(request('/test/music/owner')[1])
                        assert stream[1:] == (expected_codec, expected_bytes)
                        verified.append(stream[2])
                        current.stat('STMs')
                        wait(lambda: command(['status', 0, 50])[1]['mode'] == 'play')
                        return current

                    assert command(['playlist', 'index', '+1'])[0]
                    assert not command(['playlist', 'index', '+1'])[0]  # bounded while reconnect pending
                    third_player = reconnect('m', data['first.mp3'])
                    assert command(['playlist', 'index', '-1'])[0]
                    fourth_player = reconnect('f', data['second.flac'])
                    assert command(['stop'])[0]
                    wait(lambda: command(['status', 0, 50])[1]['mode'] == 'stop')
                    assert command(['play'])[0]
                    second_player = reconnect('f', data['second.flac'])
                entries_before_removal = request('/test/music/owner')[1]['count']
                if initial_owner:
                    assert request('/test/music/adapter/owner/' + initial_owner)[0] == 200
                    wait(lambda: request('/test/music/owner')[1]['requested'] in (1, 2, 3))
                    wait(lambda: request('/test/music/adapter/idle')[0] == 200)
                    wait(lambda: request('/test/music/owner')[1]['context'] != int(context))
                    assert not command(['playlistcontrol', 'cmd:add', f'track_id:{tracks["first.mp3"][0]}', f'queue_context:{context}'])[0]
                assert request('/test/music/offline')[0] == 200
                wait(lambda: request('/test/music/owner')[1]['phase'] == 8)
                assert not command(['playlistcontrol', 'cmd:load', f'track_id:{tracks["first.mp3"][0]}', f'queue_context:{context}'])[0]
                assert request('/test/music/online')[0] == 200
                time.sleep(.03)
                assert second_player.starts.empty()  # reinsertion never auto-starts
                if not initial_owner:
                    assert command(['playlist', 'clear'])[0]
                    assert command(['status', 0, 50])[1]['playlist_tracks'] == 0
                    assert not command(['playlist', 'index', '+1'])[0]
                assert all(not p.errors for p in players), [p.errors for p in players]
                if os.environ.get('SB_WP4_EXAMPLES'):
                    Path(os.environ['SB_WP4_EXAMPLES']).write_text(json.dumps({
                        'kind': 'Observed native host fixture requests/responses and starts; firmware events mocked; no hardware audio',
                        'commands': observed_commands, 'starts': observed_starts, 'events': observed_events},
                        ensure_ascii=False, indent=2) + '\n', encoding='utf8')
                print(json.dumps({'native_starts': len(verified), 'verified_encoded_bytes': sum(map(len, verified)),
                                  'queue_entries_before_removal': entries_before_removal, 'audio_output': 'mocked, not measured'}))
            except Exception:
                log.seek(0)
                print(log.read().decode(errors='replace')[-12000:], file=sys.stderr)
                raise
            finally:
                bp.terminate()
                bp.wait(timeout=8)
                wp.terminate()
                wp.wait(timeout=5)
                for p in players:
                    p.close()


if __name__ == '__main__':
    if len(sys.argv) > 5 and sys.argv[5] == 'benchmark':
        run(*sys.argv[1:5], library_size=int(sys.argv[6]), fixture=str(Path(sys.argv[7]).resolve()))
    elif len(sys.argv) > 5 and sys.argv[5] == 'owners':
        for owner in ('radio', 'spotify', 'qobuz'):
            run(*sys.argv[1:5], initial_owner=owner)
    else:
        run(*sys.argv[1:5])
