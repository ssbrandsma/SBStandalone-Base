"""Host-only guard/recovery tests; no physical-device connection or service start."""
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
PACKAGE=ROOT/'packaging/fab4-reliability'
spec=importlib.util.spec_from_file_location('reliability',ROOT/'scripts/fab4-reliability.py')
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)

class Guard(unittest.TestCase):
    def test_shell_launch_paths(self):
        for action in ['start','restart','rescan','startresizer','','START','unknown']:
            with self.subTest(action=action):
                result=subprocess.run(['sh',str(PACKAGE/'squeezecenter'),action],capture_output=True)
                self.assertEqual(result.returncode,77)
                self.assertIn(b'SB_TINYSC_INHIBIT',result.stderr)
    def test_all_direct_payloads(self):
        for name in ['slimserver.pl','gdresized.pl','scanner.pl']:
            with self.subTest(name=name):
                result=subprocess.run(['perl',str(PACKAGE/name),'--help'],capture_output=True)
                self.assertEqual(result.returncode,77)
                self.assertIn(b'SB_TINYSC_INHIBIT',result.stderr)
    def test_lua_without_runtime_state(self):
        from lupa.lua51 import LuaRuntime
        for scenario in ['normal_boot','missing_ram','missing_base','base_failed','usb_at_boot',
                         'usb_later','usb_reinsert','jive_restart','squeezeplay_restart','repeated_init',
                         'persistent_policy_unreadable']:
            with self.subTest(scenario=scenario):
                lua=LuaRuntime(unpack_returned_tuples=True)
                lua.execute('''
                  package.preload['loop.simple']=function() return {class=function() end} end
                  package.preload['jive.AppletMeta']=function() return {} end
                  package.preload['jive.Applet']=function() return {} end
                  io={open=function() error('must not read optional state') end}
                  os={execute=function() error('must not launch a producer') end}
                  print=function() end
                ''')
                meta=lua.execute((PACKAGE/'SqueezeCenterMeta.lua').read_text(),'guard.Meta')
                app=lua.execute((PACKAGE/'SqueezeCenterApplet.lua').read_text(),'guard.Applet')
                registrations=[]
                obj=lua.table();obj['registerService']=lambda _,name: registrations.append(name)
                meta.registerApplet(obj);meta.configureApplet(obj)
                self.assertEqual(registrations,['isBuiltInSCRunning'])
                for name in ['squeezecenterStartupCheck','startSqueezeCenter','restartServer',
                             '_writeSCPrefsFile','_wipeRescan','switchServerToDifferentMedia',
                             '_squeezecenterAction']:
                    self.assertFalse(app[name](lua.table(),'arbitrary-media'))
    def test_lua_failure_keeps_shell_and_leaf_barriers(self):
        from lupa.lua51 import LuaRuntime,LuaError
        lua=LuaRuntime()
        lua.execute("package.preload['loop.simple']=function() error('injected Lua initialization failure') end")
        with self.assertRaises(LuaError):
            lua.execute((PACKAGE/'SqueezeCenterMeta.lua').read_text(),'failed.Meta')
        self.test_shell_launch_paths();self.test_all_direct_payloads()
    def test_installation_is_gated_for_every_partial_state(self):
        for count in range(7):
            with self.subTest(installed_files=count):
                with self.assertRaisesRegex(RuntimeError,'INSTALLATION REFUSED'):
                    tool.installation_gate()
    def test_missing_policy_fails_before_transport(self):
        from unittest.mock import patch
        for failure in [FileNotFoundError('missing'),OSError('unreadable')]:
            with self.subTest(failure=type(failure).__name__):
                again=importlib.util.module_from_spec(spec)
                with patch.object(Path,'read_text',side_effect=failure):
                    with self.assertRaises(type(failure)):spec.loader.exec_module(again)
    def test_artifact_hashes(self):
        for name,expected in tool.POLICY['sha256'].items():
            self.assertEqual(tool.sha((PACKAGE/name).read_bytes()),expected)

class FirmwareFault(unittest.TestCase):
    def test_exact_boot_condition_exposes_factory_reset(self):
        boot=Path(os.environ.get('SB_FAB4_BOOT_SOURCE',ROOT/'.local/wp4a2a/linuxrc.bin'))
        if not boot.exists():self.skipTest('captured actual firmware /linuxrc required')
        source=boot.read_text()
        line=next(x for x in source.splitlines() if '$MOUNT_OK -ne 0' in x)
        self.assertIn('/usr/sbin/flash_eraseall -q ${MOUNT_MTD}',source)
        self.assertLess(source.index('flash_eraseall'),source.index('# Mount overlay filesystem'))
        self.assertIn('exec /usr/sbin/chroot /mnt/overlay /sbin/init',source)
        with tempfile.TemporaryDirectory() as directory:
            predicate=line.replace('/mnt/storage/.factoryreset',shlex.quote(directory+'/factoryreset'))
            for mount_result,expected in [(0,b'healthy'),(1,b'factory_reset')]:
                # Execute ONLY the source conditional; no firmware action or destructive command.
                command=f'MOUNT_OK={mount_result}; FACTORY_RESET=1; '+predicate+'; then printf factory_reset; else printf healthy; fi'
                result=subprocess.run(['sh','-c',command],capture_output=True,check=True)
                self.assertEqual(result.stdout,expected)

