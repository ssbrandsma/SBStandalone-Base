"""Deterministic original metadata around existing generated CC0 silence. No network."""
from pathlib import Path
import struct

MEDIA = Path(__file__).parent / "fixtures/media"
TITLE = "Scanner Café 🎵"


def sync(n):
    return bytes((n >> 21 & 127, n >> 14 & 127, n >> 7 & 127, n & 127))


def audio():
    data = (MEDIA / "base.mp3").read_bytes()
    if data.startswith(b"ID3"):
        n = sum(data[6+i] << (21-7*i) for i in range(4))
        data = data[10+n:]
    return data


def mp3(version=4, unsync=False, extended=False, title=TITLE):
    mapping = {"TIT2": "TT2", "TPE1": "TP1", "TALB": "TAL", "TPE2": "TP2",
               "TRCK": "TRK", "TPOS": "TPA", "TYER": "TYE", "TCON": "TCO", "TCMP": "TCP"}
    frames = []
    values = [("TIT2", title), ("TPE1", "Artist One\0Artist Two" if version == 4 else "Artist One"),
              ("TALB", "Two Discs"), ("TPE2", "Various Artists"), ("TRCK", "2/12"),
              ("TPOS", "1/2"), ("TYER", "2024"), ("TCON", "(17)"), ("TCMP", "1")]
    for key, value in values:
        body = (b"\x03" + value.encode("utf8")) if version == 4 else (b"\x01\xff\xfe" + value.encode("utf-16le"))
        if version == 2:
            frames.append(mapping[key].encode() + len(body).to_bytes(3, "big") + body)
        else:
            frames.append(key.encode() + (sync(len(body)) if version == 4 else struct.pack(">I", len(body))) + b"\0\0" + body)
    data = b"".join(frames)
    flags = 0
    if extended:
        assert version in (3, 4)
        data = (sync(6)+b"\x01\0" if version == 4 else struct.pack(">I", 6)+bytes(6)) + data
        flags |= 64
    if unsync:
        assert version == 3
        data = data.replace(b"\xff", b"\xff\0")
        flags |= 128
    return b"ID3"+bytes((version, 0, flags))+sync(len(data))+data+audio()


def compact_flac(rich=False):
    data = (MEDIA / "comments.flac").read_bytes()
    blocks = []
    offset = 4
    while True:
        kind = data[offset] & 127
        last = data[offset] & 128
        size = int.from_bytes(data[offset+1:offset+4], "big")
        body = data[offset+4:offset+4+size]
        if kind != 1:
            if kind == 4 and rich:
                comments = [f"TITLE={TITLE}", "ARTIST=Artist One", "ARTIST=Artist Two", "ALBUM=Two Discs",
                            "ALBUMARTIST=Various Artists", "TRACKNUMBER=2/12", "DISCNUMBER=1/2", "DATE=2024-01-01",
                            "GENRE=Rock", "GENRE=Jazz", "COMPILATION=1"]
                body = struct.pack("<I", 0)+struct.pack("<I", len(comments))
                for comment in comments:
                    b = comment.encode("utf8")
                    body += struct.pack("<I", len(b))+b
            blocks.append((kind, body))
        offset += 4+size
        if last:
            break
    result = b"fLaC"
    for i, (kind, body) in enumerate(blocks):
        result += bytes((kind | (128 if i == len(blocks)-1 else 0),))+len(body).to_bytes(3, "big")+body
    return result+data[offset:]


def populate(root):
    root = Path(root)
    fixtures = {"v22.mp3": mp3(2), "v23.mp3": mp3(3, extended=True), "v24.mp3": mp3(4, extended=True),
                "unsync.mp3": mp3(3, unsync=True), "rich.flac": compact_flac(True), "disc2.flac": compact_flac(True).replace(b"DISCNUMBER=1/2",b"DISCNUMBER=2/2"),
                "comments.ogg": (MEDIA / "comments.ogg").read_bytes(), "untagged.mp3": audio(),
                "id3v1.mp3": (MEDIA / "id3v1.mp3").read_bytes(), "cover-mp3.mp3": (MEDIA / "id3v24.mp3").read_bytes()}
    for name, data in fixtures.items():
        (root / name).write_bytes(data)
    return fixtures


def mpeg_probe(vbri=False):
    # Original structural frames for duration branches; audio decode is not
    # claimed for this synthetic header fixture. Existing silence covers audio.
    frame = bytearray(417)
    frame[:4] = bytes.fromhex("fffb9000")  # MPEG1 Layer III, 128 kbps, 44100 stereo
    if vbri:
        frame[36:40] = b"VBRI"
        frame[40:42] = b"\0\x01"
        frame[50:54] = struct.pack(">I", 2)
    return bytes(frame)+bytes.fromhex("fffb9000")+bytes(413)
