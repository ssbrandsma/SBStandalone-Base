"""Native, bounded overlay copy/restore tests on temporary host files only."""
import os
import hashlib
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

HELPER = str(Path(sys.argv.pop(1)).resolve())


class InternalFiles(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="sb-internal-install-")
        self.root = Path(self.tmp.name)
        self.stage = self.root / "stage"
        self.stage.mkdir(mode=0o700)
        self.target_root = self.root / "target"
        self.target = self.target_root / "usr/share/jive/applets/StandaloneBase/sbbase"
        self.target.parent.mkdir(parents=True, mode=0o700)
        self.target.write_bytes(b"original executable\n")
        self.target.chmod(0o755)
        self.old = self.stage / "old"
        self.old.write_bytes(self.target.read_bytes())
        self.source = self.stage / "new"
        self.source.write_bytes(bytes(range(256)) * 101)
        self.env = dict(os.environ, SB_TEST_INSTALL_STAGE=str(self.stage),
                        SB_TEST_INSTALL_TARGET=str(self.target_root))

    def tearDown(self):
        self.tmp.cleanup()

    def copy(self, source=None, target=None, mode=0o755, failure=None, expected=None):
        env = dict(self.env)
        if failure:
            env["SB_TEST_INSTALL_FAIL"] = failure
        arguments = [HELPER, "install-file", str(source or self.source), str(target or self.target), str(mode)]
        if expected is not None:
            arguments.append(expected)
        return subprocess.run(arguments, env=env,
                              capture_output=True, timeout=3)

    def test_compare_and_replace_rejects_changed_file(self):
        original = self.target.read_bytes()
        self.assertNotEqual(self.copy(expected="0" * 64).returncode, 0)
        self.assertEqual(self.target.read_bytes(), original)
        self.assertEqual(self.copy(expected=hashlib.sha256(original).hexdigest()).returncode, 0)

    def test_absent_expectation_never_overwrites_existing_file(self):
        original = self.target.read_bytes()
        self.assertNotEqual(self.copy(expected="absent").returncode, 0)
        self.assertEqual(self.target.read_bytes(), original)
        self.target.unlink()
        self.assertEqual(self.copy(expected="absent").returncode, 0)

    def test_atomic_copy_preserves_bytes_mode_and_mtime(self):
        os.utime(self.source, ns=(1000000000000000000, 1000000000123456789))
        self.assertEqual(self.copy().returncode, 0)
        self.assertEqual(self.target.read_bytes(), self.source.read_bytes())
        self.assertEqual(stat.S_IMODE(self.target.stat().st_mode), 0o755)
        self.assertEqual(self.target.stat().st_mtime_ns, self.source.stat().st_mtime_ns)

    def test_restore_original_from_qualified_backup(self):
        self.assertEqual(self.copy().returncode, 0)
        self.assertEqual(self.copy(source=self.old).returncode, 0)
        self.assertEqual(self.target.read_bytes(), b"original executable\n")

    def test_shared_sha256_matches_known_and_multiblock_vectors(self):
        for data in (b"", b"abc", bytes(range(256)) * 101):
            self.source.write_bytes(data)
            result = subprocess.run([HELPER, "sha256-internal", str(self.source)],
                                    env=self.env, capture_output=True, text=True, timeout=3)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), hashlib.sha256(data).hexdigest())

    def test_unknown_target_cannot_be_modified(self):
        target = self.target_root / "linuxrc"
        target.write_bytes(b"factory\n")
        self.assertEqual(self.copy(target=target).returncode, 2)
        self.assertEqual(target.read_bytes(), b"factory\n")

    def test_source_outside_private_stage_is_refused(self):
        source = self.root / "unqualified"
        source.write_bytes(b"unqualified\n")
        self.assertEqual(self.copy(source=source).returncode, 2)
        self.assertEqual(self.target.read_bytes(), self.old.read_bytes())

    def test_symlink_target_is_refused(self):
        outside = self.root / "unrelated"
        outside.write_bytes(b"untouched\n")
        self.target.unlink()
        self.target.symlink_to(outside)
        self.assertEqual(self.copy().returncode, 2)
        self.assertEqual(outside.read_bytes(), b"untouched\n")

    def test_symlink_parent_is_refused(self):
        self.target.unlink()
        self.target.parent.rmdir()
        outside = self.root / "unrelated-directory"
        outside.mkdir()
        (outside / "sbbase").write_bytes(b"untouched\n")
        self.target.parent.symlink_to(outside, target_is_directory=True)
        self.assertEqual(self.copy().returncode, 2)
        self.assertEqual((outside / "sbbase").read_bytes(), b"untouched\n")

    def test_symlink_source_is_refused(self):
        link = self.stage / "alias"
        link.symlink_to(self.source)
        self.assertEqual(self.copy(source=link).returncode, 2)

    def test_oversized_source_and_privileged_mode_are_refused(self):
        huge = self.stage / "huge"
        with huge.open("wb") as f:
            f.truncate(2097153)
        self.assertEqual(self.copy(source=huge).returncode, 2)
        self.assertEqual(self.copy(mode=0o4755).returncode, 2)
        self.assertEqual(self.target.read_bytes(), self.old.read_bytes())

    def test_failure_before_rename_retains_complete_original(self):
        self.assertEqual(self.copy(failure="before-rename").returncode, 2)
        self.assertEqual(self.target.read_bytes(), self.old.read_bytes())
        self.assertEqual(list(self.target.parent.glob("*.wp4a2b-*")), [])

    def test_failure_after_rename_leaves_complete_new_version_for_rollback(self):
        self.assertEqual(self.copy(failure="after-rename").returncode, 2)
        self.assertEqual(self.target.read_bytes(), self.source.read_bytes())
        self.assertEqual(self.copy(source=self.old).returncode, 0)
        self.assertEqual(self.target.read_bytes(), self.old.read_bytes())


if __name__ == "__main__":
    unittest.main()
