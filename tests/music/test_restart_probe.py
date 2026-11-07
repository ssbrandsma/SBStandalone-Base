"""Read-only native watchdog probing in a private IPC namespace and fake node."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HELPER, FIXTURE = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
del sys.argv[1:3]
LIBC = ctypes.CDLL(None, use_errno=True)
LIBC.semget.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]


class Probe(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="sb-restart-probe-")
        self.root = Path(self.tmp.name)
        self.node = self.root / "fake-watchdog"
        self.node.touch()
        self.pidfile = self.root / "jive.pid"
        self.config = self.root / "watchdog.conf"
        self.feeder = self.root / "watchdog"
        self.jive = self.root / "jive"
        for target in (self.feeder, self.jive):
            shutil.copyfile(FIXTURE, target)
            target.chmod(0o700)
        self.env = dict(os.environ, SB_TEST_PIDFILE=str(self.pidfile),
                        SB_TEST_JIVE=str(self.jive), SB_TEST_WATCHDOG=str(self.feeder),
                        SB_TEST_WATCHDOG_NODE=str(self.node), SB_TEST_WATCHDOG_CONFIG=str(self.config))
        self.config.write_text(f"pidfile = {self.pidfile}\nmin-memory = 0\nsemkey = 1234\n"
                               f"repair-binary = /usr/sbin/repair.sh\nwatchdog-device = {self.node}\n"
                               "admin =\nstartup-delay = 20\ninterval = 5\nlogtick = 0\nrealtime = yes\npriority = 1\n")
        self.assertEqual(LIBC.semget(1234, 0, 0), -1, "private IPC namespace required")
        self.sem = LIBC.semget(1234, 20, 0o600 | 0o1000 | 0o2000)
        self.assertGreaterEqual(self.sem, 0)
        values = (ctypes.c_ushort * 20)(*[1, 0] * 10)
        self.assertEqual(LIBC.semctl(self.sem, 0, 17, ctypes.byref(values)), 0)
        self.started = []
        self.watchdog = self.start(self.feeder, "feeder", self.node)
        self.player = self.start(self.jive, "jive", self.sem, 3)
        self.pidfile.write_text(str(self.player.pid) + "\n")

    def start(self, *args):
        p = subprocess.Popen(list(map(str, args)), stdout=subprocess.PIPE)
        self.started.append(p)
        self.assertEqual(p.stdout.readline(), b"ready\n")
        return p

    def tearDown(self):
        for p in self.started:
            if p.poll() is None:
                p.kill()
            p.wait(timeout=2)
            p.stdout.close()
        LIBC.semctl(self.sem, 0, 0)
        self.tmp.cleanup()

    def probe(self):
        return subprocess.run([HELPER, "restart-preflight"], env=self.env,
                              capture_output=True, text=True, timeout=3)

    def test_single_feeder_and_live_ui_proof_without_ipc_changes(self):
        before = (ctypes.c_ushort * 20)()
        after = (ctypes.c_ushort * 20)()
        self.assertEqual(LIBC.semctl(self.sem, 0, 13, ctypes.byref(before)), 0)
        result = self.probe()
        self.assertEqual(result.returncode, 0, result.stderr)
        status = json.loads(result.stdout)
        self.assertTrue(status["read_only"])
        self.assertEqual(status["jive_registrations"], 2)
        self.assertEqual(status["feeder_pid"], self.watchdog.pid)
        self.assertEqual(status["jive_pid"], self.player.pid)
        self.assertEqual(LIBC.semctl(self.sem, 0, 13, ctypes.byref(after)), 0)
        self.assertEqual(list(before), list(after))
        self.assertEqual(self.pidfile.read_text(), str(self.player.pid) + "\n")
        self.assertEqual(self.node.stat().st_size, 0)

    def test_multiple_feeders_are_refused(self):
        self.start(self.feeder, "feeder", self.node)
        self.assertEqual(self.probe().returncode, 2)

    def test_missing_feeder_is_refused(self):
        self.watchdog.kill()
        self.watchdog.wait(timeout=1)
        self.assertEqual(self.probe().returncode, 2)

    def test_wrong_watched_process_is_refused(self):
        self.pidfile.write_text(str(os.getpid()) + "\n")
        self.assertEqual(self.probe().returncode, 2)

    def test_readonly_descriptor_is_not_a_feeder(self):
        self.watchdog.kill()
        self.watchdog.wait(timeout=1)
        self.start(self.feeder, "reader", self.node)
        self.assertEqual(self.probe().returncode, 2)

    def test_writable_watched_pidfile_is_refused(self):
        self.pidfile.chmod(0o666)
        self.assertEqual(self.probe().returncode, 2)

    def test_expired_ui_countdown_is_refused(self):
        self.assertEqual(LIBC.semctl(self.sem, 1, 16, 0), 0)
        self.assertEqual(self.probe().returncode, 2)

    def test_second_expired_countdown_is_refused(self):
        self.assertEqual(LIBC.semctl(self.sem, 3, 16, 0), 0)
        self.assertEqual(self.probe().returncode, 2)

    def test_one_registration_is_not_the_fab4_profile(self):
        self.assertEqual(LIBC.semctl(self.sem, 2, 16, 1), 0)
        self.assertEqual(self.probe().returncode, 2)

    def test_three_registrations_are_refused(self):
        player = self.start(self.jive, "jive", self.sem, 3, 3)
        self.pidfile.write_text(str(player.pid) + "\n")
        self.assertEqual(self.probe().returncode, 2)

    def test_countdown_last_writer_is_not_registration_owner(self):
        for slot in (1, 3):
            self.assertEqual(LIBC.semctl(self.sem, slot, 16, 3), 0)
            self.assertEqual(LIBC.semctl(self.sem, slot, 11), os.getpid())
        self.assertEqual(self.probe().returncode, 0)

    def test_foreign_allocation_owner_is_refused(self):
        self.assertEqual(LIBC.semctl(self.sem, 2, 16, 0), 0)
        self.assertEqual(self.probe().returncode, 2)

    def test_changed_watchdog_configuration_is_refused(self):
        self.config.write_text(self.config.read_text().replace("interval = 5", "interval = 10"))
        self.assertEqual(self.probe().returncode, 2)

    def test_extra_watchdog_check_is_refused(self):
        self.config.write_text(self.config.read_text() + "file = /missing\n")
        self.assertEqual(self.probe().returncode, 2)


if __name__ == "__main__":
    unittest.main()
