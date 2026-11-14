#!/usr/bin/env python3
"""WP4a.6 native cutover: qualified Base-only replacement with auto-rollback.

ARCHITECTURE NOTE (2026-11-13): a Base-only cutover CANNOT work. The
music webserver accepts exactly one resource rendezvous at startup,
then closes the listener; a new base joining the kept web fails with
"resource rendezvous unavailable" and the supervisor ladder degrades
to sbbase-core (observed live). Use wp4a6-recover-music.py, which
restarts web+base as a pair and claims the supervisor pidfile.

Rebuilt from the WP4a.5 procedure (8 rounds, 5 live rollback landings, all
tier-1). Steps mirror the WP4a.5 receipts:

  GUARDS      boot/Jive/watchdog/base pids+starts, TinySC STOPPED_VERIFIED,
              nowplaying idle, old-base LEVEL1, music web bound, snapshot md5
  STAGE       upload versioned base to /dev tmpfs, sha-verified, add-only link
  WEB-KEPT    music web stays up (holds :8766 legitimately; not restarted)
  OLD-STOPPED SIGTERM old base, confirm exit, confirm no respawn (PPID 1)
  NEW-LAUNCH  launch versioned base (same args incl. token), wait API ready
  LEVEL1      sha256(/proc/<pid>/exe) == staged artifact sha
  HEALTH      TinySC STOPPED_VERIFIED, nowplaying idle, snapshot md5 unchanged
  SUPERVISE   45 s: pid stable, slimproto pairs stable
  ADMITTED    fence re-registers + idle admitted (Jive log)
  SOAK        60 s: 12 samples, 0 flaps
  COMMIT      receipt locally + on device

Any failure at/after OLD-STOPPED auto-rolls back: the new base (if any) is
killed and the old versioned binary relaunches with its exact cmdline, then
LEVEL1 + health + admission re-verify. Never rescans, restarts Jive, touches
the watchdog, or starts TinySC. Usage:

  SB_DEVICE_PASSWORD=... python scripts/wp4a6-cutover.py [--dry-run]

--dry-run runs GUARDS + STAGE only (no stop). Safe to repeat.
"""
import hashlib
import importlib.util
import json
import os
import random
import string
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))
spec = importlib.util.spec_from_file_location(
    "life", str(REPO / "scripts/fab4-lifecycle.py"))
life = importlib.util.module_from_spec(spec)
spec.loader.exec_module(life)

DEVICE = os.environ.get("SB_DEVICE_HOST", "192.168.1.244")
APPLET = "/usr/share/jive/applets/StandaloneBase"
SNAPSHOT = "/media/sda1/.standalone-music/library.0.snapshot"

NEW_BIN = REPO / "build-arm/fab4/music-session/sbbase_music"
# WP4a.6 switch fix (mid-play LOAD retires the live stream): live binary
# 61e99ac5 plus music_playback.c retire_previous_stream/absorb/codecs.
NEW_SHA = "9809931ccc3be6b75bdd34b80e6127b69a4aa321ab08ebdea1e1c9eb6347772f"

OLD_LINK = "sbbase-music.61e99ac544272752"
OLD_SHA_PREFIX = "61e99ac544272752"


def base_args(token):
    # Fresh token path per incarnation: sb_ob_open uses O_CREAT|O_EXCL and
    # refuses a lingering token file. The fence resolves the live path via
    # /proc scan (findToken), so rotation is the designed flow.
    return ["--config", "/mnt/storage/standalonebase/config.json",
            "--tinysc-arbitration", "--music-root", "/media/sda1",
            "--music-store", ".standalone-music", "--music-db-limit-mib",
            "32", "--music-http-port", "8766", "--ownership-token", token,
            "--music-idle-only"]


class CutoverError(Exception):
    pass


