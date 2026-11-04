"""Linux-only isolated lifecycle fixtures; no firmware, USB or watchdog access."""
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

CONTROL, FIXTURE = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
del sys.argv[1:3]


class Lifecycle(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="sb-ts-")
        self.root = Path(self.tmp.name)
        for name in ("run", "pid", "tiny"):
            (self.root / name).mkdir(mode=0o700)
        self.env = dict(os.environ, SB_TS_TEST_RUN=str(self.root / "run"),
                        SB_TS_TEST_PID_DIR=str(self.root / "pid"),
                        SB_TS_TEST_ROOT=str(self.root / "tiny"),
                        SB_TS_TEST_POLICY=str(self.root / "enabled"),
                        SB_TS_TEST_SERVICE=str(self.root / "stock"),
                        SB_TS_TEST_SHELL=str(Path("/bin/sh").resolve()),
                        SB_TS_TEST_CONTROL=CONTROL)
        self.processes = []
        self.stock('printf "%s\\n" "$1" >> "' + str(self.root / "actions") + '"\n'
                   'case "$1" in\n'
                   ' stopserver) f=squeezecenter.pid;;\n'
                   ' stopscan) f=scanner.pid;;\n'
                   ' stopresizer) f=gdresized.pid;;\n'
                   ' *) exit 0;;\nesac\n'
                   'kill "$(cat "$SB_TS_TEST_PID_DIR/$f")"\n')
        for name in ("slimserver.pl", "scanner.pl", "gdresized.pl"):
            shutil.copyfile(FIXTURE, self.root / "tiny" / name)
            (self.root / "tiny" / name).chmod(0o700)

    def stock(self, body):
        (self.root / "stock").write_text("#!/bin/sh\n" + body)
        (self.root / "stock").chmod(0o700)

    def tearDown(self):
        for p in reversed(self.processes):
            if p.poll() is None:
                p.kill()
            p.wait(timeout=3)
            for f in (p.stdin, p.stdout, p.stderr):
                if f:
                    f.close()
        self.tmp.cleanup()

    def enable(self):
        (self.root / "enabled").touch(mode=0o600)

    def start(self, role="slimserver.pl", *args):
        p = subprocess.Popen([str(self.root / "tiny" / role), *args],
                             stdout=subprocess.PIPE, env=self.env)
        self.processes.append(p)
        time.sleep(0.04)
        return p

    def worker(self, **overrides):
        p = subprocess.Popen([FIXTURE, "worker"], env=dict(self.env, **overrides),
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
        self.processes.append(p)
        return p

    def state(self, p, expected, timeout=3):
        deadline = time.monotonic() + timeout
        seen = []
        while time.monotonic() < deadline:
            if select.select([p.stdout], [], [], 0.1)[0]:
                line = p.stdout.readline()
                if not line:
                    break
                status = json.loads(line)
                seen.append(status["state"])
                if status["state"] == expected:
                    return status
        self.fail(f"Expected {expected}; saw {seen}; returncode={p.poll()}")

    def call(self, *args):
        return subprocess.run([CONTROL, *args], env=self.env,
                              capture_output=True, text=True, timeout=3)

    def test_disabled_stock_actions_are_unchanged(self):
        self.stock('printf "%s\\n" "$1" >> "' + str(self.root / "actions") + '"\n')
        for action in ("start", "stop", "restart", "rescan", "stopscan"):
            self.assertEqual(self.call("service", action).returncode, 0)
        self.assertEqual((self.root / "actions").read_text().splitlines(),
                         ["start", "stop", "restart", "rescan", "stopscan"])
        self.assertFalse((self.root / "run" / "tinysc.latch").exists())

    def test_no_tinysc_and_guarded_starts(self):
        self.enable()
        p = self.worker()
        status = self.state(p, "STOPPED_VERIFIED")
        self.assertFalse(status["music_admission"])
        p.stdin.write(b"o")
        p.stdin.flush()
        self.assertTrue(self.state(p, "STOPPED_VERIFIED")["music_admission"])
        for action in ("start", "restart", "rescan", "startresizer"):
            self.assertEqual(self.call("service", action).returncode, 77)
        self.assertFalse((self.root / "actions").exists())

    def test_all_three_services_stop(self):
        self.enable()
        daemons = [self.start(role) for role in ("slimserver.pl", "scanner.pl", "gdresized.pl")]
        self.state(self.worker(), "STOPPED_VERIFIED")
        for daemon in daemons:
            self.assertEqual(daemon.wait(timeout=1), -15)
        self.assertEqual((self.root / "actions").read_text().splitlines(),
                         ["stopscan", "stopserver", "stopresizer"])

    def test_stop_timeout_is_fail_closed(self):
        self.enable()
        self.start("slimserver.pl", "ignore")
        status = self.state(self.worker(), "FAILED")
        self.assertFalse(status["music_admission"])
        self.assertIn("deadline", status["error"])

    def test_stale_pid_is_reconciled(self):
        self.enable()
        daemon = self.start()
        (self.root / "pid" / "squeezecenter.pid").write_text("2147483647\n")
        self.state(self.worker(), "STOPPED_VERIFIED")
        self.assertEqual(daemon.wait(timeout=1), -15)

    def test_unrelated_live_pid_is_not_signalled(self):
        self.enable()
        self.start()
        (self.root / "pid" / "squeezecenter.pid").write_text(str(os.getpid()) + "\n")
        status = self.state(self.worker(), "FAILED")
        self.assertIn("stop request failed", status["error"])
        self.assertFalse((self.root / "actions").exists())

    def test_base_crash_leaves_quarantine(self):
        self.enable()
        p = self.worker()
        self.state(p, "STOPPED_VERIFIED")
        p.kill()
        p.wait(timeout=1)
        self.assertEqual(self.call("service", "start").returncode, 77)
        self.state(self.worker(), "STOPPED_VERIFIED")

    def test_live_base_duplicate_cannot_take_lease(self):
        self.enable()
        self.state(self.worker(), "STOPPED_VERIFIED")
        status = self.state(self.worker(), "FAILED")
        self.assertIn("lease", status["error"])

    def test_appearing_tinysc_revokes_admission(self):
        self.enable()
        p = self.worker()
        self.state(p, "STOPPED_VERIFIED")
        p.stdin.write(b"o")
        p.stdin.flush()
        self.assertTrue(self.state(p, "STOPPED_VERIFIED")["music_admission"])
        self.start()
        status = self.state(p, "STOPPING")
        self.assertFalse(status["music_admission"])
        self.assertFalse(self.state(p, "STOPPED_VERIFIED")["music_admission"])

    def test_hung_firmware_request_has_bounded_shutdown(self):
        self.enable()
        self.start()
        self.stock('echo $$ > "$SB_TS_TEST_RUN/hung.pid"\n'
                   "trap '' TERM\nwhile :; do sleep 1; done\n")
        p = self.worker()
        status = self.state(p, "FAILED")
        self.assertIn("deadline", status["error"])
        start = time.monotonic()
        p.stdin.write(b"q")
        p.stdin.flush()
        self.assertEqual(p.wait(timeout=2), 0)
        self.assertLess(time.monotonic() - start, 1)
        # The request leader may exit first; its TERM-ignoring firmware child
        # must not survive and keep CTest's output descriptors open.
        hung = int((self.root / "run" / "hung.pid").read_text())
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            path = Path(f"/proc/{hung}/stat")
            if not path.exists() or path.read_text().rsplit(")", 1)[1].split()[0] == "Z":
                break
            time.sleep(0.01)
        else:
            self.fail("firmware stop child survived bounded request shutdown")

    def test_surviving_orphan_prevents_verified_absence(self):
        self.enable()
        parent = self.start("slimserver.pl", "child", FIXTURE)
        child = int(parent.stdout.readline())
        try:
            worker = self.worker()
            status = self.state(worker, "FAILED")
            self.assertFalse(status["music_admission"])
            self.assertEqual(status["count"], 1)
            self.assertIn("deadline", status["error"])
            worker.kill()
            worker.wait(timeout=1)
            status = self.state(self.worker(), "FAILED")
            self.assertEqual(status["count"], 1)
            self.assertFalse(status["music_admission"])
        finally:
            os.kill(child, 9)

    def test_corrupt_prior_identities_fail_closed(self):
        self.enable()
        (self.root / "run" / "tinysc.identities").write_text("not an identity journal\n")
        (self.root / "run" / "tinysc.identities").chmod(0o600)
        status = self.state(self.worker(), "FAILED")
        self.assertIn("reconcile", status["error"])

    def test_release_requires_disabled_policy_and_no_base_lease(self):
        self.enable()
        p = self.worker()
        self.state(p, "STOPPED_VERIFIED")
        self.assertEqual(self.call("release").returncode, 77)
        (self.root / "enabled").unlink()
        self.assertEqual(self.call("release").returncode, 77)
        p.stdin.write(b"q")
        p.stdin.flush()
        p.wait(timeout=1)
        self.assertEqual(self.call("service", "start").returncode, 77)
        self.assertEqual(self.call("release").returncode, 0)
        self.assertEqual(self.call("service", "start").returncode, 0)

    def test_release_refuses_live_music_runtime(self):
        self.enable()
        p = self.worker()
        self.state(p, "STOPPED_VERIFIED")
        p.kill()
        p.wait(timeout=1)
        (self.root / "enabled").unlink()
        music = self.root / "sbbase_music"
        shutil.copyfile(FIXTURE, music)
        music.chmod(0o700)
        daemon = subprocess.Popen([str(music), "unrelated"], env=self.env)
        self.processes.append(daemon)
        time.sleep(0.04)
        self.assertEqual(self.call("release").returncode, 77)
        self.assertTrue((self.root / "run" / "tinysc.latch").exists())

    def test_stop_request_cannot_inherit_high_parent_descriptors(self):
        self.enable()
        self.start()
        control = self.root / "inspect-fds"
        result = self.root / "descriptor-result"
        control.write_text('#!/bin/sh\nif test -e /proc/$$/fd/4096; then '
                           f'echo leaked > "{result}"; else echo closed > "{result}"; fi\nexit 1\n')
        control.chmod(0o700)
        fd = os.open(self.root / "foreign-fd", os.O_CREAT | os.O_RDWR, 0o600)
        high = os.dup2(fd, 4096, inheritable=True)
        try:
            p = subprocess.Popen([FIXTURE, "worker"],
                                 env=dict(self.env, SB_TS_TEST_CONTROL=str(control)),
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 pass_fds=(high,), bufsize=0)
            self.processes.append(p)
            self.state(p, "FAILED")
            self.assertEqual(result.read_text().strip(), "closed")
        finally:
            os.close(high)
            os.close(fd)

    def test_pending_nice_launcher_is_stopped_before_script_exec(self):
        self.enable()
        nice = self.root / "nice"
        shutil.copyfile(FIXTURE, nice)
        nice.chmod(0o700)
        self.env["SB_TS_TEST_NICE"] = str(nice)
        p = subprocess.Popen([str(nice), "-n", "5", str(self.root / "tiny/slimserver.pl")], env=self.env)
        self.processes.append(p)
        time.sleep(0.04)
        self.state(self.worker(), "STOPPED_VERIFIED")
        self.assertEqual(p.wait(timeout=1), -15)

    def test_unrelated_nice_argument_is_not_tinysc(self):
        nice = self.root / "nice"
        shutil.copyfile(FIXTURE, nice)
        nice.chmod(0o700)
        self.env["SB_TS_TEST_NICE"] = str(nice)
        p = subprocess.Popen([str(nice), "-n", "5", "/bin/echo", str(self.root / "tiny/slimserver.pl")], env=self.env)
        self.processes.append(p)
        time.sleep(0.04)
        result = self.call("detect")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "count=0 uncertain=0\n")
        self.assertIsNone(p.poll())

    def test_late_stock_restart_callback_is_suppressed(self):
        self.enable()
        self.stock('case "$1" in\n'
                   ' restart) (sleep 0.25; if "$SB_TS_TEST_CONTROL" service start; '
                   'then echo started; else echo suppressed; fi > "$SB_TS_TEST_RUN/late-result") & ;;\n'
                   ' start) echo bad > "$SB_TS_TEST_RUN/started" ;;\n'
                   'esac\nexit 0\n')
        request = subprocess.Popen([CONTROL, "service", "restart"], env=self.env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.processes.append(request)
        self.assertEqual(request.wait(timeout=1), 0)
        self.state(self.worker(), "STOPPED_VERIFIED")
        self.assertEqual((self.root / "run/late-result").read_text().strip(), "suppressed")
        self.assertFalse((self.root / "run/started").exists())


if __name__ == "__main__":
    unittest.main()
