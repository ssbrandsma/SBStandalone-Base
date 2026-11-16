"""Exercise real daemon, authenticated IPC and Base HTTP gateway."""
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


def port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def exercise(binary, base, udn, music_base=None, music_web=None, expiry_location=None):
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect(("192.0.2.1", 9))
    ip = probe.getsockname()[0]
    probe.close()
    with tempfile.TemporaryDirectory(prefix="sbupnp-production-") as directory:
        root = Path(directory)
        path = str(root / "control.sock")
        daemon = subprocess.Popen([binary, "--daemon", "--socket", path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        bootstrap = None
        try:
            deadline = time.monotonic() + 10
            while not (root / "token").exists():
                assert daemon.poll() is None, daemon.communicate()
                assert time.monotonic() < deadline
                time.sleep(0.05)
            sequence = 0

            def ipc(operation, generation=0, **values):
                nonlocal sequence
                sequence += 1
                wire = bytearray(4096)
                wire[:8] = b"SBUPNP1\0"
                struct.pack_into(">6Q", wire, 8, sequence, sequence, generation, operation, values.get("offset", 0), 50)
                at = 64
                for key in ("server", "object", "field", "text", "sort"):
                    value = values.get(key, "").encode()
                    struct.pack_into(">Q", wire, at, len(value))
                    at += 8
                    wire[at:at + len(value)] = value
                    at += len(value)
                with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
                    s.settimeout(20)
                    s.connect(path)
                    s.send(wire)
                    result = s.recv(131112)
                assert result[:8] == b"SBUPNR1\0", result[:40]
                req, session, status, length = struct.unpack_from(">4Q", result, 8)
                assert req == session == sequence and len(result) == length + 40
                return status, json.loads(result[40:])

            assert ipc(1)[0] == 200
            emit('sbupnp','idle before discovery',daemon)
            servers = []
            while not servers:
                assert time.monotonic() < deadline, "fixture not discovered by daemon"
                status, value = ipc(2)
                servers = value["servers"]
                time.sleep(0.05)
            server = next(v for v in servers if v["udn"] == udn)
            args = {"server": udn, "object": "0"}
            generation = server["generation"]
            emit('sbupnp','discovered',daemon)
            assert ipc(3, generation, **args)[1]["items"][0]["id"] == "server/opaque&id"
            assert ipc(4, generation, **args)[1]["items"][0]["searchable"]
            assert ipc(5, generation, **args)[1]["search"] == "dc:title,upnp:artist"
            assert ipc(6, generation, **args, field="dc:title", text='quoted " text')[0] == 200
            assert ipc(6, generation, **args, field="upnp:genre", text="unsupported")[0] == 400
            assert ipc(7, generation, **args)[1]["update_id"] == 7
            assert ipc(3, generation + 1, **args)[0] == 409
            identifiers = []
            for offset, count in ((0, 50), (50, 50), (100, 37)):
                status, page = ipc(3, generation, server=udn, object="large", offset=offset)
                assert status == 200 and page["count"] == count and page["total"] == 137, page
                identifiers.extend(item["id"] for item in page["items"])
            assert identifiers == [f"opaque:{i}" for i in range(137)]
            emit('sbupnp','browse search pagination',daemon)
            # Exercise repeated RPC allocations and descriptor cleanup, rather
            # than measuring only a fresh process with an empty registry.
            def resources():
                values = dict(line.split(":", 1) for line in Path(f"/proc/{daemon.pid}/status").read_text().splitlines() if ":" in line)
                return int(values["VmRSS"].split()[0]), len(list(Path(f"/proc/{daemon.pid}/fd").iterdir())), int(values["Threads"])
            before = resources()
            for _ in range(100):
                assert ipc(3, generation, **args)[0] == 200
            after = resources()
            assert after[0] - before[0] < 16384 and after[1] <= before[1] + 4, (before, after)
            print(f"host repeated browse: 100 requests; RSS KiB/fds/threads {before} -> {after}")
            duplicate = subprocess.run([binary, "--daemon", "--socket", path], capture_output=True, timeout=10)
            assert duplicate.returncode != 0, "duplicate daemon was admitted"
            http_port = 9000
            env = dict(os.environ, SB_UPNP_SOCKET=path)
            bootstrap = subprocess.Popen([base], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            time.sleep(0.3)
            assert bootstrap.poll() is None, bootstrap.communicate()
            token = (root / "token").read_text()

            def gateway(args, authorized=True):
                c = http.client.HTTPConnection("127.0.0.1", http_port, timeout=15)
                headers = {"Content-Type": "application/json"}
                if authorized:
                    headers["X-Standalone-UPnP"] = token
                c.request("POST", "/api/network-music", json.dumps({"data": {"request": ["", ["networkmusic", *[f"{k}:{v}" for k, v in args.items()]]]}}), headers)
                response = c.getresponse()
                body = json.loads(response.read())
                status = response.status
                c.close()
                return status, body

            assert gateway({"op": "health"}, False)[0] == 403
            reply = gateway({"op": "servers"})
            assert reply[0] == 200 and reply[1].get("servers", [{}])[0].get("udn") == udn, reply
            status, page = gateway(dict(op="browse", generation=generation, **args))
            assert status == 200 and page["items"][1]["playable"], (status, page)
            status, page = gateway(dict(op="search", generation=generation, field="dc:title", text='escaped " & \\ text', **args))
            assert status == 200 and page["count"] == 2, (status, page)
            print("production: daemon, duplicate exclusion, private IPC, capabilities, BrowseMetadata, Search, UpdateID, stale generations, Base authentication and async gateway passed")
            bootstrap.terminate()
            bootstrap.communicate(timeout=10)
            bootstrap = None
            if music_base and music_web:
                from gateway_playback import exercise as gateway_chain
                gateway_chain(music_base, music_web, path, token, udn, generation)
                gateway_chain(music_base, music_web, path, token, udn, generation, container=True)
            from renderer_controller import exercise_renderer
            exercise_renderer(ipc, ip, root / "token", f"http://{ip}:8200/music.mp3", observe=lambda phase:emit('sbupnp',phase,daemon))
            duration = int(os.environ.get('SB_UPNP_SOAK_SECONDS','0'))
            if duration:
                assert 1 <= duration <= 7200 and music_base and music_web
                start = time.monotonic()
                samples = []
                iterations = cycles = 0
                next_cycle = start
                initial = resources()
                cpu_before = sum(int(v) for v in Path(f'/proc/{daemon.pid}/stat').read_text().split()[13:15])
                while time.monotonic() - start < duration:
                    assert daemon.poll() is None
                    assert ipc(3,generation,**args)[0] == 200
                    assert ipc(6,generation,**args,field='dc:title',text='soak')[0] == 200
                    iterations += 1
                    if time.monotonic() >= next_cycle:
                        before_cycle = time.monotonic()
                        if cycles%10==0 and expiry_location:
                            expired_udn=udn+'-expiry'
                            notify=f'NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nCACHE-CONTROL: max-age=2\r\nLOCATION: {expiry_location}\r\nNT: urn:schemas-upnp-org:device:MediaServer:1\r\nNTS: ssdp:alive\r\nUSN: {expired_udn}::urn:schemas-upnp-org:device:MediaServer:1\r\nSERVER: expiry-fixture/1.0 UPnP/1.0 fixture/1.0\r\n\r\n'
                            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
                                sender.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_IF,socket.inet_aton(ip))
                                sender.sendto(notify.encode(),('239.255.255.250',1900))
                            deadline=time.monotonic()+3
                            while not any(v['udn']==expired_udn for v in ipc(2)[1]['servers']):
                                assert time.monotonic()<deadline,'expiry fixture not discovered'
                                time.sleep(.05)
                            deadline=time.monotonic()+5
                            while any(v['udn']==expired_udn for v in ipc(2)[1]['servers']):
                                assert time.monotonic()<deadline,'expired server remained registered'
                                time.sleep(.05)
                        gateway_chain(music_base,music_web,path,token,udn,generation,container=True)
                        exercise_renderer(ipc,ip,root/'token',f'http://{ip}:8200/music.mp3')
                        cycles += 1
                        measured = resources()
                        sample = dict(elapsed_seconds=time.monotonic()-start,rss_kib=measured[0],fds=measured[1],threads=measured[2],cycle_seconds=time.monotonic()-before_cycle)
                        samples.append(sample)
                        assert measured[0] < initial[0]+16384 and measured[1] <= initial[1]+8 and measured[2] <= initial[2]+16, sample
                        report = dict(target_seconds=duration,elapsed_seconds=time.monotonic()-start,iterations=iterations,cycles=cycles,samples=samples,physical_hardware=False,simulated_decoder=True)
                        output = os.environ.get('SB_UPNP_SOAK_OUTPUT')
                        if output:
                            destination = Path(output)
                            destination.with_suffix('.tmp').write_text(json.dumps(report,indent=2))
                            destination.with_suffix('.tmp').replace(destination)
                        print('soak:',json.dumps(sample),flush=True)
                        next_cycle = time.monotonic()+30
                    time.sleep(.25)
                cpu_after = sum(int(v) for v in Path(f'/proc/{daemon.pid}/stat').read_text().split()[13:15])
                report.update(elapsed_seconds=time.monotonic()-start,iterations=iterations,cycles=cycles,
                    daemon_cpu_seconds=(cpu_after-cpu_before)/os.sysconf('SC_CLK_TCK'),completed=True)
                if output: Path(output).write_text(json.dumps(report,indent=2))
                print('soak complete:',json.dumps(report),flush=True)
        finally:
            for process in (bootstrap, daemon):
                if process is not None:
                    process.terminate()
                    try:
                        _,errors=process.communicate(timeout=20)
                        if sys.exc_info()[0] is None:
                            assert process.returncode==0,(process.returncode,errors.decode(errors='replace'))
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.communicate()