class MemoryDevice:
    def __init__(self,files):self.files=dict(files);self.writes=[]
    def read(self,path):return self.files[path]
    def atomic(self,path,data,expected,mode,uid,gid):
        assert tool.sha(self.files[path])==expected
        self.writes.append(path);self.files[path]=data

class Recovery(unittest.TestCase):
    def setUp(self):
        self.path='/etc/init.d/squeezecenter';self.original=b'original'
        self.guard=(PACKAGE/'squeezecenter').read_bytes()
        self.row={'path':self.path,'sha256':tool.sha(self.original),'mode':'0o755','uid':0,'gid':0}
        self.usb='20 15 8:1 / /media/sda1 rw - vfat /dev/sda1 rw\n'
    def test_default_preserves_only_guard(self):
        device=MemoryDevice({self.path:self.guard})
        tool.restore(device,[(self.row,self.original)],self.usb)
        self.assertEqual(device.writes,[])
        self.assertEqual(device.read(self.path),self.guard)
    def test_usb_rollback_requires_ack_before_writes(self):
        device=MemoryDevice({self.path:self.guard})
        with self.assertRaisesRegex(RuntimeError,'USB ATTACHED'):
            tool.restore(device,[(self.row,self.original)],self.usb,True)
        self.assertEqual(device.writes,[])
    def test_explicit_full_restore(self):
        device=MemoryDevice({self.path:self.guard})
        tool.restore(device,[(self.row,self.original)],self.usb,True,True)
        self.assertEqual(device.read(self.path),self.original)
    def test_interrupted_partial_install_is_recoverable(self):
        import itertools
        paths=sorted(tool.TINY)
        for mask in itertools.product([False,True],repeat=len(paths)):
            records=[];files={}
            for path,installed in zip(paths,mask):
                original=('original:'+path).encode()
                guard=(PACKAGE/tool.POLICY['targets'][path]).read_bytes()
                row=dict(self.row,path=path,sha256=tool.sha(original))
                records.append((row,original));files[path]=guard if installed else original
            device=MemoryDevice(files)
            tool.restore(device,records,'',True)
            self.assertTrue(all(device.read(row['path'])==data for row,data in records))
    def test_foreign_change_is_preserved(self):
        device=MemoryDevice({self.path:b'foreign'})
        with self.assertRaisesRegex(RuntimeError,'unrecognized'):
            tool.restore(device,[(self.row,self.original)],'',True)
        self.assertEqual(device.writes,[])
    def test_corrupt_backup_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p=root/'files'/self.path.lstrip('/');p.parent.mkdir(parents=True)
            p.write_bytes(b'corrupt')
            row=dict(self.row,bytes=len(self.original),symlink=None)
            (root/'files.json').write_text(json.dumps({'files':[row]}))
            with self.assertRaisesRegex(ValueError,'hash mismatch'):tool.snapshot_load(root)

class AtomicHost(unittest.TestCase):
    def setUp(self):
        if os.geteuid()!=0:self.skipTest('root required for exact owner restoration test')
        self.directory=tempfile.TemporaryDirectory()
        self.path=str(Path(self.directory.name)/'known-good')
        Path(self.path).write_bytes(b'original')
        tool.ALLOWED.add(self.path)
    def tearDown(self):
        if hasattr(self,'directory'):
            tool.ALLOWED.discard(self.path);self.directory.cleanup()
    def backend(self,fault=None):
        class Local(tool.SSH):
            def __init__(self):pass
            def call(inner,command,data=None):
                replacing='mv -f' in command
                if replacing and fault=='before':raise RuntimeError('interrupted before atomic rename')
                result=subprocess.run(['sh','-c',command],input=data,capture_output=True,timeout=5)
                if result.returncode:raise RuntimeError(result.stderr.decode())
                if replacing and fault=='after':raise RuntimeError('SSH lost after atomic rename')
                return result.stdout
        return Local()
    def test_real_atomic_replacement_and_restoration(self):
        device=self.backend()
        device.atomic(self.path,b'candidate',tool.sha(b'original'),0o640,0,0)
        self.assertEqual(Path(self.path).read_bytes(),b'candidate')
        self.assertEqual(Path(self.path).stat().st_mode & 0o777,0o640)
        device.atomic(self.path,b'original',tool.sha(b'candidate'),0o644,0,0)
        self.assertEqual(Path(self.path).read_bytes(),b'original')
    def test_interrupted_before_rename_preserves_original(self):
        with self.assertRaisesRegex(RuntimeError,'before'):
            self.backend('before').atomic(self.path,b'candidate',tool.sha(b'original'),0o644,0,0)
        self.assertEqual(Path(self.path).read_bytes(),b'original')
    def test_lost_ack_after_rename_leaves_complete_recoverable_version(self):
        with self.assertRaisesRegex(RuntimeError,'after'):
            self.backend('after').atomic(self.path,b'candidate',tool.sha(b'original'),0o644,0,0)
        self.assertEqual(Path(self.path).read_bytes(),b'candidate')
        self.backend().atomic(self.path,b'original',tool.sha(b'candidate'),0o644,0,0)
        self.assertEqual(Path(self.path).read_bytes(),b'original')

if __name__=='__main__':unittest.main()
