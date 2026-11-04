#!/usr/bin/env python3
"""Generate CC0 synthetic silence/tag fixtures, never download copyrighted music."""
import argparse
import base64
import json
from pathlib import Path
import struct
import subprocess
import zlib


def sync(n):
    return bytes((n >> shift) & 127 for shift in (21, 14, 7, 0))


def generate(output, ffmpeg):
    output.mkdir(parents=True, exist_ok=True)
    title = "WP0 Caf\u00e9 \U0001f3b5"
    def encode(name, codec, extra=()):
        subprocess.run([ffmpeg,"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","anullsrc=r=44100:cl=stereo","-t","0.2","-c:a",codec,"-metadata", "title="+title,"-metadata","artist=WP0 Artist","-metadata","album=WP0 Album",*extra,str(output/name)],check=True)
    encode("base.mp3", "libmp3lame", ("-id3v2_version","0","-write_id3v1","0"))
    audio = (output/"base.mp3").read_bytes()
    def chunk(kind, data):
        return struct.pack("!I",len(data))+kind+data+struct.pack("!I",zlib.crc32(kind+data))
    png = b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack("!IIBBBBB",1,1,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(b"\0\xff\0\0"))+chunk(b"IEND",b"")
    def frame(name, payload, version):
        if version == 2: return name[:3].encode()+len(payload).to_bytes(3,"big")+payload
        return name.encode()+(sync(len(payload)) if version==4 else struct.pack("!I",len(payload)))+b"\0\0"+payload
    for version in (2,3,4):
        value = b"\x01\xff\xfe"+title.encode("utf-16le") if version!=4 else b"\x03"+title.encode()
        ids = ("TT2","TP1","TAL") if version==2 else ("TIT2","TPE1","TALB")
        tag = frame(ids[0],value,version)+frame(ids[1],b"\0WP0 Artist",version)+frame(ids[2],b"\0WP0 Album",version)
        if version!=2: tag+=frame("APIC",b"\0image/png\0\x03\0"+png,version)
        (output/f"id3v2{version}.mp3").write_bytes(b"ID3"+bytes([version,0,0])+sync(len(tag))+tag+audio)
    jpeg_path=output/"generated.jpg"
    subprocess.run([ffmpeg,"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","color=c=red:s=16x16","-frames:v","1",str(jpeg_path)],check=True)
    jpeg=jpeg_path.read_bytes()
    tag=frame("TIT2",b"\x03"+title.encode(),4)+frame("APIC",b"\0image/jpeg\0\x03\0"+jpeg,4)
    (output/"jpeg-cover.mp3").write_bytes(b"ID3\x04\0\0"+sync(len(tag))+tag+audio)
    large=frame("TIT2",b"\x03"+b"X"*(1024*1024),4)
    (output/"large-tags.mp3").write_bytes(b"ID3\x04\0\0"+sync(len(large))+large+audio)
    v1=b"TAG"+b"WP0 V1".ljust(30,b"\0")+b"WP0 Artist".ljust(30,b"\0")+b"WP0 Album".ljust(30,b"\0")+b"2026"+bytes(30)+b"\0"
    (output/"id3v1.mp3").write_bytes(audio+v1)
    encode("comments.flac","flac")
    # Add an embedded generated PNG PICTURE metadata block, preserving audio.
    p=output/"comments.flac";data=p.read_bytes();offset=4;blocks=[]
    while True:
        header=data[offset:offset+4];size=int.from_bytes(header[1:],"big");blocks.append((header[0]&127,data[offset+4:offset+4+size]));offset+=4+size
        if header[0]&128: break
    picture=struct.pack("!II",3,9)+b"image/png"+struct.pack("!I",0)+struct.pack("!IIIII",1,1,24,0,len(png))+png
    blocks.append((6,picture))
    p.write_bytes(b"fLaC"+b"".join(bytes([kind | (128 if i==len(blocks)-1 else 0)])+len(body).to_bytes(3,"big")+body for i,(kind,body) in enumerate(blocks))+data[offset:])
    encode("comments.ogg","libvorbis",("-metadata","METADATA_BLOCK_PICTURE="+base64.b64encode(picture).decode()))
    (output/"truncated.mp3").write_bytes(b"ID3\x04\0\0"+sync(100)+b"TIT2")
    (output/"oversized.mp3").write_bytes(b"ID3\x04\0\0"+sync(2**27-1)+b"TIT2"+audio)
    (output/"malformed.flac").write_bytes(b"fLaC\x84\xff\xff\xffoops")
    (output/"malformed.ogg").write_bytes(b"OggS"+bytes(40))
    (output/"invalid-utf8.mp3").write_bytes(b"ID3\x04\0\0"+sync(13)+b"TIT2"+sync(3)+b"\0\0\3\xff\xfe"+audio)
    (output/"manifest.json").write_text(json.dumps({"license":"CC0-1.0; generated silence and original test tags/images", "title":title,"sample_rate":44100,"channels":2,"duration_seconds":0.2,"files":sorted(p.name for p in output.iterdir() if p.suffix in (".mp3",".flac",".ogg"))},ensure_ascii=True,indent=2))


if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("output",type=Path);parser.add_argument("--ffmpeg",default="ffmpeg");args=parser.parse_args();generate(args.output,args.ffmpeg)
