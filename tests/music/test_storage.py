import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="sb-wp6a-") as temp:
    subprocess.run([sys.argv[1], "correct", str(pathlib.Path(__file__).parent / "fixtures"),
                    str(pathlib.Path(temp) / "library.db")], check=True)
