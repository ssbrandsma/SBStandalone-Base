"""Independent standard-library MediaServer fixture; no production device access."""
import html
import http.server
import json
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

binary = sys.argv[1]
probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
probe.connect(("192.0.2.1", 9))  # determines route; sends no datagram
ip = probe.getsockname()[0]
probe.close()
udn = "uuid:sbupnp-independent-fixture"
calls = []
searches = []
didl = """<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">
<container id="server/opaque&amp;id" parentID="0" searchable="1"><dc:title>Server category</dc:title></container>
<item id="track:$1" parentID="0"><dc:title>Fixture MP3</dc:title><upnp:class>object.item.audioItem.musicTrack</upnp:class><res protocolInfo="http-get:*:audio/mpeg:*">/music.mp3</res></item></DIDL-Lite>"""


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send_xml(self, body):
        body = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/xml; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/music.mp3"):
            media = (Path(__file__).resolve().parents[3] / "tests/music/fixtures/media/base.mp3").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "audio/mpeg")
            self.send_header("Content-Length", str(len(media)))
            self.end_headers()
            self.wfile.write(media)
            return
        assert self.path in ("/description.xml","/expiry.xml"), self.path
        descriptor_udn = udn + '-expiry' if self.path == '/expiry.xml' else udn
        self.send_xml(f"""<root xmlns="urn:schemas-upnp-org:device-1-0"><specVersion><major>1</major><minor>0</minor></specVersion><device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>Fixture NAS</friendlyName><UDN>{descriptor_udn}</UDN><serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId><SCPDURL>/cds.xml</SCPDURL><controlURL>/control</controlURL><eventSubURL>/event</eventSubURL></service></serviceList></device></root>""")

    def do_POST(self):
        assert self.path == "/control", self.path
        body = self.rfile.read(int(self.headers["Content-Length"]))
        doc = ET.fromstring(body)
        action = list(list(doc)[0])[0]
        arguments = {node.tag.split("}")[-1]: node.text or "" for node in action}
        calls.append((action.tag.split("}")[-1], arguments))
        name = action.tag.split("}")[-1]
        if name in ("Browse", "Search"):
            object_id = arguments.get("ObjectID", arguments.get("ContainerID"))
            assert object_id in ("0", "large", "track:$1"), arguments
            selected = didl
            count = 2
            if arguments.get("BrowseFlag") == "BrowseMetadata":
                selected = '<DIDL-Lite><container id="0" parentID="-1" searchable="1"><title>Root</title></container></DIDL-Lite>'
                count = 1
                if object_id == "track:$1":
                    selected = didl[:didl.index('<container')] + didl[didl.index('<item'):]
            total = count
            if object_id == "large":
                offset = int(arguments["StartingIndex"])
                count = min(int(arguments["RequestedCount"]), max(0, 137 - offset))
                total = 137
                selected = '<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">' + ''.join(
                    f'<item id="opaque:{i}" parentID="large"><dc:title>Duplicate title</dc:title><upnp:class>object.item.audioItem.musicTrack</upnp:class><res protocolInfo="http-get:*:audio/mpeg:*">/music.mp3?track={i}</res></item>'
                    for i in range(offset, offset + count)) + '</DIDL-Lite>'
            content = f"<Result>{html.escape(selected)}</Result><NumberReturned>{count}</NumberReturned><TotalMatches>{total}</TotalMatches><UpdateID>7</UpdateID>"
        else:
            content = {
                "GetSearchCapabilities": "<SearchCaps>dc:title,upnp:artist</SearchCaps>",
                "GetSortCapabilities": "<SortCaps>dc:title</SortCaps>",
                "GetSystemUpdateID": "<Id>7</Id>",
            }[name]
        self.send_xml(f"""<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:{name}Response xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">{content}</u:{name}Response></s:Body></s:Envelope>""")


http = http.server.ThreadingHTTPServer((ip, 0), Handler)
http.daemon_threads = True
threading.Thread(target=http.serve_forever, daemon=True).start()
udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
udp.bind(("", 1900))
udp.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton("239.255.255.250") + socket.inet_aton(ip))
udp.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ip))
udp.settimeout(0.5)
running = True


def ssdp():
    next_alive = 0
    while running:
        if time.monotonic() >= next_alive:
            alive = f"NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nCACHE-CONTROL: max-age=30\r\nLOCATION: http://{ip}:{http.server_port}/description.xml\r\nNT: urn:schemas-upnp-org:device:MediaServer:1\r\nNTS: ssdp:alive\r\nUSN: {udn}::urn:schemas-upnp-org:device:MediaServer:1\r\nSERVER: fixture/1.0 UPnP/1.0 fixture/1.0\r\n\r\n"
            udp.sendto(alive.encode(), ('239.255.255.250',1900))
            next_alive = time.monotonic() + 5
        try:
            data, source = udp.recvfrom(8192)
        except socket.timeout:
            continue
        except OSError:
            break
        if data.startswith(b"M-SEARCH "):
            searches.append(source)
            response = f"HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=30\r\nEXT:\r\nLOCATION: http://{ip}:{http.server_port}/description.xml\r\nSERVER: fixture/1.0 UPnP/1.0 fixture/1.0\r\nST: urn:schemas-upnp-org:device:MediaServer:1\r\nUSN: {udn}::urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n"
            udp.sendto(response.encode(), source)


threading.Thread(target=ssdp, daemon=True).start()
try:
    result = subprocess.run([binary, "--seconds", "3", "--browse", udn], text=True, capture_output=True, timeout=20)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr, searches, calls)
    page = json.loads(result.stdout)
    assert len(page["items"]) == 2, page
    assert page["items"][0]["id"] == "server/opaque&id", page
    assert page["items"][0]["title"] == "Server category", page
    assert page["items"][1]["compatible_mp3"] is True, page
    assert calls and calls[0][0] == "Browse", calls
    print("network: independent SSDP discovery, HTTP description and SOAP Browse passed")
    if len(sys.argv) > 2:
        from production_path import exercise
        exercise(binary, sys.argv[2], udn, *(sys.argv[3:5]), expiry_location=f'http://{ip}:{http.server_port}/expiry.xml')
finally:
    running = False
    udp.close()
    http.shutdown()
    http.server_close()
