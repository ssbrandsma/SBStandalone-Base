#!/usr/bin/env python3
"""Loopback-only reproducible sbbase measurements; never connects to devices."""
import argparse
import http.client
import json
import os
from pathlib import Path
import socket
import statistics
import subprocess
import tempfile
import time


def run(binary, seconds=2, count=100):
    with tempfile.TemporaryDirectory(prefix="sb-wp0-") as directory:
        config = Path(directory) / "catalog.json"
        config.write_text('{"applets":[]}')
        with open(os.devnull, "wb") as log:
            process = subprocess.Popen([str(Path(binary).resolve()), "--config", str(config)], stderr=log)
            try:
                for _ in range(100):
                    if process.poll() is not None:
                        raise RuntimeError("sbbase failed to start (ports in use?)")
                    try:
                        with socket.create_connection(("127.0.0.1", 9000), timeout=.2):
                            break
                    except OSError:
                        time.sleep(.05)
                else:
                    raise RuntimeError("startup timeout")
                proc = Path(f"/proc/{process.pid}")
                def ticks():
                    fields = (proc / "stat").read_text().split(") ", 1)[1].split()
                    return int(fields[11]) + int(fields[12])
                def snapshot():
                    return {"fds": len(list((proc / "fd").iterdir())),
                            "status": {k: v.strip() for k, v in (line.split(":", 1) for line in (proc / "status").read_text().splitlines()) if k in ("VmRSS", "VmHWM", "VmSize", "Threads")}}
                start = ticks(); wall = time.monotonic(); time.sleep(seconds)
                idle = (ticks() - start) / os.sysconf("SC_CLK_TCK") / (time.monotonic() - wall) * 100
                before = snapshot(); measurements = {}
                for name, method, path, body in [
                    ("health", "GET", "/health", None),
                    ("nowplaying", "GET", "/api/nowplaying", None),
                    ("comet_date", "POST", "/cometd", json.dumps([{"channel":"/slim/request","data":{"request":["",["date","?"]],"response":"/wp0/date"}}]))]:
                    values = []
                    for _ in range(count):
                        connection = http.client.HTTPConnection("127.0.0.1", 9000, timeout=3)
                        start = time.perf_counter_ns()
                        connection.request(method, path, body, {"Content-Type":"application/json"})
                        response = connection.getresponse(); data = response.read()
                        assert response.status == 200
                        json.loads(data); connection.close()
                        values.append((time.perf_counter_ns()-start)/1e6)
                    ordered = sorted(values)
                    measurements[name] = {"n": count, "p50_ms": statistics.median(values), "p95_ms": ordered[max(0, int(count*.95)-1)], "max_ms": max(values)}
                time.sleep(.2)
                after = snapshot()
                assert after["fds"] <= before["fds"], (before, after)
                return {"binary":str(Path(binary).resolve()), "host":os.uname()._asdict() if hasattr(os.uname(), "_asdict") else list(os.uname()), "idle_seconds":seconds, "idle_cpu_percent":idle, "before":before, "after":after, "latency":measurements}
            finally:
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired: process.kill(); process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("binary"); parser.add_argument("--seconds",type=float,default=2); parser.add_argument("--count",type=int,default=100); parser.add_argument("--output")
    args = parser.parse_args(); result = json.dumps(run(args.binary,args.seconds,args.count),indent=2)
    if args.output: Path(args.output).write_text(result+"\n")
    else: print(result)
