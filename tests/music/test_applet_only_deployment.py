"""Real shell/file deployment in an isolated host tree, never a device."""
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
import zipfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
# No SSH connection in these fixtures. Minimal import stubs let the cached
# offline container test the real file workflow without installing Paramiko.
if importlib.util.find_spec("paramiko") is None:
    sys.modules["paramiko"] = types.ModuleType("paramiko")
    crypto = types.ModuleType("cryptography.utils")
    crypto.CryptographyDeprecationWarning = type("CryptoWarning", (Warning,), {})
    sys.modules["cryptography"] = types.ModuleType("cryptography")
    sys.modules["cryptography.utils"] = crypto
spec = importlib.util.spec_from_file_location("deploy", ROOT / "scripts/deploy_dist.py")
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)


class Radio:
    host = "192.168.1.244"

    def __init__(self, root):
        self.root = root
        self.commands = []

    def run(self, command, attempts=1, binary=False):
        self.commands.append(command)
        assert attempts == 1, "no mutation replay"
        command = command.replace("/usr", str(self.root / "usr"))
        value = subprocess.run(["sh", "-c", command], capture_output=True, timeout=5)
        if value.returncode:
            raise RuntimeError(value.stderr.decode())
        return value.stdout if binary else value.stdout.decode().rstrip("\r\n")

    def upload(self, source, directory, name):
        shutil.copyfile(source, Path(directory) / name)


class AppletOnly(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.target = self.root / deploy.TARGETS[0].lstrip("/")
        self.target.mkdir(parents=True)
        self.package = self.root / "dist/StandaloneBase-0.7.0-fab4.zip"
        self.package.parent.mkdir()
        with zipfile.ZipFile(self.package, "w") as archive:
            for name in deploy.APPLET_FILES:
                archive.writestr(name, b"fab4\n" if name == "build-target" else b"new " + name.encode())
            archive.writestr("sbbase", b"NEVER UPLOAD")
            archive.writestr("config.json", b"NEVER OVERWRITE")
        self.old = {}
        for name in deploy.APPLET_FILES - {"LifecycleHeartbeat.lua"}:
            data = b"fab4\n" if name == "build-target" else b"original " + name.encode()
            (self.target / name).write_bytes(data)
            (self.target / name).chmod(0o644)
            os.utime(self.target / name, (1234567890, 1234567890))
            self.old[name] = data
        (self.target / "sbbase").write_bytes(b"running native")
        self.radio = Radio(self.root)
        self.backup = self.root / "backup"

    def tearDown(self):
        self.temp.cleanup()

    def assert_original(self):
        for name in deploy.APPLET_FILES:
            path = self.target / name
            if name in self.old:
                self.assertEqual(path.read_bytes(), self.old[name])
                self.assertEqual(int(path.stat().st_mtime), 1234567890)
            else:
                self.assertFalse(path.exists())

    def test_narrow_validation_excludes_native_config_and_fixtures(self):
        names, version = deploy.validate_package(self.package, True)
        self.assertEqual(set(names), deploy.APPLET_FILES)
        self.assertEqual(version, "0.7.0")

    def test_deploy_and_explicit_restore_exact_originals(self):
        result = deploy.deploy_applet_only(self.radio, self.package, self.backup)
        self.assertEqual(result["state"], "DEPLOYED_ON_DISK")
        self.assertFalse(result["activated"])
        self.assertEqual((self.target / "sbbase").read_bytes(), b"running native")
        with zipfile.ZipFile(self.backup / "applet.zip") as selected:
            self.assertEqual(set(selected.namelist()), deploy.APPLET_FILES)
        commands = "\n".join(self.radio.commands)
        for forbidden in ("squeezeplay", "kill ", "watchdog", "squeezecenter", "patch-squeezeplay", "/mnt/storage"):
            self.assertNotIn(forbidden, commands)
        deploy.deploy_applet_only(self.radio, self.package, None, restore=self.backup)
        self.assert_original()

    def test_interrupted_copy_after_rename_rolls_back(self):
        actual = deploy.applet_replace
        calls = 0
        def fail_once(*args):
            nonlocal calls
            actual(*args)
            calls += 1
            if calls == 4:
                raise RuntimeError("injected lost acknowledgement after rename")
        with patch.object(deploy, "applet_replace", fail_once):
            with self.assertRaises(RuntimeError):
                deploy.deploy_applet_only(self.radio, self.package, self.backup)
        self.assert_original()

    def test_unknown_revision_is_never_restored_over(self):
        deploy.deploy_applet_only(self.radio, self.package, self.backup)
        target = self.target / "StandaloneBaseApplet.lua"
        target.write_bytes(b"owner modification")
        with self.assertRaises(RuntimeError):
            deploy.deploy_applet_only(self.radio, self.package, None, restore=self.backup)
        self.assertEqual(target.read_bytes(), b"owner modification")

    def test_symlink_target_rejected_before_change(self):
        target = self.target / "StandaloneBaseApplet.lua"
        target.unlink()
        target.symlink_to(self.target / "sbbase")
        with self.assertRaises(RuntimeError):
            deploy.deploy_applet_only(self.radio, self.package, self.backup)
        self.assertTrue(target.is_symlink())
        self.assertFalse(self.backup.exists())

    def test_corrupt_backup_refused(self):
        deploy.deploy_applet_only(self.radio, self.package, self.backup)
        (self.backup / "originals.tar").write_bytes(b"corrupt")
        with self.assertRaises(ValueError):
            deploy.load_applet_backup(self.backup, self.radio.host)

    def test_failed_atomic_copy_cleans_its_temporary_file(self):
        name = "StandaloneBaseApplet.lua"
        with self.assertRaises(RuntimeError):
            deploy.applet_replace(self.radio, "/missing/source", name, 0o644)
        self.assertFalse((self.target / (name + ".new")).exists())
        self.assert_original()

    def test_native_atomic_copy_and_restore_never_executes_payload(self):
        target=self.target/'sbbase'
        target.chmod(0o755)
        original=self.root/'original-native'
        shutil.copy2(target,original)
        candidate=self.root/'native-candidate'
        candidate.write_bytes(b'not executed; replacement native bytes')
        remote=deploy.TARGETS[0]+'/sbbase'
        deploy.atomic_replace(self.radio,str(candidate),remote,0o755)
        self.assertEqual(target.read_bytes(),candidate.read_bytes())
        deploy.atomic_replace(self.radio,str(original),remote,0o755)
        self.assertEqual(target.read_bytes(),original.read_bytes())
        self.assertEqual(target.stat().st_mtime_ns,original.stat().st_mtime_ns)

    def test_binary_transport_preserves_tar_bytes_and_newlines(self):
        data = b"original\x00binary\n\n"
        class Output(io.BytesIO):
            channel = type("Channel", (), {"recv_exit_status": lambda self: -1})()
        class Client:
            def get_transport(self):
                return type("Transport", (), {"is_active": lambda self: True})()
            def exec_command(self, command):
                return None, Output(data + b"\n__STANDALONEBASE_DEPLOY_RC=0\n"), Output(b"")
        radio = deploy.Radio.__new__(deploy.Radio)
        radio.client = Client()
        self.assertEqual(radio.run("cat original", attempts=1, binary=True), data)


if __name__ == "__main__":
    unittest.main()