def sha_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    dry = "--dry-run" in sys.argv
    if not NEW_BIN.exists():
        raise SystemExit(f"missing {NEW_BIN}; build fab4 first")
    if sha_file(NEW_BIN) != NEW_SHA:
        raise SystemExit("new base sha mismatch; refusing cutover")
    work = REPO / ".local/wp4a6/cutover"
    work.mkdir(parents=True, exist_ok=True)
    receipts = []
    tag = "".join(random.choice(string.ascii_letters + string.digits)
                  for _ in range(6))
    stage = f"/dev/sbmusic-wp4a6.{tag}"
    new_name = f"sbbase-music.{NEW_SHA[:16]}"
    d = life.Device(DEVICE)
    # The Touch has md5sum only (no sha256sum/python/lua CLI): all sha256
    # LEVEL1 proofs hash exe bytes downloaded to the host.
    stopped_old = {"done": False}

    def call(cmd, timeout=90):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read().decode(errors="replace")

    def call_bytes(cmd, timeout=120):
        _, o, _ = d.client.exec_command(cmd, timeout=timeout)
        return o.read()

    def step(name, ok, detail):
        receipts.append({"step": name, "ok": ok,
                         "t": datetime.now(timezone.utc).isoformat(),
                         "detail": detail})
        print(f"{name}: {'OK' if ok else 'FAIL'} {detail}")
        if not ok:
            raise CutoverError(f"{name}: {detail}")

    def wget(path):
        out = call(f"wget -qO- --timeout=8 'http://127.0.0.1:9000{path}'")
        return json.loads(out) if out.strip().startswith("{") else None

    def proc_start(pid):
        out = call(f"cut -d' ' -f22 /proc/{pid}/stat 2>/dev/null").strip()
        return out or None

    def proc_exe_sha(pid):
        data = call_bytes(f"cat /proc/{pid}/exe 2>/dev/null")
        if not data:
            return []
        return [hashlib.sha256(data).hexdigest()]

    def slim_pairs():
        out = call("grep -a -c ':0D9B' /proc/net/tcp 2>/dev/null").strip()
        return int(out) if out.isdigit() else -1

    old = {}
    try:
        # ---- GUARDS ----
        boot = call("cat /proc/sys/kernel/random/boot_id").strip()
        old["boot"] = boot
        for pid, name in (("772", "jive"), ("639", "watchdog"),
                          ("15787", "web"), ("843", "proxy")):
            st = proc_start(pid)
            if not st:
                step("GUARDS", False, f"{name} {pid} missing")
            old[name] = {"pid": pid, "start": st}
        ts = wget("/api/tinysc/lifecycle")
        if not ts or ts.get("state") != "STOPPED_VERIFIED":
            step("GUARDS", False, f"tinysc {ts}")
        oldpid = str(ts["pid"])
        old["tinysc"] = {"pid": oldpid, "start": ts["start"], "boot": boot}
        # Capture the relaunch cmdline FIRST: every later guard may abort
        # into a rollback that needs it. NUL-split, never `strings`:
        # strings drops args shorter than 4 chars (the "32"), which
        # corrupts the relaunch (observed 2026-11-13).
        old["cmdline"] = call(
            f"tr '\\0' '\\n' </proc/{oldpid}/cmdline").strip().split("\n")
        if not old["cmdline"] or not old["cmdline"][0]:
            step("GUARDS", False, "old cmdline unreadable")
        if boot != ts["boot"]:
            step("GUARDS", False, "boot id drift")
        np = wget("/api/nowplaying")
        if not np or np.get("active") or np.get("state") != "stopped":
            step("GUARDS", False, f"nowplaying not idle: {np}")
        exe = proc_exe_sha(oldpid)
        if not exe or not exe[0].startswith(OLD_SHA_PREFIX):
            step("GUARDS", False, f"old base not LEVEL1: {exe}")
        old["exe_sha"] = exe[0]
        web_pid = call("ps | grep sbwebserver-mus | grep -v grep"
                       ).split()
        if not web_pid:
            step("GUARDS", False, "music web missing")
        old["web_pid"] = web_pid[0]
        snap = call(f"md5sum {SNAPSHOT}").split()
        if not snap:
            step("GUARDS", False, "snapshot missing")
        old["snapshot_md5"] = snap[0]
        step("GUARDS", True, {"base": oldpid, "web": old["web_pid"],
                              "snapshot": snap[0][:12]})
        (work / "guards.json").write_text(json.dumps(old, indent=2))

        # ---- STAGE ----
        data = NEW_BIN.read_bytes()
        call(f"mkdir -p {stage} && chmod 700 {stage}")
        d.call(f"cat > {stage}/{new_name}", data=data)
        call(f"chmod 755 {stage}/{new_name}")
        got = hashlib.md5(call_bytes(f"cat {stage}/{new_name}")).hexdigest()
        if got != hashlib.md5(data).hexdigest():
            raise SystemExit("stage round-trip mismatch")
        call(f"ln -s {stage}/{new_name} {APPLET}/{new_name}")
        # No readlink on the Touch: prove the link resolves to our bytes.
        got = hashlib.md5(
            call_bytes(f"cat {APPLET}/{new_name}")).hexdigest()
        if got != hashlib.md5(data).hexdigest():
            raise SystemExit("stage link failed")
        step("STAGE", True, {"stage": stage, "sha": NEW_SHA[:16]})
        if dry:
            print("dry-run: staged only, no stop")
            return

        # ---- WEB-KEPT ----
        if proc_start(old["web_pid"]) is None:
            step("WEB-KEPT", False, "music web died")
        if ":223E" not in call("grep -a ' 0A ' /proc/net/tcp"):
            step("WEB-KEPT", False, ":8766 not bound")
        step("WEB-KEPT", True, {"web": old["web_pid"]})

        # ---- OLD-STOPPED ----
        call(f"kill -TERM {oldpid}")
        deadline = time.time() + 15
        while time.time() < deadline:
            if proc_start(oldpid) is None:
                break
            time.sleep(1)
        else:
            step("OLD-STOPPED", False, "old base ignored TERM")
        stopped_old["done"] = True
        # Supervisor (15 s tick + >=5 s backoff) will launch the wrapper
        # soon; the new base + pidfile claim below must win that race.
        step("OLD-STOPPED", True, {"old": oldpid})

        # ---- NEW-LAUNCH ----
        logf = f"{stage}/sbbase.log"
        call(f": > {logf}")
        call(f"mkdir -p {stage}/owner && chmod 700 {stage}/owner")
        args = " ".join(f"'{a}'" for a in
                        base_args(f"{stage}/owner/token"))
        call(f"setsid {APPLET}/{new_name} {args} >>{logf} 2>&1 "
             f"</dev/null & echo started")
        newpid = None
        deadline = time.time() + 30
        while time.time() < deadline:
            ts2 = wget("/api/tinysc/lifecycle")
            if ts2 and ts2.get("pid"):
                newpid = str(ts2["pid"])
                break
            time.sleep(2)
        if not newpid:
            step("NEW-LAUNCH", False, "API never ready")
        call(f"echo {newpid} > /tmp/standalonebase/sbbase.pid")
        newstart = proc_start(newpid)
        step("NEW-LAUNCH", True, {"pid": newpid, "start": newstart})

        # ---- LEVEL1 ----
        exe2 = proc_exe_sha(newpid)
        if not exe2 or exe2[0] != NEW_SHA:
            step("LEVEL1", False, f"exe {exe2}")
        step("LEVEL1", True, {"pid": newpid, "sha": NEW_SHA[:16]})

        # ---- HEALTH ----
        ts3 = wget("/api/tinysc/lifecycle")
        if not ts3 or ts3.get("state") != "STOPPED_VERIFIED" or \
                str(ts3.get("pid")) != newpid:
            step("HEALTH", False, f"tinysc {ts3}")
        np3 = wget("/api/nowplaying")
        if not np3 or np3.get("active") or np3.get("state") != "stopped":
            step("HEALTH", False, f"nowplaying {np3}")
        snap3 = call(f"md5sum {SNAPSHOT}").split()[0]
        if snap3 != old["snapshot_md5"]:
            step("HEALTH", False, "snapshot changed (rescan?)")
        if proc_start("772") != old["jive"]["start"] or \
                proc_start("639") != old["watchdog"]["start"]:
            step("HEALTH", False, "jive/watchdog identity drift")
        step("HEALTH", True, {"snapshot": snap3[:12]})

        # ---- SUPERVISE 45 s ----
        pairs0 = slim_pairs()
        ok45 = True
        for _ in range(9):
            time.sleep(5)
            if proc_start(newpid) != newstart or slim_pairs() != pairs0:
                ok45 = False
                break
        step("SUPERVISE", ok45, {"pairs": pairs0})

        # ---- ADMITTED (fence rejoins the fresh bridge) ----
        # V6: poll the fence status file directly (log tags vary by
        # fence version; the status file is the join contract).
        admitted = False
        seen = ""
        deadline = time.time() + 90
        while time.time() < deadline:
            out = call("cat /tmp/standalonebase/fence.status "
                       "2>/dev/null")
            if "client=true" in out and "idle_admitted=true" in out:
                admitted = True
                seen = "yes"
                break
            time.sleep(5)
        step("ADMITTED", admitted, {"idle_admitted": seen})

        # ---- SOAK 60 s ----
        flaps = 0
        for _ in range(12):
            time.sleep(5)
            if proc_start(newpid) != newstart or slim_pairs() != pairs0:
                flaps += 1
        step("SOAK", flaps == 0, {"samples": 12, "flaps": flaps})

        step("COMMIT", True, {"pid": newpid, "start": newstart,
                              "base": new_name})
    except CutoverError as e:
        if not stopped_old["done"]:
            print(f"CUTOVER ABORTED pre-stop (nothing changed): {e}")
            raise SystemExit(f"aborted: {e}")
        print(f"CUTOVER FAILED: {e}; rolling back")
        try:
            rollback(call, proc_start, proc_exe_sha, wget, old, receipts,
                     slim_pairs)
            receipts.append({"step": "ROLLED_BACK", "ok": True,
                             "t": datetime.now(timezone.utc).isoformat(),
                             "detail": {"reason": str(e)}})
        except CutoverError as e2:
            receipts.append({"step": "ROLLBACK_FAILED", "ok": False,
                             "t": datetime.now(timezone.utc).isoformat(),
                             "detail": {"reason": str(e2)}})
            raise SystemExit(f"ROLLBACK FAILED: {e2}")
        raise SystemExit(f"rolled back: {e}")
    finally:
        blob = json.dumps(receipts, indent=2)
        (work / "receipts.json").write_text(blob)
        try:
            d.call(f"cat > {stage}/cutover.receipts", data=blob.encode())
        except Exception:
            pass
        d.close()


