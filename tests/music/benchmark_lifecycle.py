"""Host-only arbitration observations; these are not ARM throughput claims."""
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time
from test_tinysc_lifecycle import Lifecycle, CONTROL


def process_metrics(pid):
    values = {}
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        key, _, value = line.partition(":")
        if key in {"VmRSS", "VmHWM", "VmSize", "Threads"}:
            values[key] = value.strip()
    text = Path(f"/proc/{pid}/stat").read_text().split(") ", 1)[1].split()
    values["cpu_seconds"] = (int(text[11]) + int(text[12])) / os.sysconf("SC_CLK_TCK")
    values["descriptors"] = len(list(Path(f"/proc/{pid}/fd").iterdir()))
    return values


def run(active):
    fixture = Lifecycle()
    fixture.setUp()
    try:
        fixture.enable()
        if active:
            for role in ("slimserver.pl", "scanner.pl", "gdresized.pl"):
                process = fixture.start(role)
                name = {"slimserver.pl": "squeezecenter", "scanner.pl": "scanner", "gdresized.pl": "gdresized"}[role]
                (fixture.root / "pid" / (name + ".pid")).write_text(str(process.pid))
        begin = time.monotonic()
        worker = fixture.worker()
        fixture.state(worker, "STOPPED_VERIFIED")
        elapsed = time.monotonic() - begin
        before = process_metrics(worker.pid)
        time.sleep(3)
        after = process_metrics(worker.pid)
        stop = time.monotonic()
        worker.stdin.write(b"q\n")
        worker.stdin.flush()
        worker.wait(timeout=3)
        return dict(synthetic_tinysc_active=active, verified_seconds=elapsed,
                    idle_observation_seconds=3, idle_cpu_seconds=after["cpu_seconds"] - before["cpu_seconds"],
                    metrics=after, shutdown_seconds=time.monotonic() - stop)
    finally:
        fixture.tearDown()


if __name__ == "__main__":
    timings = []
    for _ in range(20):
        begin = time.monotonic()
        subprocess.run([CONTROL, "detect"], check=True, stdout=subprocess.DEVNULL)
        timings.append(time.monotonic() - begin)
    print(json.dumps(dict(environment="Docker Linux x86_64 host; synthetic fixtures; no hardware",
                          detect_median_seconds=statistics.median(timings), detect_max_seconds=max(timings),
                          runs=[run(False), run(True)]), indent=2))
