"""Real Base gateway/queue/resource HTTP chain, with a simulated SlimProto peer."""
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
from resource_metrics import emit


def exercise(base, web, daemon_socket, token, udn, generation, container=False):
    left, right = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    probe = socket.socket()
    probe.bind(('127.0.0.1', 0))
    port = probe.getsockname()[1]
    probe.close()
    processes = []
    player = None
    with tempfile.TemporaryDirectory(prefix='sb-network-chain-') as directory, tempfile.TemporaryFile() as log:
        root = Path(directory)
        (root / 'settings.conf').write_text('readonly=1\n')
        try:
            processes.append(subprocess.Popen([web, '--port', str(port), '--web-root', directory,
                '--test-music-ipc', str(right.fileno())], pass_fds=(right.fileno(),),
                env=dict(os.environ, SBWEBSERVER_CONFIG_DIR=directory), stdout=log, stderr=log))
            processes.append(subprocess.Popen([base, '--test-network-playback', '--test-music-mock-adapters',
                '--test-playback-ipc', str(left.fileno()), '--test-playback-http-port', str(port)],
                pass_fds=(left.fileno(),), env=dict(os.environ, SB_UPNP_SOCKET=daemon_socket), stdout=log, stderr=log))
            left.close()
            right.close()

            def request(path, payload=None):
                started=time.monotonic()
                connection = http.client.HTTPConnection('127.0.0.1', 9000, timeout=10)
                connection.request('GET' if payload is None else 'POST', path,
                    None if payload is None else json.dumps(payload),
                    {'Content-Type': 'application/json', 'X-Standalone-UPnP': token})
                response = connection.getresponse()
                result = response.status, json.loads(response.read())
                connection.close()
                emit('Base HTTP',path,latency_ms=(time.monotonic()-started)*1000,status=result[0])
                return result

            def wait(predicate):
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    try:
                        value = predicate()
                        if value:
                            return value
                    except OSError:
                        pass
                    assert all(p.poll() is None for p in processes)
                    time.sleep(.02)
                raise AssertionError('network chain deadline')

            wait(lambda: request('/health')[0] == 200)
            for name,process in zip(('web','Base'),processes): emit(name,'idle',process)
            player = socket.create_connection(('127.0.0.1', 3483), timeout=10)
            player.settimeout(10)

            def send(opcode, body):
                player.sendall(opcode + struct.pack('!I', len(body)) + body)

            sequence = 0
            def comet(command, successful=True):
                nonlocal sequence
                sequence += 1
                status, reply = request('/cometd', [{'channel':'/slim/request','clientId':'playlist-test','id':str(sequence),
                    'data':{'request':['02:00:00:00:00:01',command],'response':'/playlist-test/reply'}}])
                assert status == 200 and reply[0]['successful'] == successful, reply
                return reply[1]['data']

            def playlist(offset=0, quantity=50):
                return comet(['status',str(offset),str(quantity),'menu:menu','useContextMenu:1'])

            def action(value, successful=True):
                return comet(value['cmd'] + ['0','50'] + [f'{k}:{v}' for k,v in value.get('params',{}).items()], successful)

            send(b'HELO', bytes([7, 0, 2, 0, 0, 0, 0, 1]) + bytes(26) + b'ENModel=fab4,mp3')
            wait(lambda: request('/test/music/adapter/idle')[0] == 200)
            payload = {'data': {'request': ['', ['networkmusic', 'op:playcontainer' if container else 'op:play', f'server:{udn}',
                f'generation:{generation}', 'object:large' if container else 'object:track:$1']]}}
            playback_started=time.monotonic()
            assert request('/api/network-music', payload)[0] == 200
            wait(lambda: request('/test/music/owner')[1]['count'] == (137 if container else 1))
            for name,process in zip(('web','Base'),processes): emit(name,'queue constructed',process,items=137 if container else 1)

            def read(size):
                data = b''
                while len(data) < size:
                    part = player.recv(size - len(data))
                    assert part
                    data += part
                return data

            while True:
                frame = read(struct.unpack('!H', read(2))[0])
                if frame[:5] == b'strms':
                    resource = frame[28:].decode().split(' ')[1]
                    assert resource.startswith('/music/stream/')
                    break
            stream = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            stream.request('GET', resource)
            response = stream.getresponse()
            data = response.read()
            stream.close()
            expected = Path(__file__).resolve().parents[3] / 'tests/music/fixtures/media/base.mp3'
            assert response.status == 200 and data == expected.read_bytes()
            send(b'RESP', b'HTTP/1.0 200 OK\r\nContent-Length: ' + str(len(data)).encode() + b'\r\n\r\n')
            stat = bytearray(51)
            stat[:4] = b'STMs'
            struct.pack_into('!I', stat, 25, 1)
            send(b'STAT', stat)
            jiffies = 1
            def emit_stat(code):
                nonlocal jiffies
                jiffies += 1
                packet = bytearray(51)
                packet[:4] = code.encode()
                struct.pack_into('!I', packet, 25, jiffies)
                send(b'STAT', packet)

            def receive_start(quiesce=False):
                while True:
                    frame = read(struct.unpack('!H', read(2))[0])
                    if frame[:5] == b'strmq':
                        emit_stat('STMf')
                        if quiesce:
                            wait(lambda: request('/test/music/adapter/idle')[0] == 200)
                            quiesce = False
                    if frame[:5] != b'strms':
                        continue
                    path = frame[28:].decode().split(' ')[1]
                    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
                    connection.request('GET',path)
                    response = connection.getresponse()
                    content = response.read()
                    connection.close()
                    assert response.status == 200 and content == expected.read_bytes()
                    send(b'RESP',b'HTTP/1.0 200 OK\r\nContent-Length: '+str(len(content)).encode()+b'\r\n\r\n')
                    emit_stat('STMs')
                    return
            wait(lambda: request('/test/music/owner')[1]['owner'] == 4)
            wait(lambda: playlist()['mode'] == 'play')
            for name,process in zip(('web','Base'),processes): emit(name,'simulated playback',process,startup_ms=(time.monotonic()-playback_started)*1000)
            current = playlist()
            assert current['count'] == (137 if container else 1) and current['playlist_cur_index'] == 0, current
            assert len(current['item_loop']) == (50 if container else 1)
            if container:
                whole = playlist(0,200)
                assert whole['count'] == 137 and len(whole['item_loop']) == 137
                if os.environ.get('SB_JIVE_PLAYLIST_EVIDENCE'):
                    Path(os.environ['SB_JIVE_PLAYLIST_EVIDENCE']).write_text(json.dumps(whole))
                later = playlist(100)
                assert later['count'] == 137 and later['offset'] == 100 and len(later['item_loop']) == 37
                assert later['item_loop'][0]['playlist index'] == 100
                assert len({item['queue_entry_id'] for page in (current,playlist(50),later) for item in page['item_loop']}) == 137
                old_action = current['item_loop'][0]['actions']['go']
                revision = current['queue_revision']
                epoch = request('/test/music/owner')[1]['epoch']
                comet(['playlist','move','0','2',f'queue_revision:{revision}',f'queue_entry_id:{current["item_loop"][0]["queue_entry_id"]}'])
                moved = playlist()
                assert moved['playlist_cur_index'] == 2 and moved['queue_revision'] > revision
                assert request('/test/music/owner')[1]['epoch'] == epoch, 'reorder restarted decoder'
                action(old_action, successful=False)
                context = action(moved['item_loop'][0]['actions']['more'])
                assert context['window']['isContextMenu'] == 1 and context['count'] >= 3
                remove = next(item for item in context['item_loop'] if item['text'] == 'Remove')
                action(remove['actions']['go'])
                assert playlist()['count'] == 136 and playlist()['playlist_cur_index'] == 1
                selected = playlist(50)['item_loop'][3]
                action(selected['actions']['go'])
                receive_start(quiesce=True)
                wait(lambda: playlist()['playlist_cur_index'] == 53 and playlist()['mode'] == 'play')
                emit_stat('STMd')
                emit_stat('STMu')
                receive_start()
                wait(lambda: playlist()['playlist_cur_index'] == 54 and playlist()['mode'] == 'play')
            comet(['pause','1'])
            emit_stat('STMp')
            wait(lambda: playlist()['mode'] == 'pause')
            assert playlist()['count'] == (136 if container else 1)
            comet(['pause','0'])
            emit_stat('STMr')
            wait(lambda: playlist()['mode'] == 'play')
            comet(['stop'])
            emit_stat('STMf')
            # A firmware flush is not physical idle proof. The fixture's
            # explicit adapter attests quiescence, as the real Jive fence must.
            wait(lambda: request('/test/music/adapter/idle')[0] == 200)
            wait(lambda: playlist()['mode'] == 'stop')
            retained = playlist()
            assert retained['count'] == (136 if container else 1)
            # An external metadata lease changes presentation, not ownership.
            stale_action = retained['item_loop'][0]['actions']['go']
            status, lease = request('/api/nowplaying/claim',{'source':'spotify','name':'Spotify','capabilities':{'next':True}})
            assert status == 201
            publisher = {'session_id':lease['session_id'],'generation':lease['generation']}
            assert request('/api/nowplaying/update',dict(publisher,state='playing',track={'id':'spotify:track:fixture','title':'External fixture'}))[0] == 200
            foreign = playlist()
            assert foreign['playlist_tracks'] == 1 and foreign['playlist_provider'] == 'spotify'
            assert not foreign['playlist_capabilities']['select']
            assert foreign['remoteMeta']['shuffle'] and foreign['remoteMeta']['repeat']
            assert foreign['remoteMeta']['buttons']['shuffle']['jiveStyle'] == 'shuffleDisabled'
            assert request('/test/music/owner')[1]['owner'] == 0
            action(stale_action,successful=False)
            assert request('/test/music/owner')[1]['count'] == retained['count']
            assert request('/api/nowplaying/release',publisher)[0] == 200
            comet(['playlist','clear',f'queue_revision:{retained["queue_revision"]}'])
            empty = playlist()
            assert empty['count'] == 0 and not empty.get('item_loop')
            for name,process in zip(('web','Base'),processes): emit(name,'stopped cleared after mutations',process)
            print('original Jive playlist: actual menu:menu pagination, count, current index, stable identities, move without decoder restart, stale-action rejection and context-menu remove passed')
            print(f'network chain ({"137-item paginated container" if container else "track metadata"}): actual SOAP -> Base async selection -> typed queue -> SlimProto token -> real web HTTP MP3 bytes; simulated firmware peer passed')
        except Exception:
            print('native chain process exit codes:', [process.poll() for process in processes])
            log.seek(0)
            print(log.read().decode(errors='replace'))
            raise
        finally:
            if player:
                player.close()
            left.close()
            right.close()
            for process in reversed(processes):
                process.terminate()
                process.wait(timeout=10)
                if sys.exc_info()[0] is None:
                    assert process.returncode == 0, process.returncode
