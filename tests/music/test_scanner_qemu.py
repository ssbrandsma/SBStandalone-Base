"""ARM coordinator, parser and exec helpers on host kernel under user QEMU.
Wrapper scripts bridge exec to qemu-arm, not to a physical target kernel.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import shlex

root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="sb-scanner-qemu-") as directory:
    for target in ("baby","fab4"):
        wrappers=[]
        for name in ("sbmusic-metadata-helper","music_metadata_fault"):
            wrapper=Path(directory)/f"{target}-{name}"
            wrapper.write_text('#!/bin/sh\nexec qemu-arm -0 "$0" '+shlex.quote(str(root/"build-arm"/target/"music"/name))+' "$@"\n')
            wrapper.chmod(0o755);wrappers.append(str(wrapper))
        env=dict(os.environ,SB_SCAN_RUNNER="qemu-arm",SB_SCAN_TEST_TIMEOUT_MS="3000")
        subprocess.run(["python3",str(root/"tests/music/test_scanner.py"),
                        str(root/"build-arm"/target/"music/test_music_scanner"),*wrappers],env=env,check=True,timeout=300)
        print(target,"ARM coordinator/parser/isolated helpers passed",flush=True)
