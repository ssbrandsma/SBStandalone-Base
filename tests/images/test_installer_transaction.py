"""Real filesystem/inode qualification of the stock installer's transaction layer."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from lupa.lua51 import LuaRuntime

ROOT = Path(sys.argv.pop(1)).resolve()
FIXTURE = Path(sys.argv.pop(1)).resolve()


class Transaction(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='sb-installer-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.parent = self.root / 'applets'
        self.parent.mkdir()
        self.lua = LuaRuntime(encoding=None, unpack_returned_tuples=True)
        self.module = self.lua.execute((ROOT / 'applet/InstallerTransaction.lua').read_bytes())
        self.fail_destination = None
        iterator = self.lua.eval(b'function(t) local i=0;return function() i=i+1;return t[i] end end')

        def attributes(path):
            path = Path(os.fsdecode(path))
            if not path.exists() and not path.is_symlink():
                return None
            value=path.lstat()
            return self.convert({'mode': 'link' if path.is_symlink() else 'directory' if path.is_dir() else 'file',
                                 'ino':value.st_ino,'dev':value.st_dev,'uid':value.st_uid,'gid':value.st_gid,'size':value.st_size})

        def mkdir(path):
            try:
                Path(os.fsdecode(path)).mkdir()
                return True
            except OSError:
                return False

        def rename(source, destination):
            destination = os.fsdecode(destination)
            if self.fail_destination and self.fail_destination in destination:
                self.fail_destination = None
                return False
            os.rename(os.fsdecode(source), destination)
            return True

        def hasher():
            digest = hashlib.sha1()
            return self.lua.table_from({b'update': lambda _, data: digest.update(data),
                                       b'digest': lambda _: digest.hexdigest().encode()})

        self.options = self.lua.table_from({
            b'fs': self.lua.table_from({b'attributes': attributes, b'symlinkattributes': attributes, b'mkdir': mkdir,b'rmdir':lambda path: Path(os.fsdecode(path)).rmdir() is None,
                b'dir': lambda path: iterator(self.convert(sorted(os.listdir(os.fsdecode(path)))))}),
            b'io': self.lua.globals().io,
            b'os': self.lua.table_from({b'rename': rename}), b'hasher': hasher,
            b'json': self.lua.table_from({b'encode': lambda value: json.dumps(self.python(value)).encode(),
                                        b'decode': lambda value: self.convert(json.loads(value))})})
        self.hasher = hasher
        self.function = self.lua.eval(b'function(f)return function(...)return f(...)end end')
        # Lupa omits popen. Execute the real shell probe and expose its bytes
        # through a real Lua file handle, preserving the production read path.
        lua_open = self.lua.globals().io.open
        def popen(command, mode):
            data = subprocess.check_output(os.fsdecode(command), shell=True)
            probe = self.root / 'mode-probe.txt'
            probe.write_bytes(data)
            return lua_open(os.fsencode(probe), b'rb')
        self.lua.globals().io.popen = self.function(popen)
        for key,value in self.options[b'fs'].items():
            self.options[b'fs'][key] = self.function(value)

    def convert(self, value):
        if isinstance(value, dict):
            return self.lua.table_from({k.encode(): self.convert(v) for k, v in value.items()})
        if isinstance(value, list):
            return self.lua.table_from([self.convert(v) for v in value])
        return value.encode() if isinstance(value, str) else value

    def python(self, value):
        if hasattr(value, 'items'):
            return {self.python(k): self.python(v) for k, v in value.items()}
        return value.decode() if isinstance(value, bytes) else value

    def applet(self, name, version='old', parent=None):
        path = (parent or self.parent) / name
        path.mkdir()
        (path / (name + 'Meta.lua')).write_text('return {version="' + version + '"}')
        return path

    def begin(self, *names):
        return self.module.new(os.fsencode(self.parent), self.convert({name: True for name in names}), self.options)

    def test_reaped_child_status_requires_correct_actual_mode(self):
        self.options[b'os'][b'execute'] = self.lua.eval(
            b'function(command) os.execute(command);return -1 end')
        self.applet('StandaloneBase')
        self.begin('StandaloneBase')
        self.assertEqual((self.parent / '.standalone-install').stat().st_mode & 0o7777, 0o700)

    def test_success_status_without_private_mode_is_refused(self):
        self.options[b'os'][b'execute'] = self.lua.eval(b'function()return 0 end')
        self.applet('StandaloneBase')
        with self.assertRaisesRegex(Exception, 'Cannot set installer mode'):
            self.begin('StandaloneBase')

    def test_verify_and_commit_need_no_child_after_extraction(self):
        self.applet('StandaloneBase')
        transaction=self.begin('StandaloneBase')
        staged=Path(os.fsdecode(transaction.destination(transaction,b'StandaloneBase')))
        (staged/'StandaloneBaseMeta.lua').write_text('return {}')
        (staged/'sbbase').write_bytes(b'prequalified inode')
        self.lua.execute(b'os.execute=function()error("fork unavailable")end;io.popen=function()error("fork unavailable")end')
        transaction.verify(transaction,b'StandaloneBase')
        transaction.commit(transaction,self.convert({'StandaloneBase':{}}))
        self.assertEqual((self.parent/'StandaloneBase/sbbase').stat().st_mode & 0o7777,0o755)

    def test_replaced_executable_inode_is_refused_before_commit(self):
        self.applet('StandaloneBase')
        transaction=self.begin('StandaloneBase')
        staged=Path(os.fsdecode(transaction.destination(transaction,b'StandaloneBase')))
        (staged/'StandaloneBaseMeta.lua').write_text('return {}')
        (staged/'sbbase').write_bytes(b'prequalified inode')
        transaction.verify(transaction,b'StandaloneBase')
        (staged/'replacement').write_bytes(b'prequalified inode')
        (staged/'replacement').replace(staged/'sbbase')
        with self.assertRaisesRegex(Exception,'Executable inode changed'):
            transaction.commit(transaction,self.convert({'StandaloneBase':{}}))

    def stage(self, transaction, name, version='new'):
        directory = Path(os.fsdecode(transaction.destination(transaction, name.encode())))
        (directory / (name + 'Meta.lua')).write_text('return {version="' + version + '"}')
        transaction.verify(transaction, name.encode())
        return directory

    def test_fresh_and_existing_configuration_states(self):
        for state in ('fresh', 'radio', 'spotify', 'fab4-local', 'candidate-renderer-enabled', 'extended-storage'):
            with self.subTest(state=state):
                if (self.parent / '.standalone-install').exists():
                    shutil.rmtree(self.parent / '.standalone-install')
                target = self.parent / 'StandaloneBase'
                if target.exists():
                    shutil.rmtree(target)
                if state != 'fresh':
                    self.applet('StandaloneBase')
                user = self.root / ('user-' + state)
                user.mkdir()
                for name in ('favorites', 'spotify-credentials', 'qobuz-settings', 'library.db', 'music.mp3', 'renderer-settings', 'sbdata'):
                    (user / name).write_bytes((state + name).encode())
                before = {p.name: p.read_bytes() for p in user.iterdir()}
                transaction = self.begin('StandaloneBase')
                self.stage(transaction, 'StandaloneBase')
                transaction.commit(transaction, self.convert({'StandaloneBase': {}}))
                assert 'new' in (target / 'StandaloneBaseMeta.lua').read_text()
                assert before == {p.name: p.read_bytes() for p in user.iterdir()}
                receipt = json.loads((self.parent / '.standalone-install/receipt.json').read_text())
                assert receipt['state'] == 'COMMITTED_ON_DISK'

    def test_failed_commit_restores_files_and_live_executable_inode(self):
        old = self.applet('StandaloneBase')
        self.applet('StandaloneRadio')
        executable = old / 'sbbase'
        shutil.copyfile(FIXTURE, executable)
        executable.chmod(0o700)
        process = subprocess.Popen([str(executable), 'daemon'], stdout=subprocess.PIPE)
        try:
            assert process.stdout.readline() == b'ready\n'
            inode = os.stat(f'/proc/{process.pid}/exe').st_ino
            digest = hashlib.sha256(Path(f'/proc/{process.pid}/exe').read_bytes()).digest()
            transaction = self.begin('StandaloneBase', 'StandaloneRadio')
            self.stage(transaction, 'StandaloneBase')
            self.stage(transaction, 'StandaloneRadio')
            self.fail_destination = '/applets/StandaloneRadio'
            with self.assertRaises(Exception):
                transaction.commit(transaction, self.convert({'StandaloneBase': {}, 'StandaloneRadio': {}}))
            assert 'old' in (old / 'StandaloneBaseMeta.lua').read_text()
            assert 'old' in (self.parent / 'StandaloneRadio/StandaloneRadioMeta.lua').read_text()
            assert process.poll() is None and os.stat(f'/proc/{process.pid}/exe').st_ino == inode
            assert hashlib.sha256(Path(f'/proc/{process.pid}/exe').read_bytes()).digest() == digest
        finally:
            process.terminate();process.wait(timeout=5);process.stdout.close()

    def test_uninstall_preserves_other_applets_and_independent_backup(self):
        old=self.applet('StandaloneBase')
        radio=self.applet('StandaloneRadio')
        transaction=self.begin('StandaloneBase')
        transaction.commit(transaction,self.convert({}))
        assert not old.exists() and radio.is_dir()
        assert (self.parent/'.standalone-install/old/StandaloneBase/StandaloneBaseMeta.lua').is_file()
        self.options[b'quiescent']=self.lua.eval(b'function()return true end')
        self.module.recover(os.fsencode(self.parent),self.options)
        assert old.is_dir() and radio.is_dir()

    def test_interruption_between_rename_and_journal_is_recovered(self):
        old = self.applet('StandaloneBase')
        transaction = self.begin('StandaloneBase')
        staged = self.stage(transaction, 'StandaloneBase')
        journal = self.parent / '.standalone-install'
        os.rename(old, journal / 'old/StandaloneBase')
        os.rename(staged, old)  # crash before row.saved/installed receipt updates
        self.module.recover(os.fsencode(self.parent), self.options)
        assert 'old' in (old / 'StandaloneBaseMeta.lua').read_text()
        assert json.loads((journal / 'receipt.json').read_text())['state'] == 'ROLLED_BACK'

    def test_incompatible_partial_runtime_and_invalid_lua_do_not_replace_old(self):
        old = self.applet('StandaloneBase')
        transaction = self.begin('StandaloneBase')
        staged = Path(os.fsdecode(transaction.destination(transaction, b'StandaloneBase')))
        (staged / 'StandaloneBaseMeta.lua').write_text('this is invalid Lua')
        with self.assertRaises(Exception): transaction.verify(transaction, b'StandaloneBase')
        (staged / 'StandaloneBaseMeta.lua').write_text('return {}')
        (staged / 'sbupnp').write_bytes(b'ELF fixture')
        with self.assertRaises(Exception): transaction.verify(transaction, b'StandaloneBase')
        assert 'old' in (old / 'StandaloneBaseMeta.lua').read_text()

    def test_changed_target_and_corrupt_backup_are_refused(self):
        old = self.applet('StandaloneBase')
        transaction = self.begin('StandaloneBase')
        self.stage(transaction, 'StandaloneBase')
        transaction.commit(transaction, self.convert({'StandaloneBase': {}}))
        self.options[b'quiescent'] = self.lua.eval(b'function()return true end')  # simulated external proof
        (old / 'user-new-file').write_text('do not erase')
        with self.assertRaisesRegex(Exception, 'Unknown current applet revision'): self.module.recover(os.fsencode(self.parent), self.options)
        assert (old / 'user-new-file').read_text() == 'do not erase'
        (old / 'user-new-file').unlink()
        backup = self.parent / '.standalone-install/old/StandaloneBase/StandaloneBaseMeta.lua'
        backup.write_text('corrupt')
        with self.assertRaisesRegex(Exception, 'Corrupt rollback applet'): self.module.recover(os.fsencode(self.parent), self.options)

    def test_committed_rollback_requires_independent_quiescence(self):
        old = self.applet('StandaloneBase')
        transaction = self.begin('StandaloneBase')
        self.stage(transaction, 'StandaloneBase')
        transaction.commit(transaction, self.convert({'StandaloneBase': {}}))
        with self.assertRaises(Exception): self.module.recover(os.fsencode(self.parent), self.options)
        self.options[b'quiescent'] = self.lua.eval(b'function()return true end')
        self.module.recover(os.fsencode(self.parent), self.options)
        assert 'old' in (old / 'StandaloneBaseMeta.lua').read_text()

    def run_stock_adapter(self, checksum_ok=True, disk_full=False, native_directory=None, package_path=None, platform_override=None, expect_failure=False, chmod_fail=False):
        old = self.applet('StandaloneBase')
        archive = io.BytesIO()
        with zipfile.ZipFile(archive,'w') as output:
            output.writestr('StandaloneBaseMeta.lua','return {version="new"}')
            output.writestr('asset.bin',bytes(range(256)))
        payload = archive.getvalue()
        if package_path:
            payload=Path(package_path).read_bytes()
        with zipfile.ZipFile(io.BytesIO(payload)) as source:
            expected_meta=source.read('StandaloneBaseMeta.lua')
        package = self.lua.globals().package.loaded
        package[b'lfs'] = self.options[b'fs']
        package[b'json'] = self.options[b'json']
        package[b'applets.StandaloneBase.InstallerTransaction'] = self.module
        package[b'sha1'] = self.lua.table_from({b'new':lambda _:self.hasher()})
        package[b'jive.utils.log'] = self.lua.eval(b'{logger=function()return {info=function()end,warn=function()end}end}')
        package[b'zipfilter'] = self.lua.eval(b'{filter=function()return function()end end}')
        if native_directory:
            import ctypes
            import lupa.lua51
            ctypes.CDLL(lupa.lua51.__file__,mode=ctypes.RTLD_GLOBAL)
            for name in ('zipfilter','sha1'):
                package[name.encode()]=self.lua.eval(b'function(path,name)return assert(package.loadlib(path,name))() end')(
                    os.fsencode(Path(native_directory)/'modules'/(name+'.so')),('luaopen_'+name).encode())
            self.lua.execute((Path(native_directory)/'src/luasocket-2.0.2/src/ltn12.lua').read_bytes())
            if package_path:
                with zipfile.ZipFile(io.BytesIO(payload)) as source:
                    target=platform_override.encode() if platform_override else source.read('build-target').strip()
                package[b'applets.StandaloneBase.Platform']=self.lua.eval(b'function(name)return {detect=function()return {name=name}end} end')(target)

        def chain(_, sink):
            def accept(chunk):
                if chunk is None:
                    sink(None)
                else:
                    with zipfile.ZipFile(io.BytesIO(chunk)) as source:
                        for name in source.namelist():
                            sink(self.convert({'filename':name}))
                            sink(source.read(name))
                return 1
            return accept

        if not native_directory:
            package[b'ltn12'] = self.lua.table_from({b'sink':self.lua.table_from({b'chain':chain})})
        package[b'jive.net.RequestHttp'] = self.lua.eval(b'function(sink) return {sink=sink,getURI=function()return {host="fixture",port=80}end}end')

        def fetch(_, request):
            if native_directory:
                for at in range(0,len(payload),1301): request[b'sink'](payload[at:at+1301])
            else:
                request[b'sink'](payload)
            request[b'sink'](None)

        socket = self.lua.table_from({b'fetch':fetch})
        package[b'jive.net.SocketHttp'] = self.lua.eval(b'function(value)return function()return value end end')(socket)
        if chmod_fail:
            self.lua.execute(b'local execute=os.execute;os.execute=function(command) if command:find("chmod 755 ",1,true) then return 1 end;return execute(command) end')
        adapter = self.lua.execute((ROOT / 'applet/InstallerCompat.lua').read_bytes())
        finished, errors = [], []
        instance = self.lua.eval(b'{appletInstallerMenu=function()end,_download=function()error("old destructive download")end,_remove=function()error("old destructive removal")end,_zipSink=function()end,task={yield=function()error("synchronous fixture must not yield")end}}')
        instance[b'appletdir'] = os.fsencode(self.parent)
        instance[b'toremove'] = self.convert({'StandaloneBase':True})
        instance[b'todownload'] = self.convert({'StandaloneBase':{'url':'fixture','sha':hashlib.sha1(payload).hexdigest() if checksum_ok else '0'*40}})
        instance[b'_finished'] = self.function(lambda *_: finished.append(True))
        selected_io = self.options[b'io']
        if disk_full:
            original_open=selected_io[b'open']
            def opened(path,mode):
                if mode==b'wb' and b'/.standalone-install/new/' in path:
                    return None
                return original_open(path,mode)
            selected_io=self.lua.table_from({b'open':opened})
        assert adapter.apply(instance,self.lua.table_from({b'fs':self.options[b'fs'],b'io':selected_io,b'showError':lambda message:errors.append(message)}))
        instance[b'_remove'](instance)
        assert 'old' in (old/'StandaloneBaseMeta.lua').read_text()
        instance[b'_download'](instance)
        assert 'old' in (old/'StandaloneBaseMeta.lua').read_text(), 'download replaced active tree'
        instance[b'_finished'](instance)
        if checksum_ok and not disk_full and not expect_failure:
            assert finished and not errors and (old/'StandaloneBaseMeta.lua').read_bytes()==expected_meta
            if package_path:
                for binary in ('sbbase','sbwebserver','sbproxy','sbvpn','sb-tinysc-control','sb-storage-helper','sb-storage-updater','svgpngrenderer','sbupnp','sbbase-network','sbwebserver-network','storage-setup.sh','storage-boot.sh','patch-squeezeplay-artwork.sh'):
                    assert (old/binary).stat().st_mode & 0o777 == 0o755, binary

        else:
            assert not finished and errors and 'old' in (old/'StandaloneBaseMeta.lua').read_text()

    def test_actual_stock_adapter_stages_verified_archive_before_commit(self):
        self.run_stock_adapter()

    def test_actual_stock_adapter_checksum_failure_retains_old_tree(self):
        self.run_stock_adapter(checksum_ok=False)

    def test_actual_stock_adapter_disk_full_retains_old_tree(self):
        self.run_stock_adapter(disk_full=True)


if __name__ == '__main__':
    unittest.main()
