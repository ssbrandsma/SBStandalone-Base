#!/usr/bin/env python3
"""Reproducible isolated parser measurements. Host/QEMU figures are not device data."""
import argparse
import json
from pathlib import Path
import subprocess
import time


def run(directory, platform, iterations):
    root=Path(__file__).resolve().parents[2]
    media=root/"tests/music/fixtures/media"
    manifest=json.loads((media/"manifest.json").read_text())
    records=[]
    for parser in ("hls","taglib"):
        binary=directory/f"parser-{parser}-{platform}"
        for filename in manifest["files"]:
            command=(["qemu-arm"] if platform!="host" else [])+[str(binary.resolve()),str((media/filename).resolve()),str(iterations)]
            start=time.perf_counter()
            try:
                result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
                try: data=json.loads(result.stdout)
                except (ValueError,UnicodeDecodeError):data={"invalid_json_output":True,"stdout_hex":result.stdout[:128].hex()}
                if "title" in data: data["title"]=data["title"][:256]
                data.update(parser=parser,platform=platform,file=filename,exit_code=result.returncode,binary_bytes=binary.stat().st_size,wall_ms=(time.perf_counter()-start)*1000)
                if parser=="taglib" and filename in ("id3v22.mp3","id3v23.mp3","id3v24.mp3","comments.flac","comments.ogg"):
                    data["expected_metadata_matches"]=data.get("title")==manifest["title"] and data.get("artist")=="WP0 Artist" and data.get("album")=="WP0 Album"
                    data["expected_audio_matches"]=data.get("sample_rate")==44100 and data.get("channels")==2 and 150<=data.get("duration_ms",0)<=300
                    if filename!="id3v22.mp3":data["expected_art_matches"]=data.get("art_bytes")==69
                records.append(data)
            except subprocess.TimeoutExpired:
                records.append(dict(parser=parser,platform=platform,file=filename,timeout_seconds=10))
    return {"execution":"native x86_64 host" if platform=="host" else "qemu-arm user emulation on host; NOT hardware timing/RSS", "iterations":iterations,"results":records}


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--directory",type=Path,default=Path(".local/wp0"));p.add_argument("--platform",choices=("host","baby","fab4"),default="host");p.add_argument("--iterations",type=int,default=100);p.add_argument("--output",required=True,type=Path);a=p.parse_args()
    a.output.write_text(json.dumps(run(a.directory,a.platform,a.iterations),indent=2)+"\n")
