#!/usr/bin/env python3
"""Source-derived wire fixtures against the existing framing code, not a player."""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
fixtures=json.loads((Path(__file__).parent/"fixtures/protocol.json").read_text())
with tempfile.TemporaryDirectory(prefix="wp0-wire-") as directory:
    lib=Path(directory)/"protocol.so"
    subprocess.run(["cc","-shared","-fPIC","-I"+str(root/"native/sbbase/include"),str(root/"native/sbbase/src/protocol.c"),"-o",str(lib)],check=True)
    library=C.CDLL(str(lib))
    parse=library.sb_parse_client_frame
    parse.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.POINTER(C.c_void_p),C.POINTER(C.c_uint32),C.POINTER(C.c_size_t)]
    def decode(data):
        incoming=C.create_string_buffer(data);op=C.create_string_buffer(5);payload=C.c_void_p();length=C.c_uint32();used=C.c_size_t()
        result=parse(incoming,len(data),op,C.byref(payload),C.byref(length),C.byref(used))
        return result,op.value.decode(),C.string_at(payload,length.value) if result==1 else b"",used.value
    batch=b""
    for f in fixtures["slimproto"]:
        data=bytes.fromhex(f["hex"])
        if f["opcode"]=="strm":
            assert struct.unpack("!H",data[:2])[0]==len(data)-2
            assert data[2:6]==b"strm" and data[6:9]==b"s1m"
            assert struct.unpack("!H",data[24:26])[0]==80 and data[30:].startswith(b"GET ")
            server=library.sb_server_frame;server.argtypes=[C.c_char_p,C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t];server.restype=C.c_size_t
            output=C.create_string_buffer(len(data));payload=data[6:]
            assert server(b"strm",payload,len(payload),output,len(output))==len(data)
            assert output.raw==data
            assert server(b"strm",payload,len(payload),output,len(data)-1)==0
            continue
        result,op,payload,used=decode(data)
        assert result==1 and op==f["opcode"] and len(payload)==f["payload_bytes"] and used==len(data)
        for n in range(len(data)):assert decode(data[:n])[0]==0
        batch+=data
    while batch:
        result,op,payload,used=decode(batch);assert result==1 and used;batch=batch[used:]
    assert decode(b"HELO"+struct.pack("!I",65537))[0]==-1
    # The framing helper intentionally accepts short HELO/STAT payloads.
    # Their semantic minimum is a fixture specification for WP3a, not activated here.
    for op in (b"HELO",b"STAT",b"RESP",b"DSCO"):
        assert decode(op+struct.pack("!I",0))[0]==1
for f in fixtures["jive"]:
    response=f["response"]
    if "base" in response:
        item=response["item_loop"][0]
        for name,action in response["base"]["actions"].items():
            params=dict(action["params"]);params.update(item[action["itemsParams"]])
            assert params and action["cmd"]
            if name=="play":assert action["nextWindow"]=="nowPlaying" and params["cmd"]=="load"
            if name=="add":assert "nextWindow" not in action and params["cmd"]=="add"
print("source-derived SlimProto framing and Jive action fixture shapes passed")
