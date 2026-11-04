"""Native detached coordinator tests with synthetic UI/IPC/device fixtures.

No factory process, audio driver, real watchdog or physical device is used.
"""
import ctypes
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

HELPER, FIXTURE = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
del sys.argv[1:3]
LIBC = ctypes.CDLL(None, use_errno=True)
LIBC.semget.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]

ACTIONS = '''#!/bin/sh
set -eu
run=$SB_TEST_COORDINATOR_RUN
helper=$SB_TEST_COORDINATOR_HELPER
plan=$(cat "$run/plan")
printf '%s\\n' "$1" >> "$run/actions.log"
case "$1" in
 preflight)
  test "$plan" != preflight-fail
  if test "$plan" = old-exits; then "$helper" terminate "$2" "$3"; sleep 0.1; fi
  if test "$plan" = slow-preflight; then sleep 0.3; fi
  if test "$plan" = preflight-hang; then trap '' TERM; while :; do sleep 1; done; fi
  ;;
 stop)
  test "$plan" != stop-fail
  if test "$plan" = lying-stop; then exit 0; fi
  "$helper" maintenance-stop "$2" "$3"
  ;;
 start)
  test "$plan" != init-fail
  if test "$plan" = missing-identity; then exit 0; fi
  :
  ;;
 health)
  test "$plan" != recovery-fail
  nonce=$(cat "$run/restart.challenge")
  "$helper" verify-ui "$2" "$3" "$nonce" >/dev/null
  ;;
 recover)
  test "$plan" != recovery-fail
  if test -f "$run/restart.new"; then "$helper" quiesce-new
  else "$helper" recover-old; fi
  ;;
 health-recovery)
  nonce=$(cat "$run/restart.challenge")
  "$helper" verify-ui "$2" "$3" "$nonce" >/dev/null
  ;;
 *) exit 64;;
esac
'''


