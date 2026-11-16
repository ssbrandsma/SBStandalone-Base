"""Optional host-only measurements; records no request arguments or credentials."""
import json
import os
from pathlib import Path
import time

def resources(process):
    values = dict(line.split(':', 1) for line in Path(f'/proc/{process.pid}/status').read_text().splitlines() if ':' in line)
    ticks = Path(f'/proc/{process.pid}/stat').read_text().split()
    return dict(rss_kib=int(values['VmRSS'].split()[0]), peak_rss_kib=int(values['VmHWM'].split()[0]),
                fds=len(list(Path(f'/proc/{process.pid}/fd').iterdir())), threads=int(values['Threads']),
                cpu_seconds=(int(ticks[13])+int(ticks[14]))/os.sysconf('SC_CLK_TCK'))

def emit(component, phase, process=None, **values):
    output = os.environ.get('SB_UPNP_METRICS_OUTPUT')
    if output:
        row = dict(component=component, phase=phase, monotonic_seconds=time.monotonic(), physical=False, **values)
        if process:
            row.update(resources(process))
        with open(output, 'a') as file:
            file.write(json.dumps(row)+'\n')
