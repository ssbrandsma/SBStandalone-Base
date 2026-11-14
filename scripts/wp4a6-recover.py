#!/usr/bin/env python3
"""WP4a.6 recovery: launch the keepalive music base under supervisor fencing.

Context: the music base (32147) was killed by a cutover dry-run whose LEVEL1
guard used sha256sum, which does not exist on the Touch (busybox has md5sum
only; no python/lua CLI). The guard failed spuriously on a healthy base and
an unguarded rollback path killed it, then failed to relaunch (cmdline was
captured after the failing check). The Jive applet supervisor (15 s tick +
5-60 s backoff, pidfile /tmp/standalonebase/sbbase.pid, cmdline prefix
match) relaunched the wrapper, which fell through to sbbase-core.

This script recovers directly to the NEW (keepalive) music base:
  GUARDS   boot/jive/watchdog/web/proxy identity, snapshot md5, token file,
           music web alive, new artifact sha (host-side)
  STAGE    upload versioned base to /dev tmpfs, add-only link (md5-verified
           round-trip; sha proven host-side before upload)
  SWAP     single fast sequence: TERM core, launch music base, claim pidfile
           (must complete before the supervisor's >=5 s backoff launch)
  LEVEL1   download /proc/<pid>/exe bytes, sha256 on HOST == staged sha
  HEALTH   TinySC STOPPED_VERIFIED, nowplaying idle, snapshot md5 unchanged,
           jive/watchdog starts unchanged, supervisor pidfile stable
  SUPERVISE 45 s pid/tcp stability
  ADMITTED fence re-registers + idle admitted (Jive log)
  SOAK     60 s, 12 samples, 0 flaps
  COMMIT   receipt locally + on device

Rollback (only after SWAP attempted): kill the new base, relaunch the OLD
versioned music binary with its exact cmdline + pidfile claim, LEVEL1-old
(exe bytes vs old stage file, host-side sha) + health. If that fails, stop
killing: the supervisor relaunches sbbase-core by itself (tier-2); verify
core + tinysc and report.
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
PIDFILE = "/tmp/standalonebase/sbbase.pid"
SNAPSHOT = "/media/sda1/.standalone-music/library.0.snapshot"
SNAP_MD5 = "972e35a7d97a053950b8552082166451"

NEW_BIN = REPO / "build-arm/fab4/music-session/sbbase_music"
NEW_SHA = "61e99ac5442727524f7f2cfa06287bc90f388559900d60e24bda94b359c6a9d9"
NEW_WEB_BIN = REPO / "build-arm/fab4/music-session/sbwebserver_music"
# 2026-11-10: previous pin (e05aee5b…) was stale; the binary below was
# built 2026-11-09 22:05 from the current (uncommitted WP4a.5) web source
# (sources dated ≤21:23) and served all 5 WP4a.6 night plays as pid 10903
# without issue. Pin updated to that exact qualified binary.
NEW_WEB_SHA = "482b85db08e9f44bc0768cf7a009d5ecedd4990079782516f14a1ab2d825a662"
OLD_LINK = f"{APPLET}/sbbase-music.05b484fdcb155447"
OLD_STAGE = "/dev/sbmusic-wp4a6.yAr9mx/sbbase-music.05b484fdcb155447"
OLD_STAGE_DIR = "/dev/sbmusic-wp4a6.yAr9mx"


def base_args(token):
    # Fresh token path per incarnation: sb_ob_open uses O_CREAT|O_EXCL and
    # refuses a lingering token file. The fence resolves the live path via
    # /proc scan (findToken), so rotation is the designed flow.
    return ["--config", "/mnt/storage/standalonebase/config.json",
            "--tinysc-arbitration", "--music-root", "/media/sda1",
            "--music-store", ".standalone-music", "--music-db-limit-mib",
            "32", "--music-http-port", "8766", "--ownership-token", token,
            "--music-idle-only"]


class RecoverError(Exception):
    pass


def sha_bytes(data):
    return hashlib.sha256(data).hexdigest()


def md5_bytes(data):
    return hashlib.md5(data).hexdigest()


def main():
    if not NEW_BIN.exists() or not NEW_WEB_BIN.exists():
        raise SystemExit("build fab4 music-session first")
    new_data = NEW_BIN.read_bytes()
    if sha_bytes(new_data) != NEW_SHA:
        raise SystemExit("new base sha mismatch; refusing")
    new_md5 = md5_bytes(new_data)
    new_web_data = NEW_WEB_BIN.read_bytes()
    if sha_bytes(new_web_data) != NEW_WEB_SHA:
        raise SystemExit("new web sha mismatch; refusing")
    new_web_md5 = md5_bytes(new_web_data)
    work = REPO / ".local/wp4a6/cutover"
    work.mkdir(parents=True, exist_ok=True)
    receipts = []
    tag = "".join(random.choice(string.ascii_letters + string.digits)
                  for _ in range(6))
    stage = f"/dev/sbmusic-wp4a6.{tag}"
    new_name = f"sbbase-music.{NEW_SHA[:16]}"
    new_web_name = f"sbwebserver-music.{NEW_WEB_SHA[:16]}"
    d = life.Device(DEVICE)
    swapped = {"done": False}

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

    def slim_pairs():
        out = call("grep -a -c ':0D9B' /proc/net/tcp 2>/dev/null").strip()
        return int(out) if out.isdigit() else -1

    def exe_sha(pid):
        return sha_bytes(call_bytes(f"cat /proc/{pid}/exe 2>/dev/null"))

    def launch_fast(binary, logf, tag, token):
        # Claim the pidfile within seconds (supervisor backoff >= 5 s);
        # API readiness is awaited only after the claim.
        args = " ".join(f"'{a}'" for a in base_args(token))
        call(f": > {logf}")
        call(f"setsid {binary} {args} >>{logf} 2>&1 </dev/null & echo bg")
        pid = None
        deadline = time.time() + 10
        while time.time() < deadline:
            for line in call(f"ps -o pid,args | grep '{tag}' | "
                             "grep -v grep").strip().split("\n"):
                if line.strip():
                    pid = line.split()[0]
                    break
            if pid:
                break
            time.sleep(0.5)
        if pid:
            call(f"echo {pid} > {PIDFILE}")
        return pid

    def wait_api(pid, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            ts = wget("/api/tinysc/lifecycle")
            if ts and str(ts.get("pid")) == str(pid):
                return ts
            time.sleep(2)
        return None

    def bridge_snapshot(token):
        # Read-only SBOWN1 hello over SSH direct-tcpip (:9000 is
        # loopback-only; busybox wget cannot POST). Returns the hello
        # JSON dict or None.
        try:
            t = d.client.get_transport()
            ch = t.open_channel("direct-tcpip", ("127.0.0.1", 9000),
                                ("127.0.0.1", 0), timeout=10)
            ch.settimeout(15)
            body = "SBOWN1 hello 4 0 1 0 0 0 0 0 0 0"
            ch.sendall((f"POST /api/music/ownership HTTP/1.1\r\n"
                        f"Host: 127.0.0.1:9000\r\n"
                        f"X-Music-Ownership: {token}\r\n"
                        f"Content-Length: {len(body)}\r\n"
                        f"Connection: close\r\n\r\n{body}").encode())
            data = b""
            while True:
                try:
                    chunk = ch.recv(4096)
                except Exception:
                    break
                if not chunk:
                    break
                data += chunk
            ch.close()
            text = data.decode(errors="replace")
            if "\r\n\r\n" not in text:
                return None
            return json.loads(text.split("\r\n\r\n", 1)[1])
        except Exception:
            return None

    old = {}
    try:
        # ---- GUARDS (read-only; abort here never rolls back) ----
        tools = call("for t in md5sum wget ps setsid strings cut grep; do "
                     "command -v $t >/dev/null 2>&1 && echo -n \"y\" || "
                     "echo -n \"N$t \"; done; echo")
        if "N" in tools:
            step("GUARDS", False, f"missing tools: {tools.strip()}")
        # PIDs are discovered, never fixed: they change every boot.
        # Boot id is captured here and re-checked at HEALTH (a reboot
        # mid-run aborts); it is not compared to a stale constant.
        old["boot"] = call(
            "cat /proc/sys/kernel/random/boot_id").strip()
        if not old["boot"]:
            step("GUARDS", False, "boot id unreadable")
        for comm, name in (("jive", "jive"),
                             ("watchdog", "watchdog"),
                             ("sbwebserver", "web"),
                             ("sbproxy", "proxy")):
            found = [l.split()[0] for l in
                     call(f"ps -o pid,comm | grep -E ' {comm}$' "
                          f"| grep -v grep").strip().split("\n")
                     if l.strip()]
            # Exclude kernel threads (e.g. watchdog/0 has no /proc/pid/exe
            # match; comm is exact so watchdog/0 never matches ^watchdog$).
            found = [p for p in found if proc_start(p)]
            if len(found) != 1:
                step("GUARDS", False, f"{name}: procs {found}")
            old[name] = {"pid": found[0],
                         "start": proc_start(found[0])}
        if call(f"md5sum {SNAPSHOT}").split()[0] != SNAP_MD5:
            step("GUARDS", False, "snapshot md5 drift")
        # Tier-1 rollback needs the old music artifacts; after a reboot
        # they are wiped (/dev tmpfs) and only tier-2 (supervisor core)
        # remains. Record, do not require.
        # Applet links vanish spontaneously (the 12:52 link was gone
        # within the hour while older ones persist); the stage file is
        # the durable tier-1 artifact, the link is recreated at use.
        old["tier1"] = call(
            f"test -x {OLD_STAGE} && echo yes").strip() == "yes"
        web = call("ps | grep sbwebserver-mus | grep -v grep").split()
        old["web_pid"] = web[0] if web else None
        old["web_cmdline"] = None
        if old["web_pid"]:
            old["web_cmdline"] = call(
                f"strings /proc/{old['web_pid']}/cmdline").strip().split(
                "\n")
            if not old["web_cmdline"] or not old["web_cmdline"][0]:
                step("GUARDS", False, "web cmdline unreadable")
        core = call("ps | grep sbbase-core | grep -v grep").split()
        old["core_pid"] = core[0] if core else None
        # Prestate: either supervisor core (post-crash) or a live music
        # base (planned cutover). A live music base must be LEVEL1-old
        # (host-side exe proof) before it may be replaced.
        mus = call("ps -o pid,args | grep 'sbbase-music\\.'"
                   " | grep -v grep").strip().split("\n")
        mus = [l.split() for l in mus if l.strip()]
        if len(mus) > 1:
            step("GUARDS", False, f"duplicate music base: {mus}")
        old["music_pid"] = mus[0][0] if mus else None
        if old["music_pid"]:
            if old["core_pid"]:
                step("GUARDS", False, "core + music both up")
            argv0 = mus[0][1]
            # Known-good generations: the WP4a.5 base, the keepalive
            # base, or the new base itself (re-run). The running bytes
            # must equal the staged artifact they launched from.
            known = ("da86f37417996f1d", "7759936ef1bad7e1",
                     "21d7521bb053302f", "31451815c8ba38d2",
                     "c8e2c8eec733fb53", "80dd2b7e936da7cd",
                     "1aa6c3d9bb879aae", "dfc8b91c6865e957",
                     "26b66b6a4126b43b", "928d5ea7c2803847",
                     "ee5e0dddc532acbf",
                     "640ce9e143510390",
                     "3f4ebffc88e2db73",
                     "610ebaac0537df40",
                     "bd7d46f52f243a8d",
                     "f290ddf21cda2f40",
                     "4ce8779ee3ad2bb9",
                     "511790061016ee05",
                     "05b484fdcb155447",
                     NEW_SHA[:16])
            if not any(k in argv0 for k in known):
                step("GUARDS", False,
                     f"unknown live base: {argv0[:80]}")
            live_tag = argv0.rsplit(
                "sbbase-music.", 1)[-1][:16] \
                if "sbbase-music." in argv0 else ""
            if NEW_SHA[:16] in argv0:
                want = NEW_SHA
            elif live_tag and live_tag in OLD_LINK:
                # Applet link may have vanished; prove the live exe
                # against the durable stage file instead.
                want = sha_bytes(call_bytes(f"cat '{OLD_STAGE}'"))
            else:
                want = sha_bytes(call_bytes(f"cat '{argv0}'"))
            if not want or exe_sha(old["music_pid"]) != want:
                step("GUARDS", False,
                     f"live music base not LEVEL1: {argv0[:60]}")
            old["music_start"] = proc_start(old["music_pid"])
            old["music_argv0"] = argv0
        elif not old["core_pid"]:
            step("GUARDS", False, "neither core nor music base up")
        step("GUARDS", True, {"core": old["core_pid"],
                              "music": old["music_pid"],
                              "web": old["web_pid"],
                              "tier1": old["tier1"],
                              "boot": old["boot"][:8]})
        (work / "recover-guards.json").write_text(json.dumps(old, indent=2))

        # ---- STAGE ----
        call(f"mkdir -p {stage} && chmod 700 {stage}")
        call(f"mkdir -p {stage}/owner && chmod 700 {stage}/owner")
        call(f"mkdir -p {stage}/web-config && chmod 700 {stage}/web-config")
        new_token = f"{stage}/owner/token"
        d.call(f"cat > {stage}/{new_name}", data=new_data)
        call(f"chmod 755 {stage}/{new_name}")
        if call(f"md5sum {stage}/{new_name}").split()[0] != new_md5:
            raise SystemExit("stage round-trip md5 mismatch")
        d.call(f"cat > {stage}/{new_web_name}", data=new_web_data)
        call(f"chmod 755 {stage}/{new_web_name}")
        if call(f"md5sum {stage}/{new_web_name}").split()[0] != new_web_md5:
            raise SystemExit("stage web round-trip md5 mismatch")
        # -f: a reboot leaves a dangling link of the same name (its
        # /dev target was wiped); replacement is add-only in effect.
        call(f"ln -sfn {stage}/{new_name} {APPLET}/{new_name}")
        # No readlink on the Touch: prove the link resolves to our bytes.
        got = call(f"md5sum {APPLET}/{new_name}").split()
        if not got or got[0] != new_md5:
            raise SystemExit("stage link failed")
        step("STAGE", True, {"stage": stage, "sha": NEW_SHA[:16],
                             "web": NEW_WEB_SHA[:16]})

        # ---- SWAP (fast: kill core+web, launch web+base, claim pidfile)
        # The web's rendezvous accept is single-shot: the stale web can
        # never adopt a fresh base, so both relaunch (CUTOVER-02). Web
        # first, base immediately after (base retries 12 s); pidfile
        # claimed within seconds (supervisor backoff >= 5 s).
        def stop(pid, name):
            if not pid or proc_start(pid) is None:
                return
            call(f"kill -TERM {pid}")
            deadline = time.time() + 10
            while time.time() < deadline:
                if proc_start(pid) is None:
                    return
                time.sleep(0.5)
            step("SWAP", False, f"{name} ignored TERM")

        stop(old["core_pid"], "core")
        stop(old["music_pid"], "old music base")
        stop(old["web_pid"], "music web")
        # Fresh staged web (the pre-reboot web binary is wiped); config
        # dir is a fresh isolate like the original staging.
        wargv = (f"'{stage}/{new_web_name}' --port 8766 --web-root "
                 f"'{APPLET}' --config-dir '{stage}/web-config'")
        wlog = f"{stage}/web.log"
        call(f": > {wlog}")
        call(f"setsid {wargv} >>{wlog} 2>&1 </dev/null & echo bg")
        newpid = launch_fast(f"{APPLET}/{new_name}", f"{stage}/sbbase.log",
                             new_name, new_token)
        swapped["done"] = True
        if not newpid:
            step("SWAP", False, "music base never appeared")
        if not wait_api(newpid):
            blog = call(f"tail -c 300 {stage}/sbbase.log")
            step("SWAP", False, f"music base API down; log: {blog[:150]}")
        newstart = proc_start(newpid)
        # Identify the fresh web (old pid dead, exactly one mus web left).
        webs = [l.split()[0] for l in
                call("ps -o pid,args | grep sbwebserver-music | "
                     "grep -v grep").strip().split("\n") if l.strip()]
        if len(webs) != 1:
            step("SWAP", False, f"web count {len(webs)}")
        step("SWAP", True, {"pid": newpid, "start": newstart,
                            "web": webs[0]})

        # ---- LEVEL1 (exe bytes hashed on host) ----
        if exe_sha(newpid) != NEW_SHA:
            step("LEVEL1", False, "exe bytes != staged artifact")
        step("LEVEL1", True, {"pid": newpid, "sha": NEW_SHA[:16]})

        # ---- HEALTH ----
        ts = wget("/api/tinysc/lifecycle")
        if not ts or ts.get("state") != "STOPPED_VERIFIED" or \
                str(ts.get("pid")) != newpid:
            step("HEALTH", False, f"tinysc {str(ts)[:150]}")
        nq = wget("/api/nowplaying")
        # Idle or radio-playing are both healthy; music-active is not
        # expected outside a test and aborts the maintenance.
        if nq and nq.get("source") == "music" and nq.get("active"):
            step("HEALTH", False, f"music active: {str(nq)[:150]}")
        if call(f"md5sum {SNAPSHOT}").split()[0] != SNAP_MD5:
            step("HEALTH", False, "snapshot changed")
        if call("cat /proc/sys/kernel/random/boot_id").strip() != \
                old["boot"]:
            step("HEALTH", False, "reboot mid-run")
        if proc_start(old["jive"]["pid"]) != old["jive"]["start"] or \
                proc_start(old["watchdog"]["pid"]) != \
                old["watchdog"]["start"]:
            step("HEALTH", False, "jive/watchdog drift")
        if call(f"cat {PIDFILE} 2>/dev/null").strip() != newpid:
            step("HEALTH", False, "supervisor pidfile lost")
        if ":223E" not in call("grep -a ' 0A ' /proc/net/tcp"):
            step("HEALTH", False, ":8766 not bound")
        if "rendezvous unavailable" in call(f"cat {wlog}"):
            step("HEALTH", False, "web missed rendezvous")
        step("HEALTH", True, {"pid": newpid})

        # ---- SUPERVISE 45 s ----
        # Jive's SlimProto reconnect churns TIME_WAIT + fresh connections,
        # so exact pair-count stability is wrong here. Flap = pid change,
        # SlimProto fully gone, or TinySC stale.
        def settled():
            return slim_pairs() >= 1

        deadline = time.time() + 30
        while time.time() < deadline and not settled():
            time.sleep(2)
        if not settled():
            step("SUPERVISE", False, "slimproto never reconnected")
        ok45, pairs_seen = True, []
        for _ in range(9):
            time.sleep(5)
            pairs_seen.append(slim_pairs())
            ts = wget("/api/tinysc/lifecycle")
            if proc_start(newpid) != newstart or slim_pairs() < 1 or \
                    not ts or not ts.get("fresh"):
                ok45 = False
                break
        if call(f"cat {PIDFILE} 2>/dev/null").strip() != newpid:
            step("SUPERVISE", False, "pidfile stolen by supervisor race")
        step("SUPERVISE", ok45, {"pairs": pairs_seen})

        # ---- ADMITTED (fence rejoins the fresh bridge) ----
        # Radio playing is the legitimate hold-case: the fence must NOT
        # register (incarnation stays 0). Tick liveness then re-verifies
        # at Test1 (radio stop -> rejoin + admit).
        nq = wget("/api/nowplaying")
        radio_playing = bool(nq and nq.get("source") == "radio" and
                             nq.get("state") == "playing")
        # Fresh VM (post-reboot): no fence installed yet, so incarnation
        # stays 0 with idle nowplaying. Confirm by the absence of any
        # fence line in the Jive log; the fence install is a separate
        # verified step that re-checks admission.
        fence_lines = call("grep -a -h -c 'PlaybackFence' "
                               "/var/log/messages /var/log/messages.0 "
                               "2>/dev/null").strip().split()
        fresh_vm = all(x == "0" for x in fence_lines) and \
            len(fence_lines) > 0
        token = call(f"cat {new_token}").strip()
        adm, snap = False, None
        deadline = time.time() + 90
        while time.time() < deadline:
            snap = bridge_snapshot(token)
            if not snap:
                time.sleep(5)
                continue
            if radio_playing:
                if snap.get("incarnation") == "0":
                    adm = True
                    break
            elif snap.get("incarnation") != "0" and \
                    snap.get("idle_admitted") is True:
                adm = True
                break
            elif fresh_vm and snap.get("incarnation") == "0" and \
                    nq and not nq.get("active"):
                adm = True
                break
            time.sleep(5)
        detail = {"snapshot": snap}
        if radio_playing:
            detail["held_for_radio"] = True
        if fresh_vm and snap and snap.get("incarnation") == "0":
            detail["fresh_vm_fence_pending"] = True
        step("ADMITTED", adm, detail)

        # ---- SOAK 60 s ----
        flaps = 0
        for _ in range(12):
            time.sleep(5)
            ts = wget("/api/tinysc/lifecycle")
            if proc_start(newpid) != newstart or slim_pairs() < 1 or \
                    not ts or not ts.get("fresh"):
                flaps += 1
        step("SOAK", flaps == 0, {"samples": 12, "flaps": flaps})
        step("COMMIT", True, {"pid": newpid, "base": new_name})
    except RecoverError as e:
        print(f"RECOVERY FAILED: {e}", flush=True)
        if swapped["done"]:
            try:
                if not old.get("tier1") or not old.get("web_cmdline"):
                    raise RecoverError(
                        "RB-SKIP: no tier-1 artifacts (wiped); tier-2")
                print("rolling back to OLD music base", flush=True)
                # Kill intruders (base + web), relaunch old web + old base.
                cur = call("ps | grep -E 'sbbase-music|sbbase-core|"
                           "sbwebserver-mus' | grep -v grep").strip()
                for line in cur.split("\n"):
                    if line.strip():
                        call(f"kill -KILL {line.split()[0]}")
                time.sleep(3)
                wargv = " ".join(f"'{a}'" for a in old["web_cmdline"]
                                 if a)
                call(f"setsid {wargv} "
                     f">>{OLD_STAGE_DIR}/web.log 2>&1 "
                     f"</dev/null & echo bg")
                rb_owner = f"{OLD_STAGE_DIR}/owner-rb"
                call(f"mkdir -p {rb_owner} && chmod 700 {rb_owner} && "
                     f"rm -f {rb_owner}/token")
                call(f"ln -sfn {OLD_STAGE} {OLD_LINK}")
                rpid = launch_fast(
                    OLD_LINK, f"{OLD_STAGE_DIR}/sbbase.log",
                    "sbbase-music.05b484fdcb155447",
                    f"{rb_owner}/token")
                if not rpid or not wait_api(rpid):
                    blog = call("tail -c 200 "
                                f"{OLD_STAGE_DIR}/sbbase.log")
                    raise RecoverError(
                        f"RB-RELAUNCH: old base API down; {blog[:120]}")
                old_sha = sha_bytes(call_bytes(f"cat {OLD_STAGE}"))
                if exe_sha(rpid) != old_sha:
                    raise RecoverError("RB-LEVEL1: exe != old artifact")
                ts = wget("/api/tinysc/lifecycle")
                if not ts or ts.get("state") != "STOPPED_VERIFIED":
                    raise RecoverError(f"RB-HEALTH: tinysc {ts}")
                receipts.append({"step": "ROLLED_BACK", "ok": True,
                                 "t": datetime.now(
                                     timezone.utc).isoformat(),
                                 "detail": {"pid": rpid}})
                print(f"rolled back to old base {rpid}", flush=True)
            except RecoverError as e2:
                receipts.append({"step": "ROLLBACK_FAILED", "ok": False,
                                 "t": datetime.now(
                                     timezone.utc).isoformat(),
                                 "detail": {"reason": str(e2)}})
                print(f"TIER-2: leaving supervisor to heal ({e2})",
                      flush=True)
                time.sleep(45)  # supervisor tick + backoff relaunches core
                ts = wget("/api/tinysc/lifecycle")
                print(f"tier-2 state: {str(ts)[:200]}", flush=True)
        else:
            print("pre-swap failure: nothing changed, no rollback",
                  flush=True)
        raise SystemExit(f"failed: {e}")
    finally:
        blob = json.dumps(receipts, indent=2)
        (work / "recover-receipts.json").write_text(blob)
        try:
            d.call(f"cat > {stage}/recover.receipts", data=blob.encode())
        except Exception:
            pass
        d.close()


if __name__ == "__main__":
    main()
