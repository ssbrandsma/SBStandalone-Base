#!/usr/bin/env python3
"""WP4a.6 music-stack recovery: relaunch music web + versioned base together.

Why this exists: a Base-only replacement cannot work. The music webserver
accepts exactly ONE resource rendezvous at startup, then closes the
rendezvous listener. A new base joining a kept webserver fails startup
with "resource rendezvous unavailable", and the supervisor ladder
degrades to the plain sbbase-core (observed 2026-11-13: cutover LEVEL1
failed, API owned by sbbase-core pid 6746). Recovery must restart the
webserver (fresh rendezvous) and the base as a pair, fast enough to
beat the supervisor tick that launches sbbase-core.

Also fixes a rollback landmine found the same night: capturing a
cmdline with `strings` drops arguments shorter than 4 chars (the "32"
in --music-db-limit-mib 32), so a strings-captured relaunch passes
corrupt args. This driver quotes from NUL-split /proc cmdlines only.

Launches the staged NEW base (switch fix) by default; --old relaunches
the previous known-good base instead. Never rescans, restarts Jive,
touches the watchdog, or starts TinySC.
"""
import hashlib
import importlib.util
import json
import os
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
STAGE = "/dev/sbmusic-wp4a6.2A4ei9"  # verified by cutover STAGE step
NEW_NAME = "sbbase-music.9809931ccc3be6b7"
NEW_SHA = "9809931ccc3be6b75bdd34b80e6127b69a4aa321ab08ebdea1e1c9eb6347772f"
OLD_NAME = "sbbase-music.61e99ac544272752"
OLD_SHA = "61e99ac544272752"  # prefix; full sha in guards record
WEB_BIN = "/dev/sbmusic-wp4a6.yRxbCj/sbwebserver-music.482b85db08e9f44b"
WEB_CONFIG = "/dev/sbmusic-wp4a6.yRxbCj/web-config"


def base_args(token):
    return ["--config", "/mnt/storage/standalonebase/config.json",
            "--tinysc-arbitration", "--music-root", "/media/sda1",
            "--music-store", ".standalone-music", "--music-db-limit-mib",
            "32", "--music-http-port", "8766", "--ownership-token", token,
            "--music-idle-only"]


class RecoverError(Exception):
    pass


