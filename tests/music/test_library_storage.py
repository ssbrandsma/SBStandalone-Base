#!/usr/bin/env python3
"""Deterministic disposable database tests; no hardware, LMS or network."""
import hashlib
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())


def run(*args):
    result = subprocess.run([binary, *map(str, args)], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, timeout=180)
    assert result.returncode == 0, (args, result.stdout, result.stderr)
    return result.stdout.strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory(prefix="sb-library-wp1-") as directory:
    root = Path(directory)
    for wal in (0, 1):
        path = root / f"library-{wal}.db"
        print(run("correct", path, 1000, wal))
    path = root / "library-0.db"
    with sqlite3.connect(path) as db:
        expected = db.execute("select id,path,title from tracks order by id").fetchall()
    # Interrupted writer spills >512 KiB of dirty data, then exits without close.
    run("crash", path)
    journal=Path(str(path)+"-journal")
    assert journal.exists() and journal.read_bytes()[:8] == bytes.fromhex("d9d505f920a163d7"), "expected a real hot rollback journal"
    assert run("count", path) == "1000"
    with sqlite3.connect(path) as db:
        assert db.execute("pragma integrity_check").fetchone()[0] == "ok"
        assert db.execute("select id,path,title from tracks order by id").fetchall() == expected
    print(run("limit", path))
    print(run("iofault", path))
    assert run("count", path) == "1000"
    with sqlite3.connect(path) as db:
        assert db.execute("select id,path,title from tracks order by id").fetchall() == expected
    before = digest(path)
    assert run("probe", path, 1) == "ok"
    assert digest(path) == before
    # Newer and foreign schemas are never rewritten or deleted.
    newer = root / "newer.db"; shutil.copyfile(path, newer)
    with sqlite3.connect(newer) as db: db.execute("pragma user_version=999")
    before = digest(newer)
    assert run("probe", newer) == "new_schema" and digest(newer) == before
    foreign = root / "foreign.db"
    with sqlite3.connect(foreign) as db: db.execute("create table user_data(value)")
    before = digest(foreign)
    assert run("probe", foreign) == "invalid" and digest(foreign) == before
    corrupt = root / "corrupt.db"; shutil.copyfile(path, corrupt)
    data = bytearray(corrupt.read_bytes()); data[:16] = b"NOT SQLITE DATA!"; corrupt.write_bytes(data)
    before = digest(corrupt)
    assert run("probe", corrupt) == "corrupt" and digest(corrupt) == before
    # A v1 fixture has the same schema minus the two v2 scanner fields.
    legacy = root / "legacy.db"; shutil.copyfile(path, legacy)
    with sqlite3.connect(legacy) as db:
        db.execute("drop index track_fingerprint")
        for column in ("bit_depth", "track_total", "disc_total", "release_date", "total_samples"):
            db.execute(f"alter table tracks drop column {column}")
        db.execute("alter table library_meta drop column scan_counter")
        db.execute("alter table media drop column scan_root")
        db.execute("alter table tracks drop column fingerprint")
        db.execute("alter table tracks drop column scan_epoch")
        db.execute("pragma user_version=1")
    before = digest(legacy)
    assert run("probe", legacy, 1) == "migration_required" and digest(legacy) == before
    assert run("probe", legacy) == "ok"
    with sqlite3.connect(legacy) as db:
        assert db.execute("pragma user_version").fetchone()[0] == 3
        assert db.execute("select id,path,title from tracks order by id").fetchall() == expected
        assert db.execute("select count(*) from tracks where fingerprint='' and scan_epoch=0").fetchone()[0] == len(expected)
    # Missing parent/source does not create directories or replacement data.
    legacy2 = root / "legacy2.db"; shutil.copyfile(path,legacy2)
    with sqlite3.connect(legacy2) as db:
        db.execute("drop index track_fingerprint")
        for column in ("bit_depth","track_total","disc_total","release_date","total_samples"):
            db.execute(f"alter table tracks drop column {column}")
        db.execute("alter table library_meta drop column scan_counter")
        db.execute("alter table media drop column scan_root")
        db.execute("pragma user_version=2")
    before=digest(legacy2)
    assert run("probe",legacy2,1)=="migration_required" and digest(legacy2)==before
    assert run("probe",legacy2)=="ok"
    with sqlite3.connect(legacy2) as db:
        assert db.execute("pragma user_version").fetchone()[0]==3
        assert db.execute("select id,path,title from tracks order by id").fetchall()==expected
        assert db.execute("select count(*) from tracks where bit_depth=0 and track_total=0 and disc_total=0 and release_date=''").fetchone()[0]==len(expected)
    missing = root / "removed" / "library.db"
    assert run("probe", missing) == "io" and not missing.parent.exists()
    # Repeated worker start/close is exercised in C alongside per-operation FDs.
    print("migration/newer-schema/corrupt-copy/hot-journal/I/O/removal/read-only checks passed")
