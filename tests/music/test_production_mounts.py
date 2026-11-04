"""Optional privileged host test: run ONLY in a private mount namespace.

Usage: sudo unshare -m python3 tests/music/test_production_mounts.py BASE HELPER
No device, decoder, fixture bridge or ownership ACK is involved.
"""
from pathlib import Path
import os
from contextlib import closing
import sqlite3
import subprocess
import sys
from wp2_fixtures import mp3, compact_flac

assert os.geteuid() == 0
assert os.readlink('/proc/self/ns/mnt') != os.readlink('/proc/1/ns/mnt'), 'private mount namespace required'
base, helper = map(lambda x: Path(x).resolve(), sys.argv[1:3])
ram = Path('/dev/standalone-music-test')
assert not ram.exists(), 'never cover an existing directory'
subprocess.run(['mount', '--make-rprivate', '/'], check=True)
ram.mkdir(mode=0o700)
ram_mounted = media_mounted = False
try:
    subprocess.run(['mount', '-t', 'tmpfs', '-o', 'size=8m,mode=0700', 'tmpfs', str(ram)], check=True)
    ram_mounted = True
    subprocess.run(['mount', '-t', 'tmpfs', '-o', 'size=8m,mode=0700', 'tmpfs', '/media'], check=True)
    media_mounted = True
    mount = Path('/media/sb-production-fixture')
    mount.mkdir()
    subprocess.run(['mount', '-t', 'tmpfs', '-o', 'size=8m,mode=0700', 'tmpfs', str(mount)], check=True)
    try:
        album = mount / 'album'
        album.mkdir()
        (album / 'track.mp3').write_bytes(mp3())
        (album / 'track.flac').write_bytes(compact_flac(True))
        parser = ram / 'metadata-helper'
        parser.write_bytes(helper.read_bytes())
        parser.chmod(0o500)
        result = subprocess.run([str(base), '--ownership-token', str(ram / 'token'),
            '--music-root', str(album), '--music-database', str(ram / 'library.db'),
            '--music-helper', str(parser), '--music-http-port', '80', '--music-scan', '--music-scan-only'],
            capture_output=True, text=True, timeout=30)
        print(result.stderr, end='')
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert not (ram / 'token').exists()
        with closing(sqlite3.connect(f'file:{ram}/library.db?mode=ro', uri=True)) as db:
            rows = db.execute('select id,path,title from tracks order by path').fetchall()
            assert len(rows) == 2 and len({r[0] for r in rows}) == 2, rows
            assert {Path(r[1]).suffix for r in rows} == {'.mp3', '.flac'}, rows
            assert db.execute('pragma integrity_check').fetchone() == ('ok',)
            print('Production bounded scan-only path: two indexed tracks, distinct IDs, SQLite integrity OK, capability removed')
    finally:
        subprocess.run(['umount', str(mount)], check=True)
finally:
    if media_mounted:
        subprocess.run(['umount', '/media'], check=True)
    if ram_mounted:
        subprocess.run(['umount', str(ram)], check=True)
    ram.rmdir()