def main():
    use_old = "--old" in sys.argv
    want_name = OLD_NAME if use_old else NEW_NAME
    want_sha = OLD_SHA if use_old else NEW_SHA
    work = REPO / ".local/wp4a6/recover-music"
    work.mkdir(parents=True, exist_ok=True)
    receipts = []
    d = life.Device(DEVICE)

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
        print(f"{name}: {'OK' if ok else 'FAIL'} {detail}", flush=True)
        if not ok:
            raise RecoverError(f"{name}: {detail}")

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

    def wait_exit(pid, secs=10):
        deadline = time.time() + secs
        while time.time() < deadline:
            if proc_start(pid) is None:
                return True
            time.sleep(1)
        return False

    try:
        # ---- PRE ----
        boot = call("cat /proc/sys/kernel/random/boot_id").strip()
        pre = {"boot": boot, "want": want_name}
        for pid, name in (("772", "jive"), ("639", "watchdog")):
            st = proc_start(pid)
            if not st:
                step("PRE", False, f"{name} {pid} missing")
            pre[name] = {"pid": pid, "start": st}
        snap = call(f"md5sum {SNAPSHOT}").split()
        if not snap:
            step("PRE", False, "snapshot missing")
        pre["snapshot_md5"] = snap[0]
        marker = call(f"cat /var/run/standalonebase/music-fence.enabled "
                      f"2>&1").strip()
        if "enabled" not in marker and "1" not in marker:
            call("mkdir -p /var/run/standalonebase && "
                 "echo 1 > /var/run/standalonebase/music-fence.enabled")
            marker = "recreated"
        pre["marker"] = marker
        # Staged candidate must be the exact verified bytes.
        got = hashlib.md5(call_bytes(f"cat {STAGE}/{want_name}")).hexdigest()
        want_md5 = hashlib.md5((REPO / "build-arm/fab4/music-session"
                                / "sbbase_music").read_bytes()).hexdigest() \
            if not use_old else None
        if not use_old and got != want_md5:
            step("PRE", False, "staged base mismatch")
        if use_old:
            old_bytes = call_bytes(f"cat {APPLET}/{want_name}")
            if not old_bytes or hashlib.sha256(old_bytes).hexdigest(
                    )[:16] != OLD_SHA:
                step("PRE", False, "old base file missing/corrupt")
        web = [l.split()[0] for l in
               call("ps -o pid,args | grep sbwebserver-music | "
                    "grep -v grep").strip().split("\n") if l.strip()]
        core = [l.split()[0] for l in
                call("ps -o pid,args | grep 'sbbase-core' | "
                     "grep -v grep").strip().split("\n") if l.strip()]
        pre["web_pids"] = web
        pre["core_pids"] = core
        step("PRE", True, pre)
        (work / "pre.json").write_text(json.dumps(pre, indent=2))

        # ---- STOP (single chain; the supervisor tick is the race) ----
        for pid in core:
            call(f"kill -TERM {pid}")
        for pid in web:
            call(f"kill -TERM {pid}")
        for pid in core + web:
            if not wait_exit(pid, 10):
                call(f"kill -KILL {pid}")
                if not wait_exit(pid, 5):
                    step("STOP", False, f"pid {pid} survives KILL")
        # No listener may hold the music ports now.
        holders = call("grep -a -E ':(0D9B|223E)' /proc/net/tcp "
                       "2>/dev/null").strip()
        # (informational: TIME_WAIT rows may linger; binds still work)
        step("STOP", True, {"core": core, "web": web,
                            "tcp_leftovers": bool(holders)})

        # ---- WEB + BASE (back-to-back in ONE chain) ----
        # The web blocks in rendezvous_accept (180 s) until the base
        # connects, so waiting for :8766 between the launches
        # deadlocks. Both go up in a single fast chain to beat the
        # supervisor tick that launches sbbase-core. Single-line,
        # absolute paths, no shell variables: multi-line chains
        # mangled in transport on 2026-11-13.
        tokdir = f"{STAGE}/owner2"
        logf = f"{STAGE}/sbbase2.log"
        wlog = f"{STAGE}/web2.log"
        call(f"mkdir -p {tokdir} && chmod 700 {tokdir} && "
             f"rm -f {tokdir}/token")
        wargv = (f"{WEB_BIN} --port 8766 --web-root {APPLET} "
                 f"--config-dir {WEB_CONFIG}")
        args = " ".join(base_args(f"{tokdir}/token"))
        chain = (f": > {wlog}; : > {logf}; "
                 f"setsid {wargv} >>{wlog} 2>&1 </dev/null & sleep 1; "
                 f"setsid {APPLET}/{want_name} {args} >>{logf} 2>&1 "
                 f"</dev/null & echo launched")
        call(chain, timeout=60)
        # Claim the supervisor pidfile fast: alive() prefix-matches
        # ROOT/sbbase against our cmdline, so our pid fences it.
        time.sleep(4)
        call("pgrep -f sbbase-music. > /tmp/standalonebase/sbbase.pid; "
             "cat /tmp/standalonebase/sbbase.pid")
        webs = [l.split()[0] for l in
                call("ps -o pid,args | grep sbwebserver-music | "
                     "grep -v grep").strip().split("\n") if l.strip()]
        if len(webs) != 1:
            step("WEB", False, {"webs": webs,
                                "log": call(f"tail -5 {wlog}")})
        step("WEB", True, {"pid": webs[0]})
        newpid = None
        deadline = time.time() + 30
        while time.time() < deadline:
            ts = wget("/api/tinysc/lifecycle")
            if ts and ts.get("pid"):
                newpid = str(ts["pid"])
                break
            time.sleep(2)
        if not newpid:
            step("BASE", False, {"log": call(f"tail -8 {logf}")})
        # If the supervisor ladder won the race, its core owns the API
        # instead of our base: evict the intruder once, then re-read.
        exe = proc_exe_sha(newpid)
        if not exe or not exe[0].startswith(want_sha[:16]):
            intruder = newpid
            call(f"kill -KILL {intruder}")
            wait_exit(intruder, 8)
            time.sleep(2)
            ts = wget("/api/tinysc/lifecycle")
            newpid = str(ts["pid"]) if ts and ts.get("pid") else None
            exe = proc_exe_sha(newpid) if newpid else []
            if not newpid or not exe or \
                    not exe[0].startswith(want_sha[:16]):
                step("BASE", False,
                     {"intruder": intruder, "pid": newpid, "exe": exe,
                      "log": call(f"tail -8 {logf}")})
        newstart = proc_start(newpid)
        step("BASE", True, {"pid": newpid, "start": newstart})

        # ---- LEVEL1 ----
        exe2 = proc_exe_sha(newpid)
        if not exe2 or not exe2[0].startswith(want_sha[:16]) or \
                (not use_old and exe2[0] != NEW_SHA):
            step("LEVEL1", False, f"exe {exe2}")
        step("LEVEL1", True, {"pid": newpid, "sha": exe2[0][:16]})

        # ---- HEALTH ----
        ts3 = wget("/api/tinysc/lifecycle")
        if not ts3 or ts3.get("state") != "STOPPED_VERIFIED" or \
                str(ts3.get("pid")) != newpid:
            step("HEALTH", False, f"tinysc {ts3}")
        np3 = wget("/api/nowplaying")
        if not np3 or np3.get("active") or np3.get("state") != "stopped":
            step("HEALTH", False, f"nowplaying {np3}")
        snap3 = call(f"md5sum {SNAPSHOT}").split()[0]
        if snap3 != pre["snapshot_md5"]:
            step("HEALTH", False, "snapshot changed (rescan?)")
        if proc_start("772") != pre["jive"]["start"] or \
                proc_start("639") != pre["watchdog"]["start"]:
            step("HEALTH", False, "jive/watchdog identity drift")
        step("HEALTH", True, {"snapshot": snap3[:12]})

        # ---- ADMITTED ----
        admitted = False
        deadline = time.time() + 90
        while time.time() < deadline:
            out = call("cat /tmp/standalonebase/fence.status 2>/dev/null")
            if "client=true" in out and "idle_admitted=true" in out:
                admitted = True
                break
            time.sleep(5)
        step("ADMITTED", admitted, {})

        # ---- SOAK 30 s ----
        flaps = 0
        for _ in range(6):
            time.sleep(5)
            if proc_start(newpid) != newstart:
                flaps += 1
        step("SOAK", flaps == 0, {"samples": 6, "flaps": flaps})
        step("COMMIT", True, {"pid": newpid, "base": want_name})
    except RecoverError as e:
        print(f"RECOVERY FAILED: {e}")
        raise SystemExit(f"failed: {e}")
    finally:
        blob = json.dumps(receipts, indent=2)
        (work / "receipts.json").write_text(blob)
        try:
            d.call(f"cat > {STAGE}/recover-music.receipts",
                   data=blob.encode())
        except Exception:
            pass
        d.close()


if __name__ == "__main__":
    main()
