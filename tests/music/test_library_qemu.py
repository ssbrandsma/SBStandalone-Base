#!/usr/bin/env python3
"""ARM user-mode execution only; no target kernel or physical media coverage."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="sb-library-qemu-") as directory:
    for target in ("baby", "fab4"):
        binary = root / "build-arm" / target / "music/test_music_library"
        for wal in (0, 1):
            db = Path(directory) / f"{target}-{wal}.db"
            subprocess.run(["qemu-arm", str(binary), "correct", str(db), "1000", str(wal)],
                           check=True, timeout=300)
        print(target, "ARM library tests passed under QEMU", flush=True)