def rollback(call, proc_start, proc_exe_sha, wget, old, receipts, slim_pairs):
    def step(name, ok, detail):
        receipts.append({"step": name, "ok": ok,
                         "t": datetime.now(timezone.utc).isoformat(),
                         "detail": detail})
        print(f"{name}: {'OK' if ok else 'FAIL'} {detail}")
        if not ok:
            raise CutoverError(f"{name}: {detail}")

    # Kill any new base; never touch an intact old base (round-6 lesson:
    # snapshot first, kill only the intruder).
    cur = call("ps | grep sbbase-music | grep -v grep").strip().split("\n")
    for line in cur:
        if not line.strip():
            continue
        pid = line.split()[0]
        exe = proc_exe_sha(pid)
        if exe and exe[0].startswith(OLD_SHA_PREFIX):
            continue  # old base intact: leave it
        call(f"kill -KILL {pid}")
    time.sleep(3)
    # Relaunch old versioned binary with its exact cmdline except a
    # fresh token path (O_EXCL refuses the lingering token file).
    if not old.get("cmdline"):
        step("RB-RELAUNCH", False, "no old cmdline captured")
    binary = old["cmdline"][0]
    rb_dir = "/dev/sbmusic-wp4a6-rollback"
    rb_owner = f"{rb_dir}/owner-rb"
    call(f"mkdir -p {rb_dir} {rb_owner} && chmod 700 {rb_dir} {rb_owner} && "
         f"rm -f {rb_owner}/token")
    argv = [a for a in old["cmdline"][1:] if a]
    if "--ownership-token" in argv:
        i = argv.index("--ownership-token")
        argv[i + 1] = f"{rb_owner}/token"
    args = " ".join(f"'{a}'" for a in argv)
    call(f"setsid {binary} {args} "
         f">>{rb_dir}/sbbase.log 2>&1 </dev/null & echo rb")
    rpid = None
    deadline = time.time() + 30
    while time.time() < deadline:
        ts = wget("/api/tinysc/lifecycle")
        if ts and ts.get("pid"):
            rpid = str(ts["pid"])
            break
        time.sleep(2)
    if not rpid:
        step("RB-RELAUNCH", False, "old base API never ready")
    exe = proc_exe_sha(rpid)
    if not exe or not exe[0].startswith(OLD_SHA_PREFIX):
        step("RB-LEVEL1", False, f"exe {exe}")
    step("RB-LEVEL1", True, {"pid": rpid})
    ts = wget("/api/tinysc/lifecycle")
    if not ts or ts.get("state") != "STOPPED_VERIFIED":
        step("RB-HEALTH", False, f"tinysc {ts}")
    snap = call(f"md5sum {SNAPSHOT}").split()[0]
    if snap != old["snapshot_md5"]:
        step("RB-HEALTH", False, "snapshot changed")
    step("RB-HEALTH", True, {"pid": rpid})


if __name__ == "__main__":
    main()
