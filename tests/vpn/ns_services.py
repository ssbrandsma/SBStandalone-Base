#!/usr/bin/env python3
"""Services reachable over either physical or VPN interfaces inside test namespaces."""
import http.server,json,socket,struct,threading,sys
from pathlib import Path
root=Path(sys.argv[1])
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_GET(self):
  body=json.dumps({'peer':self.client_address[0]}).encode();self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
def dns():
 s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('9.9.9.9',53))
 while True:
  data,peer=s.recvfrom(2048)
  if len(data)<12:continue
  with (root/'dns-peers').open('a') as f:f.write(peer[0]+'\n')
  response=data[:2]+b'\x81\x80'+data[4:6]+b'\x00\x01\x00\x00\x00\x00'+data[12:]+b'\xc0\x0c\x00\x01\x00\x01'+struct.pack('!I',30)+b'\x00\x04'+socket.inet_aton('203.0.113.8');s.sendto(response,peer)
class IPv6Server(http.server.ThreadingHTTPServer):address_family=socket.AF_INET6
threading.Thread(target=IPv6Server(('::',19091),Handler).serve_forever,daemon=True).start()
def v6_dns():
 s=socket.socket(socket.AF_INET6,socket.SOCK_STREAM);s.bind(('::',53));s.listen(8)
 while True:
  c,_=s.accept();c.close()
threading.Thread(target=v6_dns,daemon=True).start()
threading.Thread(target=dns,daemon=True).start()
http.server.ThreadingHTTPServer(('0.0.0.0',19090),Handler).serve_forever()
