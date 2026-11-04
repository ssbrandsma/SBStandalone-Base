"""Controlled real-format fixture scans. No target hardware or network."""
import argparse
import json
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import hashlib
from wp2_fixtures import compact_flac

parser=argparse.ArgumentParser()
parser.add_argument("binary");parser.add_argument("helper")
parser.add_argument("--sizes",nargs="+",type=int,default=[1000,10000,50000])
parser.add_argument("--output",required=True)
args=parser.parse_args()
binary,helper=map(lambda p:str(Path(p).resolve()),(args.binary,args.helper))
data=compact_flac()
builds={p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in (binary,helper)}
results=[]
for n in args.sizes:
    with tempfile.TemporaryDirectory(prefix="sb-scan-bench-",dir="/dev/shm") as directory,tempfile.TemporaryDirectory(prefix="sb-scan-bench-db-") as database:
        root=Path(directory);db=Path(database)/"library.db"
        for i in range(n):
            folder=root/f"album{i//100:05}"
            if i%100==0:folder.mkdir()
            (folder/f"track{i:06}.flac").write_bytes(data)
        def run(initial,queries=False):
            argv=[binary,"scan",str(root),str(db),helper,str(initial)]
            if queries:argv.append("queries")
            p=subprocess.run(argv,capture_output=True,text=True,timeout=1200)
            assert p.returncode==0,(p.stdout,p.stderr)
            result=json.loads(p.stdout);assert result["state"]==4,result
            with sqlite3.connect(db) as connection:assert connection.execute("select count(*) from tracks").fetchone()[0]==n
            return result
        first=run(1,True)
        assert first["added"]==n and first["queries"]>0,first
        unchanged=run(0)
        assert unchanged["unchanged"]==n and unchanged["parser_bytes"]==0,unchanged
        changed=root/"album00000/track000000.flac"
        payload=bytearray(data)
        # Change an ASCII title within the Vorbis Comment, preserving valid bytes.
        at=payload.find(b"WP0 ");assert at>=0;payload[at:at+3]=b"WP2";changed.write_bytes(payload)
        update=run(0)
        assert update["updated"]==1 and update["unchanged"]==n-1,update
        item={"files":n,"fixture_bytes":len(data),"database_bytes":db.stat().st_size,
              "build_sha256":builds,"fixture_sha256":hashlib.sha256(data).hexdigest(),
              "initial":first,"unchanged":unchanged,"single_update":update,
              "files_per_second":n*1000/max(first["elapsed_ms"],1),
              "cpu_ms_per_file":(first["cpu_ms"]+first["child_cpu_ms"])/n}
        results.append(item)
        assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==digest for p,digest in builds.items()),"benchmark executable changed during run"
        Path(args.output).write_text(json.dumps(results,indent=2)+"\n")
        print(json.dumps({"files":n,"initial_ms":first["elapsed_ms"],"unchanged_ms":unchanged["elapsed_ms"],"single_update_full_reconcile_ms":update["elapsed_ms"],"db_bytes":item["database_bytes"],"query_p95_ms":first["query_p95_ms"]}),flush=True)
