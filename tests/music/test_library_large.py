#!/usr/bin/env python3
import subprocess
from pathlib import Path
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="sb-library-large-") as directory:
    for count in (10000, 50000):
        subprocess.run([str(Path(sys.argv[1]).resolve()), "correct", str(Path(directory)/f"{count}.db"), str(count)],
                       check=True, timeout=900)
