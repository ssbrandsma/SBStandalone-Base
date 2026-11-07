"""Production rendezvous pairing: real music base + real music webserver, no
inherited descriptors. A test-only path shim redirects the hardcoded
production music paths and the mount table into a disposable directory so the
identical bootstrap runs unprivileged. Proves listen/accept/connect/fd-passing
RESET/ACK and surfaces music_ready through authenticated ownership."""
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
# Pinned recovery source: the live model derives a random session source from
# boot entropy, so the fixture and the service must agree on explicit bytes
# via UPDATE media + --music-source-id.
FIXED_SOURCE = b"\x01" + bytes(15)

with tempfile.TemporaryDirectory(prefix="sb-rv-") as workspace, \
        tempfile.TemporaryDirectory(prefix="sb-rv-music-", dir="/dev/shm") as music_dir, \
        tempfile.TemporaryFile() as log:
    root = Path(workspace)
    music = Path(music_dir)
    (music / "one.mp3").write_bytes(mp3(4))
    (music / "two.mp3").write_bytes(mp3(3))
    # Real scan on a real mount; media generation is translated to the live
    # fake mount below, mirroring the controlled fixture translation pattern.
    scan_db = root / "scan.db"
    scanned = subprocess.run([scanner, "scan", str(music), str(scan_db), helper, "1"],
                             capture_output=True, text=True, timeout=60)
    assert scanned.returncode == 0, (scanned.stdout, scanned.stderr)
    rv = root / "rv"
    fake_root = rv / "root"
    fake_media = fake_root / "media" / "x"
    music_root = fake_media / "music"
    music_root.mkdir(parents=True)
    shutil.copy(music / "one.mp3", music_root / "one.mp3")
    shutil.copy(music / "two.mp3", music_root / "two.mp3")
    ram = fake_root / "dev" / "standalone-music-test"
    ram.mkdir(parents=True, mode=0o700)
    dev = os.stat(rv).st_dev
    major, minor = os.major(dev), os.minor(dev)
    lines = Path("/proc/self/mountinfo").read_text().splitlines()
    template = next(line for line in lines if " /dev/shm " in line and " tmpfs " in line)
    before, _, after = template.partition(" - ")
    head = before.split()
    tail = after.split()

    def fake(mount_id, path):
        return (f"{mount_id} {head[1]} {major}:{minor} / {path} rw,nosuid,nodev - "
                f"{tail[0]} {tail[1]} rw,nosuid,nodev")

    # The storage worker only tracks mounts with a /dev block source and
    # re-validates identity every cycle; a sourceless fake would be revoked.
    # fuseblk (like real Fab4 USB sticks) skips the rdev match while still
    # requiring a genuine block node.
    block = next((f"/dev/{e}" for e in sorted(os.listdir("/dev"))
                  if stat.S_ISBLK(os.stat(f"/dev/{e}").st_mode)), None)
    assert block is not None, "worker validation needs a /dev block device"
    lines.append(fake(9001, "/dev/standalone-music-test"))
    lines.append(f"{FAKE_MEDIA_ID} {head[1]} {major}:{minor} / /media/x "
                 f"rw,nosuid,nodev - fuseblk {block} rw,nosuid,nodev")
    mountinfo = rv / "mountinfo"
    mountinfo.write_text("\n".join(lines) + "\n")
    with sqlite3.connect(scan_db) as db:
        # Live generation is the model's first INSERT assignment (1); the
        # mount id only proves mount-table consistency.
        db.execute("UPDATE media SET source=?, generation=?", (FIXED_SOURCE, 1))
        assert db.execute("SELECT count(*) FROM tracks").fetchone()[0] == 2
    # The working database lives on a private ≤8MiB tmpfs inside a mount
    # namespace; the host tree only stages the bytes (self-cleaning).
    stage_db = rv / "stage-library.db"
    shutil.copy(scan_db, stage_db)
    ram_mount = str(ram)
    # The ram line must carry the live tmpfs device numbers: production
    # validation compares the walked mountpoint against the table. Probe with
    # os.stat (a real path the shim passes through); reading mountinfo here
    # would return this very fake table through the shim.
    namespace_setup = (f"mount -t tmpfs -o size=6M tmpfs {ram_mount} && chmod 700 {ram_mount}"
                       f" && cp {stage_db} {ram_mount}/library.db && chmod 600 {ram_mount}/library.db"
                       f" && realdev=$({sys.executable} -c \"import os;st=os.stat('{ram_mount}');"
                       f"print('%d:%d'%(os.major(st.st_dev),os.minor(st.st_dev)))\")"
                       f" && sed -i -E \"s/^(9001 [0-9]+ )[0-9]+:[0-9]+/\\1$realdev/\" {mountinfo}"
                       f" && exec \"$@\"")
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
    common = os.environ | {"SBWEBSERVER_CONFIG_DIR": str(config),
                           "SB_MUSIC_IPC_DIR": str(root / "ipc"),
                           "SB_TS_TEST_RUN_DIR": str(tinysc_run),
                           "SB_TS_TEST_POLICY": str(policy)}
    base_env = common | {"SB_RV_ROOT": str(rv), "LD_PRELOAD": shim}
    wp = subprocess.Popen([web, "--port", str(web_port), "--web-root", str(web_root)],
                          env=common, stdout=log, stderr=log)
    bp = subprocess.Popen(["unshare", "-Urm", "bash", "-c", namespace_setup, "base",
                           base, "--music-root", "/media/x/music",
                           "--music-database", "/dev/standalone-music-test/library.db",
                           "--music-open", "--music-source-id", FIXED_SOURCE.hex(),
                           "--music-helper", helper, "--music-http-port", str(web_port),
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
        print(json.dumps({"rendezvous": "pass", "music_ready": True}))
    finally:
        for p in (bp, wp):
            if p.poll() is None:
                p.terminate()
        for p in (bp, wp):
            p.wait(timeout=10)
    assert not token_path.exists(), "stopped base must remove its capability"
print("Production base/webserver rendezvous pairing passed")
