"""Static ARM user-mode tests on host mounts; never physical-media coverage."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="sb-storage-qemu-") as temp:
    for target in ("baby", "fab4"):
        subprocess.run(["qemu-arm", str(root / "build-arm" / target / "music/test_music_storage"),
                        "correct", str(root / "tests/music/fixtures"), str(Path(temp) / f"{target}.db")],
                       check=True, timeout=120)
        print(target, "WP6a ARM/QEMU passed", flush=True)
