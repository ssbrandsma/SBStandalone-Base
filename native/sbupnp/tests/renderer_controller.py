"""Independent SOAP/GENA controller with an explicitly simulated Base backend."""
import html
import http.client
import http.server
import json
import socket
import threading
import time
from urllib.parse import urljoin, urlsplit
import xml.etree.ElementTree as ET


def exercise_renderer(ipc, ip, token_path, media_uri, observe=None):
    state = dict(state="STOPPED", owned=False, busy=False, awake=True, volume=50, position_ms=0, duration_ms=3000, uri="")
    requests, events = [], []
    token = token_path.read_text()

    class Backend(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            assert self.path == "/api/network-music"
            assert self.headers["X-Standalone-UPnP"] == token
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            args = dict(v.split(":", 1) for v in body["data"]["request"][1][1:])
            requests.append(args)
            status, result = 200, state
            if args["op"] != "renderstate":
                if state["busy"] or not state["awake"]:
                    status, result = 409, {"error": "not_admitted"}
                elif args["op"] == "renderplay":
                    state.update(state="TRANSITIONING", owned=True, uri=args["uri"])
                    result = {"accepted": True}
                else:
                    command = args["command"]
                    if command == "pause":
                        state["state"] = "PAUSED_PLAYBACK"
                    elif command == "stop":
                        state.update(state="STOPPED", owned=False)
                    elif command == "volume":
                        state["volume"] = int(args["value"])
                    result = {"accepted": True}
            payload = json.dumps(result, separators=(",", ":")).encode()
            self.send_response(status)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    class Callback(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_NOTIFY(self):
            body = self.rfile.read(int(self.headers["Content-Length"]))
            events.append((self.headers["SID"], int(self.headers["SEQ"]), ET.fromstring(body)))
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()

    backend = http.server.ThreadingHTTPServer(("127.0.0.1", 9000), Backend)
    callback = http.server.ThreadingHTTPServer((ip, 0), Callback)
    for server in (backend, callback):
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
    uuid = "uuid:12345678-1234-4234-8234-123456789abc"
    try:
        assert ipc(1)[1]["renderer"] is False
        status, result = ipc(8, server=uuid, object="Fixture Squeezebox", text="1")
        if observe: observe('renderer enabled')
        assert status == 200, result
        port = ipc(1)[1]["renderer_port"]
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        udp.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ip))
        udp.settimeout(0.5)
        search = b'M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\nMX: 1\r\nST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n'
        udp.sendto(search, ("239.255.255.250", 1900))
        location = None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                packet, _ = udp.recvfrom(8192)
            except socket.timeout:
                continue
            headers = {line.split(":", 1)[0].lower(): line.split(":", 1)[1].strip() for line in packet.decode().split("\r\n")[1:] if ":" in line}
            if headers.get("st") == "urn:schemas-upnp-org:device:MediaRenderer:1" and uuid in headers.get("usn", ""):
                location = headers["location"]
                break
        udp.close()
        assert location, "renderer SSDP response missing"

        def exchange(method, path, body=None, headers=None):
            c = http.client.HTTPConnection(ip, port, timeout=10)
            c.request(method, path, body=body, headers=headers or {})
            response = c.getresponse()
            result = response.status, dict(response.getheaders()), response.read()
            c.close()
            return result

        status, _, body = exchange("GET", urlsplit(location).path)
        assert status == 200
        description = ET.fromstring(body)
        namespace = {"d": "urn:schemas-upnp-org:device-1-0"}
        assert description.find("d:device/d:UDN", namespace).text == uuid
        services = {}
        for service in description.findall("d:device/d:serviceList/d:service", namespace):
            name = service.find("d:serviceType", namespace).text.split(":")[-2]
            services[name] = {node.tag.split("}")[-1]: node.text for node in service}
            status, _, data = exchange("GET", service.find("d:SCPDURL", namespace).text)
            assert status == 200
            schema = ET.fromstring(data)
            ns = {"s": "urn:schemas-upnp-org:service-1-0"}
            variables = {v.find("s:name", ns).text for v in schema.findall("s:serviceStateTable/s:stateVariable", ns)}
            for related in schema.findall(".//s:relatedStateVariable", ns):
                assert related.text in variables

        def soap(service, action, **arguments):
            kind = f"urn:schemas-upnp-org:service:{service}:1"
            args = "".join(f"<{key}>{html.escape(str(value))}</{key}>" for key, value in arguments.items())
            body = f'<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:{action} xmlns:u="{kind}">{args}</u:{action}></s:Body></s:Envelope>'
            status, _, response = exchange("POST", services[service]["controlURL"], body.encode(), {"Content-Type": "text/xml; charset=utf-8", "SOAPACTION": f'"{kind}#{action}"'})
            root = ET.fromstring(response)
            values = {node.tag.split("}")[-1]: node.text or "" for node in root.iter()}
            return status, values

        assert soap("ConnectionManager", "GetProtocolInfo")[1]["Sink"] == "http-get:*:audio/mpeg:*"
        assert soap("AVTransport", "GetTransportInfo", InstanceID=9)[1]["errorCode"] == "718"
        assert soap("AVTransport", "SetAVTransportURI", InstanceID=0, CurrentURI="file:///etc/passwd", CurrentURIMetaData="")[0] == 500
        before = len([r for r in requests if r["op"] == "renderplay"])
        assert soap("AVTransport", "SetAVTransportURI", InstanceID=0, CurrentURI=media_uri, CurrentURIMetaData="")[0] == 200
        assert len([r for r in requests if r["op"] == "renderplay"]) == before
        state["busy"] = True
        assert soap("AVTransport", "Play", InstanceID=0, Speed="1")[0] == 500
        state.update(busy=False, awake=False)
        assert soap("AVTransport", "Play", InstanceID=0, Speed="1")[0] == 500
        state["awake"] = True
        assert soap("AVTransport", "Play", InstanceID=0, Speed="1")[0] == 200
        assert soap("AVTransport", "GetTransportInfo", InstanceID=0)[1]["CurrentTransportState"] == "TRANSITIONING"
        # Simulate a correlated decoder event; accepting Play did not set PLAYING.
        state["state"] = "PLAYING"
        assert soap("AVTransport", "GetTransportInfo", InstanceID=0)[1]["CurrentTransportState"] == "PLAYING"
        assert soap("AVTransport", "Pause", InstanceID=0)[0] == 200
        assert soap("AVTransport", "GetTransportInfo", InstanceID=0)[1]["CurrentTransportState"] == "PAUSED_PLAYBACK"
        assert soap("RenderingControl", "SetVolume", InstanceID=0, Channel="Master", DesiredVolume=37)[0] == 200
        assert soap("RenderingControl", "GetVolume", InstanceID=0, Channel="Master")[1]["CurrentVolume"] == "37"
        assert soap("RenderingControl", "SetMute", InstanceID=0, Channel="Master", DesiredMute=1)[0] == 200
        assert soap("RenderingControl", "GetMute", InstanceID=0, Channel="Master")[1]["CurrentMute"] == "1"
        event_path = services["AVTransport"]["eventSubURL"]
        status, headers, _ = exchange("SUBSCRIBE", event_path, headers={"CALLBACK": f"<http://{ip}:{callback.server_port}/events>", "NT": "upnp:event", "TIMEOUT": "Second-60"})
        assert status == 200, (status, headers)
        sid = next(value for key, value in headers.items() if key.lower() == "sid")
        deadline = time.monotonic() + 3
        while not events and time.monotonic() < deadline:
            time.sleep(0.05)
        assert events and events[0][0] == sid and events[0][1] == 0, events
        assert exchange("SUBSCRIBE", event_path, headers={"SID": sid, "TIMEOUT": "Second-60"})[0] == 200
        assert exchange("SUBSCRIBE", event_path, headers={"CALLBACK": "<http://127.0.0.1:9000/privileged>", "NT": "upnp:event", "TIMEOUT": "Second-60"})[0] == 412
        assert exchange("UNSUBSCRIBE", event_path, headers={"SID": sid})[0] == 200
        assert soap("AVTransport", "Seek", InstanceID=0, Unit="REL_TIME", Target="0:00:01")[1]["errorCode"] == "710"
        assert soap("AVTransport", "Stop", InstanceID=0)[0] == 200
        assert soap("AVTransport", "GetTransportInfo", InstanceID=0)[1]["CurrentTransportState"] == "STOPPED"
        assert ipc(8, server=uuid, object="Fixture Squeezebox", text="0")[0] == 200
        assert ipc(1)[1]["renderer"] is False
        if observe: observe('renderer disabled')
        print("renderer: SSDP, descriptions, SOAP, simulated Base state correlation, ownership/standby rejection, volume/mute, GENA subscribe/renew/unsubscribe, callback origin guard and disable passed")
    finally:
        ipc(8, server=uuid, object="Fixture Squeezebox", text="0")
        for server in (callback, backend):
            server.shutdown()
            server.server_close()