class Coordinator(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="sb-coordinator-")
        self.root = Path(self.tmp.name)
        self.node = self.root / "fake-watchdog"
        self.node.touch()
        self.pidfile = self.root / "jive.pid"
        self.config = self.root / "watchdog.conf"
        self.feeder = self.root / "watchdog"
        self.jive = self.root / "jive"
        self.actions = self.root / "actions.sh"
        self.actions.write_text(ACTIONS)
        self.actions.chmod(0o700)
        for target in (self.feeder, self.jive):
            shutil.copyfile(FIXTURE, target)
            target.chmod(0o700)
        self.env = dict(os.environ, SB_TEST_PIDFILE=str(self.pidfile), SB_TEST_JIVE=str(self.jive),
                        SB_TEST_WATCHDOG=str(self.feeder), SB_TEST_WATCHDOG_NODE=str(self.node),
                        SB_TEST_WATCHDOG_CONFIG=str(self.config), SB_TEST_COORDINATOR_RUN=str(self.root),
                        SB_TEST_COORDINATOR_ACTIONS=str(self.actions), SB_TEST_COORDINATOR_HELPER=HELPER,
                        SB_TEST_COORDINATOR_TIMEOUT="1500", SB_TEST_BASE_ROOT=str(self.root / "base"))
        self.alsa = self.root / "jive_alsa"
        shutil.copyfile(FIXTURE, self.alsa)
        self.alsa.chmod(0o700)
        self.env["SB_TEST_ALSA"] = str(self.alsa)
        self.config.write_text(f"pidfile = {self.pidfile}\nmin-memory = 0\nsemkey = 1234\n"
                               f"repair-binary = /usr/sbin/repair.sh\nwatchdog-device = {self.node}\n"
                               "admin =\nstartup-delay = 20\ninterval = 5\nlogtick = 0\nrealtime = yes\npriority = 1\n")
        self.assertEqual(LIBC.semget(1234, 0, 0), -1, "private IPC namespace required")
        self.sem = LIBC.semget(1234, 20, 0o600 | 0o1000 | 0o2000)
        self.assertGreaterEqual(self.sem, 0)
        values = (ctypes.c_ushort * 20)(*[1, 0] * 10)
        self.assertEqual(LIBC.semctl(self.sem, 0, 17, ctypes.byref(values)), 0)
        self.env["SB_TEST_UI_SEMID"] = str(self.sem)
        self.started = []
        self.coordinators = []
        self.watchdog = self.start(self.feeder, "feeder", self.node)
        self.player = self.start(self.jive, "ui", self.sem, self.root, "old")
        self.pidfile.write_text(str(self.player.pid) + "\n")
        self.plan("normal")

    def tearDown(self):
        for value in self.coordinators:
            path = Path(f'/proc/{value["pid"]}/stat')
            try:
                if path.exists() and self.start_time(value["pid"]) == value["start"]:
                    os.kill(value["pid"], signal.SIGKILL)
            except (ProcessLookupError, FileNotFoundError):
                pass
        for link in Path("/proc").glob("[0-9]*/exe"):
            try:
                target = os.readlink(link)
                if target.startswith(str(self.root) + "/"):
                    os.kill(int(link.parent.name), signal.SIGKILL)
            except (OSError, ValueError):
                pass
        for p in self.started:
            if p.poll() is None:
                p.kill()
            p.wait(timeout=2)
            p.stdout.close()
        LIBC.semctl(self.sem, 0, 0)
        self.tmp.cleanup()

    @staticmethod
    def start_time(pid):
        return Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[19]

    def start(self, *args):
        p = subprocess.Popen(list(map(str, args)), stdout=subprocess.PIPE, env=self.env)
        self.started.append(p)
        self.assertEqual(p.stdout.readline(), b"ready\n")
        return p

    def plan(self, value):
        (self.root / "plan").write_text(value)
        self.env["SB_TEST_UI_MODE"] = value if value in {"no-health", "wrong-challenge", "frozen", "init-exit"} else "normal"
        self.env.pop("SB_TEST_COORDINATOR_BAD_RECORD", None)
        if value == "missing-identity":
            self.env["SB_TEST_COORDINATOR_BAD_RECORD"] = "1"

    def call(self, *args):
        return subprocess.run([HELPER, *map(str, args)], env=self.env,
                              capture_output=True, text=True, timeout=4)

    def launch(self):
        result = self.call("launch-restart")
        self.assertEqual(result.returncode, 0, result.stderr)
        value = json.loads(result.stdout)
        self.coordinators.append(value)
        return value

    def state(self, *expected, timeout=7):
        until = time.monotonic() + timeout
        seen = []
        while time.monotonic() < until:
            try:
                value = json.loads((self.root / "restart.status").read_text())
                seen.append(value["state"])
                if value["state"] in expected:
                    return value
            except (OSError, json.JSONDecodeError):
                pass
            time.sleep(0.025)
        self.fail(f"Expected {expected}; saw {seen[-20:]}")

    def test_normal_detached_restart_and_pid_handoff(self):
        old = self.player.pid
        self.launch()
        status = self.state("COMPLETED")
        self.assertEqual(status["actor"], "maintenance_coordinator")
        new = int(self.pidfile.read_text())
        self.assertNotEqual(old, new)
        self.assertEqual(self.player.wait(timeout=1), -15)
        self.assertTrue(Path(f"/proc/{new}").exists())
        self.assertFalse((self.root / "restart.challenge").exists())
        self.assertEqual(self.watchdog.poll(), None)
        self.assertEqual(self.node.stat().st_size, 0)

    def test_preflight_refusal_never_changes_watched_jive(self):
        self.plan("preflight-fail")
        self.launch()
        self.state("REFUSED")
        self.assertEqual(self.pidfile.read_text(), str(self.player.pid) + "\n")
        self.assertIsNone(self.player.poll())

    def test_single_coordinator_owns_concurrent_requests(self):
        self.plan("slow-preflight")
        self.launch()
        self.state("PREFLIGHT")
        second = self.call("launch-restart")
        if second.returncode == 0:
            self.coordinators.append(json.loads(second.stdout))
        self.state("COMPLETED")
        actions = (self.root / "actions.log").read_text().splitlines()
        self.assertEqual(actions.count("start"), 1)
        self.assertEqual(actions.count("stop"), 1)

    def test_stale_watched_pid_is_refused(self):
        self.pidfile.write_text(str(os.getpid()) + "\n")
        self.launch()
        self.state("REFUSED")
        self.assertIsNone(self.player.poll())

    def test_feeder_failure_before_handoff_is_refused(self):
        self.watchdog.kill()
        self.watchdog.wait(timeout=1)
        self.launch()
        self.state("REFUSED")
        self.assertIsNone(self.player.poll())
        self.assertEqual(int(self.pidfile.read_text()), self.player.pid)

    def test_old_jive_exits_after_preflight_retains_orphan_decoder_identity(self):
        self.player.kill()
        self.player.wait(timeout=1)
        self.player = self.start(self.jive, "ui", self.sem, self.root, "with-decoder")
        self.pidfile.write_text(str(self.player.pid) + "\n")
        time.sleep(0.05)
        self.plan("old-exits")
        self.launch()
        self.state("RECOVERED")
        values = (ctypes.c_ushort * 20)()
        self.assertEqual(LIBC.semctl(self.sem, 0, 13, ctypes.byref(values)), 0)
        self.assertEqual(sum(values[n] == 0 for n in range(0, 20, 2)), 1,
                         "the old orphan decoder must not retain an IPC allocation")

    def test_stop_failure_with_advancing_original_ui_restores_original(self):
        self.player.kill()
        self.player.wait(timeout=1)
        self.player = self.start(self.jive, "ui", self.sem, self.root, "normal")
        self.pidfile.write_text(str(self.player.pid) + "\n")
        self.plan("stop-fail")
        self.launch()
        self.state("RECOVERED")
        self.assertEqual(int(self.pidfile.read_text()), self.player.pid)
        self.assertIsNone(self.player.poll())

    def test_hung_preflight_is_bounded_before_handoff(self):
        self.plan("preflight-hang")
        started = time.monotonic()
        self.launch()
        self.state("REFUSED")
        self.assertLess(time.monotonic() - started, 3)
        self.assertIsNone(self.player.poll())

    def test_missing_new_identity_recovers_without_qualification(self):
        self.plan("missing-identity")
        self.launch()
        self.state("RECOVERED")
        self.assertNotEqual(int(self.pidfile.read_text()), self.player.pid)

    def test_initialization_failure_recovers_once(self):
        self.plan("init-fail")
        self.launch()
        self.state("RECOVERED")
        actions = (self.root / "actions.log").read_text().splitlines()
        self.assertEqual(actions.count("start"), 1)
        self.assertEqual(actions.count("recover"), 1)

    def test_real_child_initialization_exit_is_captured(self):
        self.plan("init-exit")
        self.launch()
        self.state("RECOVERED")
        evidence = (self.root / "jive.exit").read_text()
        self.assertIn("exited=1 status=23 signal=0", evidence)

    def test_wrong_challenge_is_not_healthy_ui(self):
        self.plan("wrong-challenge")
        self.launch()
        self.state("RECOVERED")
        self.assertNotIn("COMPLETED", (self.root / "restart.status").read_text())

    def test_no_ui_progress_requires_recovery(self):
        self.plan("no-health")
        self.launch()
        self.state("RECOVERED")

    def test_ssh_launcher_hup_does_not_destroy_coordinator(self):
        value = self.launch()
        os.kill(value["pid"], signal.SIGHUP)
        self.state("COMPLETED")

    def test_failed_recovery_preserves_explicit_maintenance_supervision(self):
        self.plan("recovery-fail")
        value = self.launch()
        self.state("FAILED_HELD")
        self.assertEqual(int(self.pidfile.read_text()), value["pid"])
        self.assertTrue(Path(f'/proc/{value["pid"]}').exists())
        self.assertIsNone(self.watchdog.poll())
        # Owner reviews the failure and hands back to this already-live UI;
        # this performs no second restart or playback.
        new_pid, new_start = (self.root / "restart.new").read_text().split()
        result = self.call("pid-swap", value["pid"], value["start"], new_pid, new_start)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.state("OWNER_RECOVERED")


if __name__ == "__main__":
    unittest.main()
