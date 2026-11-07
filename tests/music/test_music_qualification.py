"""Execute generated Music recovery with the real copier on a host sandbox."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HELPER = str(Path(sys.argv.pop(1)).resolve())
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("qualification", ROOT / "scripts/fab4-music-qualification.py")
q = importlib.util.module_from_spec(spec)
spec.loader.exec_module(q)


class Recovery(unittest.TestCase):
    def test_preflight_enforces_install_and_checked_restart_boundaries(self):
        class Device:
            free=3192
            def profile(self):
                return '7.7.3 r16676', ('1 0 0:0 / /var rw - ramfs ramfs rw\n'
                    '2 0 0:1 / /dev rw - tmpfs none rw\n'
                    '3 0 8:1 / /media/sda1 rw - vfat /dev/sda1 rw\n')
            def call(self,command):
                if 'boot_id' in command:return b'boot'
                if '/api/nowplaying' in command:return json.dumps(dict(active=False,state='stopped')).encode()
                if 'df -k' in command:return ('fs 10000 9000 '+str(self.free)+' 90% /\n').encode()
                raise AssertionError(command)
            def script(self,text):return b''
        device=Device()
        with self.assertRaises(RuntimeError):q.life.preflight(device)
        q.life.preflight(device,minimum_free_kib=1428)
        device.free=1427
        with self.assertRaises(RuntimeError):q.life.preflight(device,minimum_free_kib=1428)
        device.free=1428;q.life.preflight(device,minimum_free_kib=1428)
        with self.assertRaises(RuntimeError):q.life.preflight(device,minimum_free_kib=0)
        device.free=4096;q.life.preflight(device)

    def test_restart_reserves_atomic_recovery_without_stage_duplication(self):
        plan=dict(files=[dict(bytes=936740,original=dict(bytes=933888)),dict(bytes=100,original=None)])
        self.assertEqual(q.restart_space_kib(plan),1428)
        # Boundary is based on actual checked members; a larger saved image
        # increases the requirement even if the new candidate is smaller.
        plan['files'][0]['original']['bytes']=2097152
        self.assertEqual(q.restart_space_kib(plan),2560)

    def test_generated_recovery_restores_current_browse_stack(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            overlay, stage = root / "overlay", root / "stage"
            overlay.mkdir(mode=0o700);stage.mkdir(mode=0o700)
            (root / "run").mkdir(mode=0o700)
            (stage / "new").mkdir();(stage / "original").mkdir();(stage / "recovery").mkdir()
            rows, originals = [], {}
            for index, (path, mode) in enumerate(q.TARGETS.items()):
                data = f"candidate-{index}".encode()
                old = None
                if index % 2 or path.endswith(("sbbase-music", "StandaloneBaseApplet.lua")):
                    raw = f"original-{index}".encode()
                    old = dict(path=path, source=f"original/{index}", sha256=q.life.sha(raw), mode=mode)
                    (stage / old["source"]).write_bytes(raw)
                    originals[path] = (old, raw)
                row = dict(path=path, source=f"new/{index}", sha256=q.life.sha(data), mode=mode, original=old)
                rows.append(row)
                (stage / row["source"]).write_bytes(data)
                target = overlay / path.lstrip("/")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data);target.chmod(mode)
            for name in ("sbproxy", "sbwebserver"):
                originals[q.APP + "/" + name] = (dict(sha256="1" * 64), b"audit")
            wrapper = q.recovery_wrapper()
            (stage / "recovery/base").write_bytes(wrapper)
            plan = dict(files=rows, recovery_wrapper_sha256=q.life.sha(wrapper))
            scripts = q.scripts(plan, originals)
            # Replace only the fixed host-test roots; all target/path policy
            # checks still run inside the real native fixture executable.
            for name, raw in scripts.items():
                text = raw.decode().replace("h=" + q.APP + "/sb-maintenance-helper", "h=" + HELPER)
                text = text.replace(q.STAGE, str(stage)).replace("/usr/share", str(overlay / "usr/share"))
                text = text.replace("/etc/init.d", str(overlay / "etc/init.d"))
                text = text.replace("/var/run/standalonebase", str(root / "run"))
                (stage / name).write_text(text)
            env = dict(os.environ, SB_TEST_INSTALL_STAGE=str(stage), SB_TEST_INSTALL_TARGET=str(overlay))
            result = subprocess.run(["sh", str(stage / "recover-base.sh")], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            keep = {q.APP + "/sb-maintenance-helper", q.APP + "/restart-actions.sh", "/etc/init.d/squeezeplay", q.APP + "/sbbase"}
            for row in rows:
                target = overlay / row["path"].lstrip("/")
                if row["path"] == q.APP + "/sbbase":
                    self.assertEqual(target.read_bytes(), wrapper)
                elif row["path"] in keep:
                    self.assertEqual(target.read_bytes(), (stage / row["source"]).read_bytes())
                elif row["original"]:
                    self.assertEqual(target.read_bytes(), originals[row["path"]][1])
                else:
                    self.assertFalse(target.exists())

    def test_fixed_recovery_has_distinct_ram_db_and_no_second_attempt(self):
        text = q.recovery_wrapper().decode()
        self.assertIn("library-before-wp4a3b-recovery.db", text)
        self.assertIn('mv "$db" "$saved"', text)
        self.assertIn("set -C", text)
        self.assertIn("--music-browse-only", text)
        for forbidden in ("kill ", "rm ", "squeezeplay stop", "squeezecenter start"):
            self.assertNotIn(forbidden, text)

    def test_recovery_wrapper_preserves_ram_library_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);app=root / "app";ram=root / "ram";run=root / "run"
            for folder in (app,ram,run):folder.mkdir(mode=0o700)
            for name,output in (("sbbase-music","BROWSE"),("sbbase-core","CORE")):
                path=app / name;path.write_text('#!/bin/sh\necho '+output+'\n');path.chmod(0o700)
            (ram / "library.db").write_bytes(b"existing valid library")
            (ram / "library-before-menu-fix.db").write_bytes(b"older backup")
            (run / "wp4a3b-recovery.enabled").touch()
            text=q.recovery_wrapper().decode().replace(q.APP,str(app)).replace("/dev/standalone-music-test",str(ram)).replace("/var/run/standalonebase",str(run))
            path=root / "wrapper";path.write_text(text)
            first=subprocess.run(["sh",str(path)],capture_output=True,text=True)
            self.assertEqual((first.returncode,first.stdout),(0,"BROWSE\n"),first.stderr)
            self.assertEqual((ram / "library-before-wp4a3b-recovery.db").read_bytes(),b"existing valid library")
            self.assertEqual((ram / "library-before-menu-fix.db").read_bytes(),b"older backup")
            second=subprocess.run(["sh",str(path)],capture_output=True,text=True)
            self.assertEqual((second.returncode,second.stdout),(0,"CORE\n"))


if __name__ == "__main__":
    unittest.main()
