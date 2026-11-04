#!/usr/bin/env python3
"""Synthetic loopback regression: no device, playback, database or scanner."""
import http.client
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def envelope(args, rid=1):
    return {"id":rid,"channel":"/slim/request","clientId":"wp0","data":{"request":["",args],"response":"/wp0/reply"}}


with tempfile.TemporaryDirectory(prefix="wp0-dispatch-") as directory:
    config=Path(directory)/"config.json";config.write_text('{"applets":[]}')
    process=subprocess.Popen([sys.argv[1],"--config",str(config)],stderr=subprocess.DEVNULL)
    try:
        for _ in range(80):
            assert process.poll() is None
            try:
                with socket.create_connection(("127.0.0.1",9000),timeout=.2):break
            except OSError:time.sleep(.05)
        else:raise AssertionError("startup timeout")
        def request(body):
            c=http.client.HTTPConnection("127.0.0.1",9000,timeout=3)
            c.request("POST","/cometd",body,{"Content-Type":"application/json"})
            r=c.getresponse();status=r.status;data=json.loads(r.read());c.close();return status,data
        def command(args):
            status,data=request(json.dumps([envelope(args)]));assert status==200
            return data
        assert command(["date","?"])[0]["successful"]
        for name in ("browselibrary","albums","artists","titles","genres","musicfolder","playlistcontrol"):
            data=command([name,"menu:1"]);assert data[0]["successful"] is False and data[1]["data"]["error"]=="unsupported_command",data
        data=command(["unknown","play","stop","status","menu"])
        assert data[0]["error"]=="unknown_command"
        for args in (["date",{}],["date",":bad"],["date","target:a","target:b"],["date","x"*512],["date"]+["?"]*24):
            data=command(args);assert data[0]["successful"] is False and data[0]["error"]=="malformed_command",data
        for body in ('[{"channel":"/slim/request",}]','[{"data":1 "channel":"/slim/request"}]','{}\x00','[{"channel":"x","channel":"y"}]'):
            status,data=request(body);assert status==400 and data[0]["error"]=="malformed_json",(status,data)
        status,data=request(json.dumps([envelope(["date","?"],1),envelope(["unknown","stop"],2),envelope(["jiveapplets",0,50,"target:fab4"],3)]))
        assert status==200 and len(data)==6 and data[0]["successful"] and not data[2]["successful"] and data[4]["successful"],data
        # Arguments containing supported names do not select their handlers.
        data=command(["date","play","stop","status","menu"])
        assert "date_epoch" in data[1]["data"] and "item_loop" not in data[1]["data"]
        # Feature off: no music route, no music menu advertised.
        menu=command(["menu",0,100])[1]["data"]
        assert "myMusic" not in json.dumps(menu)
        c=http.client.HTTPConnection("127.0.0.1",9000,timeout=3);c.request("GET","/music/stream/test");r=c.getresponse();assert r.status==404;r.read();c.close()
        # A new fixture is explicitly reconstructed rather than an observed capture.
        fixtures=json.loads((Path(__file__).parent/"fixtures/protocol.json").read_text())
        assert all(f["classification"] in ("source-derived expected behavior","synthetic boundary/error case") for f in fixtures["jive"])
        for f in fixtures["jive"]:
            assert isinstance(f["request"],list) and f["sources"]
            assert "count" in f["response"] or "actions" in f["response"] or "playlist_tracks" in f["response"]
        print("typed HTTP dispatch, batch isolation and disabled routes passed")
    finally:
        process.terminate()
        try:process.wait(timeout=3)
        except subprocess.TimeoutExpired:process.kill();process.wait()
