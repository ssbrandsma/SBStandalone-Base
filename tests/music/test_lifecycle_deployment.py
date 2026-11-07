"""Exercise production rollback planning with the real native copier in a host sandbox."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HELPER = str(Path(sys.argv.pop(1)).resolve())
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("deployment", ROOT / "scripts/fab4-lifecycle.py")
deployment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deployment)
if "--music" in sys.argv:
    sys.argv.remove("--music")
    music_spec=importlib.util.spec_from_file_location("music_deployment",ROOT / "scripts/fab4-music-qualification.py")
    music_deployment=importlib.util.module_from_spec(music_spec)
    music_spec.loader.exec_module(music_deployment)
    deployment.TARGETS=music_deployment.TARGETS
    deployment.REMOTE_BACKUP=music_deployment.STAGE


class Deployment(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.stage = self.root / "stage"
        self.stage.mkdir(mode=0o700)
        self.target = self.root / "overlay"
        self.target.mkdir(mode=0o700)
        self.rows = []
        self.original = {}
        for index, (path, mode) in enumerate(deployment.TARGETS.items()):
            old = None
            if index % 2:
                data = ("original-%d" % index).encode()
                leaf = self.target / path.lstrip("/")
                leaf.parent.mkdir(parents=True, exist_ok=True)
                leaf.write_bytes(data)
                leaf.chmod(mode)
                source = self.stage / ("old-%d" % index)
                source.write_bytes(data)
                old = dict(source=source.name, sha256=deployment.sha(data), mode=mode)
                self.original[path] = data
            data = ("new-%d" % index).encode()
            source = self.stage / ("new-%d" % index)
            source.write_bytes(data)
            (self.target / path.lstrip("/")).parent.mkdir(parents=True, exist_ok=True)
            self.rows.append(dict(path=path, mode=mode, sha256=deployment.sha(data),
                                  source=source.name, original=old))
        self.plan = dict(files=self.rows, recovery=dict(applet_sha256="0" * 64))
        self.env = dict(os.environ, SB_TEST_INSTALL_STAGE=str(self.stage),
                        SB_TEST_INSTALL_TARGET=str(self.target))

    def tearDown(self):
        self.tmp.cleanup()

    def invoke(self, command, *arguments, failure=None):
        args = list(map(str, arguments))
        if command == "install-file":
            args[0] = str(self.stage / args[0].removeprefix(deployment.REMOTE_BACKUP + "/"))
            args[1] = str(self.target / args[1].lstrip("/"))
        elif command == "remove-file":
            args[0] = str(self.target / args[0].lstrip("/"))
        env = dict(self.env)
        if failure:
            env["SB_TEST_INSTALL_FAIL"] = failure
        value = subprocess.run([HELPER, command, *args], env=env, capture_output=True, timeout=3)
        if value.returncode:
            raise RuntimeError("Native install/restore refused")

    def observed(self):
        return {row["path"]: (dict(sha256=deployment.sha(leaf.read_bytes())), leaf.read_bytes())
                for row in self.rows if (leaf := self.target / row["path"].lstrip("/")).exists()}

    def assert_original(self):
        actual = {path: row[1] for path, row in self.observed().items()}
        self.assertEqual(actual, self.original)

    def test_interruption_at_every_deployment_boundary(self):
        # Includes an interrupted first copy and a complete deployment. No
        # service, USB path, process or watchdog is changed by this fixture.
        for length in range(len(self.rows) + 1):
            with self.subTest(completed=length):
                for row in self.rows[:length]:
                    self.invoke("install-file", deployment.REMOTE_BACKUP + "/" + row["source"],
                                row["path"], row["mode"], row["original"]["sha256"] if row["original"] else "absent")
                deployment.file_rollback(self.plan, self.observed(), self.invoke,
                                         [row["path"] for row in self.rows[:length]])
                self.assert_original()

    def test_failure_after_rename_is_also_restored(self):
        for row in self.rows:
            with self.subTest(path=row["path"]):
                with self.assertRaises(RuntimeError):
                    self.invoke("install-file", deployment.REMOTE_BACKUP + "/" + row["source"],
                                row["path"], row["mode"], failure="after-rename")
                deployment.file_rollback(self.plan, self.observed(), self.invoke, [row["path"]])
                self.assert_original()

    def test_foreign_revision_is_never_overwritten(self):
        row = self.rows[0]
        leaf = self.target / row["path"].lstrip("/")
        leaf.write_bytes(b"unrelated owner change")
        with self.assertRaises(RuntimeError):
            deployment.file_rollback(self.plan, self.observed(), self.invoke, [row["path"]])
        self.assertEqual(leaf.read_bytes(), b"unrelated owner change")

    def test_wrong_host_is_refused_before_connect(self):
        with self.assertRaises(ValueError):
            deployment.Device("192.168.1.245")

    def test_attempt_journal_prevents_repeating_restart(self):
        deployment.attempt(self.root, "restart", dict(boot="fixture"))
        with self.assertRaises(RuntimeError):
            deployment.attempt(self.root, "restart", dict(boot="fixture"))

    def test_reboot_requires_verified_post_restart_checks(self):
        with self.assertRaises(RuntimeError):
            deployment.attempt(self.root, "reboot", {})
        deployment.save_json(self.root / "hardware-attempts.json", dict(restart=dict(result=dict(state="COMPLETED"))))
        with self.assertRaises(RuntimeError):
            deployment.attempt(self.root, "reboot", {})
        deployment.save_json(self.root / "hardware-attempts.json", dict(restart=dict(result=dict(state="COMPLETED"), after={})))
        deployment.attempt(self.root, "reboot", {})
        with self.assertRaises(RuntimeError):
            deployment.attempt(self.root, "reboot", {})


if __name__ == "__main__":
    unittest.main()
