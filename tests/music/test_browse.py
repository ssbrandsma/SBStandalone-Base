"""Native fixture/broker lifecycle and real WP2 scanner concurrency on tmpfs."""
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from wp2_fixtures import compact_flac

binary=str(Path(sys.argv[1]).resolve())
helper=str(Path(sys.argv[2]).resolve())
with tempfile.TemporaryDirectory(prefix='sb-music-browse-') as temporary:
    root=Path(temporary)
    subprocess.run([binary,'native',str(root/'native.db'),'1000'],check=True,timeout=60)
    with tempfile.TemporaryDirectory(prefix='wp3b-scan-',dir='/dev/shm') as media:
        base=Path(media);data=compact_flac()
        for i in range(1000):
            parent=base/f'folder{i//100}'
            parent.mkdir(exist_ok=True)
            (parent/f'track{i}.flac').write_bytes(data)
        subprocess.run([binary,'scan',str(root/'scan.db'),media,helper],check=True,timeout=150)
        connection=sqlite3.connect(root/'scan.db')
        assert connection.execute('SELECT count(*) FROM tracks').fetchone()[0]==1000
        connection.close()
print('Native browser and actual concurrent scanner passed')
