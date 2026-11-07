"""Production store-mode pairing: snapshot library + idle-gated playback.

Serves a crafted SBMUSIC1 snapshot through the store path (the only path
that can serve a whole-mount library) with --music-idle-only, pairs base
and webserver over rendezvous IPC, and requires music_ready. A preceding
open-check leg proves the same snapshot serves its tracks through the same
binary. The fixture shadows /dev with a tmpfs inside a user+mount namespace
(the production store work area); only urandom/null/zero are bound through.
Disk-identity aliases are fixture symlinks so snapshot-source derivation is
deterministic on any host.
"""
import hashlib
import http.client
import json
import os
import shutil
import socket
import sqlite3
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from wp2_fixtures import mp3

base, web, scanner, helper, shim = map(lambda p: str(Path(p).resolve()), sys.argv[1:6])

FAKE_MEDIA_ID = 9002
UUID = "RV-STORE-UUID-01"
HW = "usb:cafe:beef:RVSTORE01:partition:1"
EXPECTED_SOURCE = hashlib.sha256((UUID + "\0" + HW + "\0").encode()).hexdigest()[:32]

with tempfile.TemporaryDirectory(prefix="sb-rv-store-") as workspace, \
        tempfile.TemporaryDirectory(prefix="sb-rv-store-scan-", dir="/dev/shm") as scan_src, \
        tempfile.TemporaryFile() as log:
    root = Path(workspace)
    music = Path(scan_src)
    (music / "one.mp3").write_bytes(mp3(4))
    (music / "two.mp3").write_bytes(mp3(3))
    scan_db = root / "scan.db"
    scanned = subprocess.run([scanner, "scan", str(music), str(scan_db), helper, "1"],
                             capture_output=True, text=True, timeout=60)
    assert scanned.returncode == 0, (scanned.stdout, scanned.stderr)
    with sqlite3.connect(scan_db) as db:
        db.execute("UPDATE media SET source=?, generation=?",
                   (bytes.fromhex(EXPECTED_SOURCE), FAKE_MEDIA_ID))
        assert db.execute("SELECT count(*) FROM tracks").fetchone()[0] == 2
    payload = scan_db.read_bytes()
    assert len(payload) >= 4096
    rv = root / "rv"
    fake_media = rv / "root" / "media" / "x"
    store = fake_media / ".standalone-music"
    store.mkdir(parents=True)
    for name in ("one.mp3", "two.mp3"):
        shutil.copy(music / name, fake_media / name)
    header = ("SBMUSIC1 1 %d %s %s\n" % (len(payload), hashlib.sha256(payload).hexdigest(),
                                         EXPECTED_SOURCE)).encode()
    assert len(header) < 192
    envelope = store / "library.0.snapshot"
    envelope.write_bytes(header + bytes(192 - len(header)) + payload)
    envelope.chmod(0o644)
    block = next((f"/dev/{e}" for e in sorted(os.listdir("/dev"))
                  if stat.S_ISBLK(os.stat(f"/dev/{e}").st_mode)), None)
    assert block is not None, "worker validation needs a /dev block device"
    disk = rv / "root" / "dev" / "disk"
    (disk / "by-uuid").mkdir(parents=True)
    (disk / "by-id").mkdir(parents=True)
    os.symlink(block, disk / "by-uuid" / UUID)
    os.symlink(block, disk / "by-id" / HW)
    dev = os.stat(rv).st_dev
    major, minor = os.major(dev), os.minor(dev)
    lines = Path("/proc/self/mountinfo").read_text().splitlines()
    template = next(line for line in lines if " /dev/shm " in line and " tmpfs " in line)
    before, _, after = template.partition(" - ")
    head = before.split()
    lines.append(f"{FAKE_MEDIA_ID} {head[1]} {major}:{minor} / /media/x "
                 f"rw,nosuid,nodev - fuseblk {block} rw,nosuid,nodev")
    (rv / "mountinfo").write_text("\n".join(lines) + "\n")
    token_dir = root / "owner"
    token_dir.mkdir(mode=0o700)
    token_path = token_dir / "token"
    tinysc_run = root / "tinysc-run"
    tinysc_run.mkdir(mode=0o700)
    policy = root / "tinysc-arbitration.enabled"
    policy.write_text("test\n")
    policy.chmod(0o600)
    web_root = root / "web"
    web_root.mkdir()
    (web_root / "index.html").write_text("fixture")
    config = root / "config"
    config.mkdir()
    (config / "settings.conf").write_text("readonly=1\n")
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    web_port = probe.getsockname()[1]
    probe.close()
    shadow = rv / "shadow-dev"
    shadow.mkdir()
    blkname = os.path.basename(block)
    # Setup utilities run without the shim (real mount table, real paths);
    # only the exec'd base sees redirected production paths. The shadow /dev
    # must also carry the media block node: identity validation stats it.
    namespace_setup = ("mkdir -p {shadow} && touch {shadow}/urandom {shadow}/null {shadow}/zero {shadow}/blk"
                       " && env -u LD_PRELOAD mount --bind /dev/urandom {shadow}/urandom"
                       " && env -u LD_PRELOAD mount --bind /dev/null {shadow}/null"
                       " && env -u LD_PRELOAD mount --bind /dev/zero {shadow}/zero"
                       " && env -u LD_PRELOAD mount --bind {block} {shadow}/blk"
                       " && env -u LD_PRELOAD mount -t tmpfs -o size=16M tmpfs /dev"
                       " && touch /dev/urandom /dev/null /dev/zero /dev/{blkname}"
                       " && env -u LD_PRELOAD mount --bind {shadow}/urandom /dev/urandom"
                       " && env -u LD_PRELOAD mount --bind {shadow}/null /dev/null"
                       " && env -u LD_PRELOAD mount --bind {shadow}/zero /dev/zero"
                       " && env -u LD_PRELOAD mount --bind {shadow}/blk /dev/{blkname}"
                       " && exec \"$@\"").format(shadow=shadow, block=block, blkname=blkname)
    check_env = dict(os.environ, SB_RV_ROOT=str(rv), LD_PRELOAD=shim)
    checked = subprocess.run(["unshare", "-Urm", "bash", "-c", namespace_setup, "check",
                              base, "--music-open-check", "--music-browse-only",
                              "--music-root", "/media/x", "--music-store", ".standalone-music",
                              "--music-db-limit-mib", "32"],
                             env=check_env, capture_output=True, text=True, timeout=60)
    assert checked.returncode == 0, (checked.stdout, checked.stderr)
    report = json.loads(checked.stdout)
    assert report["tracks"] == 2 and not report["scan_started"] \
        and not report["listeners_started"], report
    common = os.environ | {"SBWEBSERVER_CONFIG_DIR": str(config),
                           "SB_MUSIC_IPC_DIR": str(root / "ipc"),
                           "SB_TS_TEST_RUN_DIR": str(tinysc_run),
                           "SB_TS_TEST_POLICY": str(policy)}
    base_env = common | {"SB_RV_ROOT": str(rv), "LD_PRELOAD": shim}
    wp = subprocess.Popen([web, "--port", str(web_port), "--web-root", str(web_root)],
                          env=common, stdout=log, stderr=log)
    bp = subprocess.Popen(["unshare", "-Urm", "bash", "-c", namespace_setup, "base",
                           base, "--music-root", "/media/x",
                           "--music-store", ".standalone-music",
                           "--music-db-limit-mib", "32",
                           "--music-http-port", str(web_port),
                           "--ownership-token", str(token_path), "--music-idle-only",
                           "--tinysc-arbitration"],
                          env=base_env, stdout=log, stderr=log)

    def base_request(body, token):
        c = http.client.HTTPConnection("127.0.0.1", 9000, timeout=5)
        c.request("POST", "/api/music/ownership", body,
                  {"X-Music-Ownership": token, "Content-Type": "text/plain"})
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, json.loads(data)

    try:
        until = time.monotonic() + 25
        token = None
        while time.monotonic() < until:
            if token_path.exists():
                try:
                    candidate = token_path.read_text()
                    status, _ = base_request("SBOWN1 hello 1 0 1 0 0 0 0 0 0 0", candidate)
                    if status == 200:
                        token = candidate
                        break
                except (OSError, ValueError):
                    pass
            if bp.poll() is not None or wp.poll() is not None:
                break
            time.sleep(0.05)
        log.seek(0)
        tail = log.read().decode(errors="replace")[-2000:]
        assert token is not None, ("base startup", bp.poll(), wp.poll(), tail)
        assert bp.poll() is None and wp.poll() is None
        until = time.monotonic() + 20
        ready = None
        while time.monotonic() < until:
            status, snapshot = base_request("SBOWN1 hello 1 0 1 0 0 0 0 0 0 0", token)
            assert status == 200, (status, snapshot)
            if snapshot.get("music_ready"):
                ready = snapshot
                break
            time.sleep(0.1)
        assert ready is not None, "rendezvous IPC never became music_ready"
        assert ready["idle_only"] and not ready["idle_admitted"], ready
        c = http.client.HTTPConnection("127.0.0.1", web_port, timeout=5)
        c.request("GET", "/")
        r = c.getresponse()
        r.read()
        c.close()
        assert r.status == 200, r.status
        print(json.dumps({"rendezvous_store": "pass", "music_ready": True}))
    finally:
        for p in (bp, wp):
            if p.poll() is None:
                p.terminate()
        for p in (bp, wp):
            p.wait(timeout=10)
    assert not token_path.exists(), "stopped base must remove its capability"
print("Production store-mode base/webserver rendezvous pairing passed")
