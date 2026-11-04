"""Independent source-derived server frame goldens, not live audio traffic."""
import json
from pathlib import Path
import struct
import subprocess
import sys

def frame(op, payload):
    return struct.pack('!H', len(payload)+4)+op.encode()+payload

def strm(command, argument=0, start=False):
    # Slimproto.lua strm unpack; Squeezebox.pm aaaaaaaCCCaCCCNnN.
    p = bytearray(command.encode()+b'0m????'+bytes(3)+b'0'+bytes(13))
    assert len(p) == 24
    p[14:18] = struct.pack('!I', argument)
    if start:
        p[1] = ord('1')
        p[7], p[12] = 64, 20
        p[18:24] = struct.pack('!H4B', 9000, 127, 0, 0, 1)
        p.extend(b'GET /music/stream/'+b'a'*64+b' HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n')
    return frame('strm', p)

gold = {'start': strm('s', start=True)}
for command in 'puqft':
    gold[command] = strm(command, 0x12345678 if command in 'put' else 0)
gold['audg'] = frame('audg', struct.pack('!IIBBIII', 128, 64, 0, 0, 65536, 32768, 0x12345678))
gold['aude'] = frame('aude', b'\x01\x01')
gold['cont'] = frame('cont', bytes(7))
lines = subprocess.check_output([sys.argv[1], '--wire'], text=True).splitlines()
actual = {name: bytes.fromhex(raw) for name, raw in (line.split() for line in lines)}
assert actual == gold, [(k, actual.get(k), v) for k, v in gold.items() if actual.get(k) != v]
fixture = json.loads((Path(__file__).parent/'fixtures/transport.json').read_text())
assert fixture['kind'] == 'source-derived, no device capture'
assert {k: v.hex() for k, v in gold.items()} == fixture['outgoing']
old = json.loads((Path(__file__).parent/'fixtures/protocol.json').read_text())
for item in old['slimproto']:
    if item['opcode']=='strm':continue
    raw=bytes.fromhex(item['hex'])
    assert raw[:4].decode()==item['opcode'] and int.from_bytes(raw[4:8],'big')==len(raw)-8
    assert subprocess.check_output([sys.argv[1],'--decode',item['opcode'],raw[8:].hex()],text=True).strip() in ('1','2','3','4')
print('Independent SlimProto serializer goldens passed')
