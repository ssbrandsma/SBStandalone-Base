import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import signal
import time
import shlex
import random
from wp2_fixtures import MEDIA, TITLE, mp3, compact_flac, populate, mpeg_probe

binary, helper, fault = map(lambda x: str(Path(x).resolve()), sys.argv[1:4])
runner=shlex.split(os.environ.get("SB_SCAN_RUNNER",""))


def run(*args):
    p = subprocess.run([*runner,binary, *map(str, args)], capture_output=True, text=True, timeout=120)
    assert p.returncode == 0, (args, p.stdout, p.stderr)
    return json.loads(p.stdout)


def rows(db):
    with sqlite3.connect(db) as connection:
        return connection.execute("select id,path,title,fingerprint from tracks order by path").fetchall()


def rejected(db):
    with sqlite3.connect(db) as connection:
        return connection.execute("select path,code,previous_track_id from rejected_files order by path").fetchall()


with tempfile.TemporaryDirectory(prefix="sb-scanner-", dir="/dev/shm") as media_dir, tempfile.TemporaryDirectory(prefix="sb-scanner-db-") as db_dir:
    root, dbroot = Path(media_dir), Path(db_dir)
    fixtures = populate(root)
    for version in ("v22", "v23", "v24", "unsync"):
        m = run("probe", root / f"{version}.mp3")
        assert m["status"] == 0 and m["title"] == TITLE and m["album_artist"] == "Various Artists", m
        assert (m["track"], m["track_total"], m["disc"], m["disc_total"], m["year"], m["compilation"]) == (2,12,1,2,2024,1), m
        assert m["genres"] == ["Rock"] and m["rate"] == 44100 and 150 < m["duration_ms"] < 400, m
    assert run("probe", root / "v24.mp3")["artists"] == ["Artist One", "Artist Two"]
    for vbri in (False,True):
        path=dbroot/f"duration-{vbri}.mp3";path.write_bytes(mpeg_probe(vbri))
        m=run("probe",path)
        assert m["status"] == 0 and m["duration_ms"] == 52 and m["bitrate"] == 128,m
        assert m["warnings"] == (0 if vbri else 1),m
    m = run("probe", root / "rich.flac")
    assert m["status"] == 0 and m["bit_depth"] == 16 and m["duration_ms"] == 200 and m["genres"] == ["Rock", "Jazz"], m
    contradictory=dbroot/"contradictory.flac"
    # Equal-length comment replacement preserves the FLAC block framing.
    contradictory.write_bytes(compact_flac(True).replace(b"ALBUM=Two Discs",b"DATE=1999-01-01"))
    m=run("probe",contradictory)
    assert m["status"]==0 and m["year"]==1999 and m["warnings"]>0,m
    for name in ("cover-mp3.mp3", "rich.flac", "comments.ogg"):
        m = run("probe", root/name)
        assert m["status"] == 0 and "image/png" in m["art"], m
        assert m["bytes"] <= 768*1024
    for name in ("truncated.mp3","oversized.mp3","large-tags.mp3","malformed.flac","malformed.ogg"):
        m = run("probe", MEDIA/name)
        assert m["status"] != 0, (name,m)
    m = run("probe", MEDIA/"invalid-utf8.mp3")
    assert m["status"] == 0 and "\ufffd" in m["title"], m
    rng=random.Random(2)
    for i in range(60):
        payload=bytearray((mp3(),compact_flac(),(MEDIA/"comments.ogg").read_bytes())[i%3])
        if i%4==0:payload=payload[:rng.randrange(1,len(payload))]
        else:
            for _ in range(4):payload[rng.randrange(len(payload))]=rng.randrange(256)
        malformed=dbroot/f"mutation.{('mp3','flac','ogg')[i%3]}";malformed.write_bytes(payload)
        m=run("probe",malformed)
        assert m["status"] in range(5) and m["bytes"]<=768*1024,m

    # Initial library, every supported format and exact retained metadata.
    db = dbroot / "library.db"
    result = run("scan",root,db,helper,1)
    assert result["state"] == 4 and result["added"] == len(fixtures) and result["errors"] == 0, result
    original = rows(db)
    with sqlite3.connect(db) as c:
        assert c.execute("select bit_depth,track_total,disc_total from tracks where path='rich.flac'").fetchone() == (16,12,2)
        assert c.execute("select release_date,total_samples from tracks where path='rich.flac'").fetchone() == ("2024-01-01",8820)
        assert c.execute("select disc,disc_total from tracks where path='disc2.flac'").fetchone() == (2,2)
        assert c.execute("select count(distinct album_id) from tracks where path in ('rich.flac','disc2.flac')").fetchone()[0] == 1
        assert c.execute("select count(*) from tracks").fetchone()[0] == len(fixtures)
    result = run("scan",root,db,helper,0)
    assert result["state"] == 4 and result["unchanged"] == len(fixtures) and result["parser_bytes"] == 0, result
    assert rows(db) == original
    result=run("readonly-media",root,db,helper,0)
    assert result["state"]==4 and result["unchanged"]==len(fixtures) and rows(db)==original,result
    result=run("generation2",root,db,helper,0)
    assert result["state"]==4 and rows(db)==original,result
    result=run("stale",root,db,helper,0)
    assert result["state"]==7 and result["removed"]==0 and rows(db)==original,result
    result=run("scan",root,db,helper,0,"twice")
    assert result["state"]==4 and result["unchanged"]==len(fixtures) and result["parser_bytes"]==0,result

    # Update and rename preserve committed IDs with supported inode evidence.
    changed_id = next(row[0] for row in original if row[1] == "v24.mp3")
    (root/"v24.mp3").write_bytes(mp3(4,title="Updated title"))
    result = run("scan",root,db,helper,0)
    assert result["state"] == 4 and result["updated"] == 1, result
    assert next(row[:3] for row in rows(db) if row[1] == "v24.mp3") == (changed_id,"v24.mp3","Updated title")
    (root/"v24.mp3").rename(root/"renamed.mp3")
    result = run("scan",root,db,helper,0)
    assert result["state"] == 4 and result["renamed"] == 1, result
    assert next(row[0] for row in rows(db) if row[1] == "renamed.mp3") == changed_id
    (root/"v23.mp3").unlink()
    result = run("scan",root,db,helper,0)
    assert result["state"] == 4 and result["removed"] == 1, result
    previous = rows(db)

    # A bounded per-file parser rejection is recoverable, never whole-scan
    # fatal: traversal completes, the rejection is classified and recorded
    # durably, no track is fabricated, and complete reconciliation still
    # retires the genuinely missing record.
    (root/"v22.mp3").unlink()
    (root/"broken.mp3").write_bytes(b"ID3\x04\0\0\x7f\x7f\x7f\x7f")
    result = run("scan",root,db,helper,0)
    assert result["state"] == 4 and result["removed"] == 1, result
    assert result["errors"] == 1 and result["recoverable"] == 1 and result["fatal"] == 0, result
    assert result["limited"] == 1, result
    classes = result["error_class"]
    assert len(classes) == 1 and classes[0]["path"] == "broken.mp3", result
    assert classes[0]["recoverable"] == 1 and classes[0]["code"] == 3, result
    current = rows(db)
    assert not any(row[1] == "broken.mp3" for row in current)
    assert not any(row[1] == "v22.mp3" for row in current)
    assert len(current) == len(previous) - 1
    filings = rejected(db)
    assert len(filings) == 1 and filings[0][0] == "broken.mp3" and filings[0][1] == 3, filings
    assert filings[0][2] == 0, filings
    # An unchanged rescan skips the known-bad file without reparsing it.
    again = run("scan",root,db,helper,0)
    assert again["state"] == 4 and again["metadata_parses"] == 0, again
    assert again["recoverable"] == 1 and again["fatal"] == 0, again
    assert rejected(db) == filings
    (root/"broken.mp3").unlink()
    (root/"v22.mp3").write_bytes(fixtures["v22.mp3"])
    healed = run("scan",root,db,helper,0)
    assert healed["state"] == 4 and rejected(db) == [], healed
    # Permission/depth failures are incomplete, even when a privileged host
    # could bypass mode bits. Exact committed IDs remain present.
    denied=root/"denied";denied.mkdir();(denied/"no.mp3").write_bytes(mp3());denied.chmod(0)
    before={row[0] for row in rows(db)}
    result=run("scan",root,db,helper,0)
    assert result["state"]==6 and result["errors"]>0 and before.issubset({row[0] for row in rows(db)}),result
    denied.chmod(0o700);shutil.rmtree(denied)
    deep=root/"deep";deep.mkdir();leaf=deep
    for _ in range(18): leaf=leaf/"d";leaf.mkdir()
    (leaf/"too-deep.mp3").write_bytes(mp3())
    result=run("scan",root,db,helper,0)
    assert result["state"]==6 and any("depth" in e for e in result["error_detail"]),result
    shutil.rmtree(deep)
    hard=root/"hard";hard.mkdir();(hard/"a.mp3").write_bytes(mp3());os.link(hard/"a.mp3",hard/"b.mp3")
    result=run("scan",root,db,helper,0)
    assert result["state"]==4 and sum(row[1].startswith("hard/") for row in rows(db))==1,result

    # Ignore escapes/internal directories/non-audio, preserve Unicode and long names.
    os.symlink("/etc",root/"escape")
    os.symlink(".",root/"loop")
    (root/".StandaloneBase").mkdir()
    (root/".StandaloneBase"/"ignored.mp3").write_bytes(mp3())
    (root/"image.txt").write_text("not audio")
    long_name="é"*110+".mp3"
    (root/long_name).write_bytes(mp3())
    (root/"wrong.flac").write_bytes(b"not flac but has an extension")
    result=run("scan",root,db,helper,0)
    assert result["state"] == 4 and result["errors"] == 1, result
    assert result["recoverable"] == 1 and result["fatal"] == 0 and result["unsupported"] == 1, result
    assert len(result["error_class"]) == 1 and result["error_class"][0]["path"] == "wrong.flac", result
    assert result["error_class"][0]["recoverable"] == 1 and result["error_class"][0]["code"] == 1, result
    assert any(row[1] == long_name for row in rows(db))
    assert not any(row[1] == "wrong.flac" for row in rows(db))
    assert not any("ignored" in row[1] for row in rows(db))
    assert rejected(db) == [("wrong.flac",1,0)], rejected(db)
    (root/"wrong.flac").unlink()

    # Sidecar discovery/revision, no image decoding or cache writes.
    (root/"cover.jpg").write_bytes((MEDIA/"generated.jpg").read_bytes())
    assert run("scan",root,db,helper,0)["state"] == 4
    with sqlite3.connect(db) as c:
        ref=c.execute("select w.reference from tracks t join artwork w on w.id=t.artwork_id where t.path='untagged.mp3'").fetchone()[0]
        assert ref.startswith("sidecar:9:cover.jpg:image/jpeg:") and "@v1:" in ref,ref

    # Fault isolation/timeouts/mutation are controlled helper executables.
    fault_root=root/"faults";fault_root.mkdir();(fault_root/"one.mp3").write_bytes(mp3())
    for kind in ("crash","hang","mutate","after"):
        link=dbroot/kind;os.symlink(fault,link)
        result=run("scan",fault_root,dbroot/f"{kind}.db",link,1)
        assert result["state"] == 6 and result["errors"] == 1 and not rows(dbroot/f"{kind}.db"), result
    (fault_root/"one.mp3").write_bytes(mp3())
    # Existing library used read-only: explicit refusal; unchanged file bytes.
    before=hashlib.sha256(db.read_bytes()).hexdigest()
    result=run("readonly-db",root,db,helper,0)
    assert result["state"] == 6 and hashlib.sha256(db.read_bytes()).hexdigest() == before,result
    # Cancel/remove kill a pending slow helper; no partial deletion.
    lifecycle={}
    for action in ("cancel","remove","stop"):
        result=run("scan",fault_root,dbroot/f"{action}.db",dbroot/"hang",1,action)
        assert result["state"] == (7 if action=="remove" else 5) and result["removed"] == 0 and result["elapsed_ms"] < 1000,result
        lifecycle[action]=result
    # Persisted root scope prevents selecting a different directory and then
    # interpreting the old source's entire catalogue as missing.
    before=rows(db)
    result=run("scan",fault_root,db,helper,0)
    assert result["state"]==6 and rows(db)==before,result
    unbound=dbroot/"unbound.db";shutil.copyfile(db,unbound)
    with sqlite3.connect(unbound) as c:c.execute("update media set scan_root=NULL")
    result=run("scan",root,unbound,helper,0)
    assert result["state"]==6 and rows(unbound)==before,result
    # Database write failure never turns existing content into an empty library.
    result=run("db-fail",root,db,helper,0)
    assert result["state"]==6 and result["removed"]==0 and rows(db)==before,result
    # Kill the entire disposable scanner/helper process group mid-scan, then
    # reopen: previously committed identities survive, with no orphan helper.
    process=subprocess.Popen([*runner,binary,"scan",str(root),str(db),str(dbroot/"hang"),"1"],stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
    time.sleep(0.05)
    assert process.poll() is None
    os.killpg(process.pid,signal.SIGKILL);process.communicate(timeout=5)
    assert rows(db)==before
    result=run("scan",root,db,helper,0)
    assert result["state"]==4,result
    if os.environ.get("SB_SCAN_TEST_METRICS"):
        Path(os.environ["SB_SCAN_TEST_METRICS"]).write_text(json.dumps(lifecycle,indent=2)+"\n")
    print("WP2 metadata, exact library contents, incremental identity, deletion safety, isolation, cancellation/removal and policy tests passed")
