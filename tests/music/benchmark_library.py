#!/usr/bin/env python3
"""Fresh deterministic imports and bounded queries at 1k/10k/50k tracks."""
import argparse
import json
from pathlib import Path
import platform
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("binary", type=Path)
p.add_argument("--output", required=True, type=Path)
p.add_argument("--sizes", default="1000,10000,50000")
a = p.parse_args()
result = {"host": platform.platform(), "cache_note": "first connection queries after reopen/quick_check; OS page cache uncontrolled; warm mean of 20",
          "query_order": ["artists", "albums", "track_page", "search"], "runs": []}
with tempfile.TemporaryDirectory(prefix="sb-library-benchmark-") as directory:
    for size in map(int, a.sizes.split(",")):
        completed = subprocess.run([str(a.binary.resolve()), "bench", str(Path(directory)/f"{size}.db"), str(size)],
                                   check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=900)
        assert completed.returncode == 0, (size,completed.stdout,completed.stderr)
        result["runs"].append(json.loads(completed.stdout))
        a.output.write_text(json.dumps(result, indent=2)+"\n")
        print("completed", size, flush=True)
a.output.write_text(json.dumps(result, indent=2)+"\n")
