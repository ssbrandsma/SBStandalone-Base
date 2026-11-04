"""Actual Linux fork/PID-file/IPC tests in an isolated host container.

No physical watchdog, device, decoder or playback API is used.
"""
import concurrent.futures
import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

TEST,PRODUCTION,JIVE=map(lambda value:str(Path(value).resolve()),sys.argv[1:4])
del sys.argv[1:4]

class Primitives(unittest.TestCase):
    def test_high_inherited_descriptor_is_closed_before_detached_exec(self):
        path=Path(self.folder.name)/'foreign-fd'
        fd=os.open(path,os.O_RDWR|os.O_CREAT,0o600)
        high=os.dup2(fd,4096,inheritable=True)
        try:
            result=subprocess.run([TEST,'launch-hold'],env=self.environment,
                                  pass_fds=(high,),capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,0,result.stderr)
            value=json.loads(result.stdout)
            self.started.append(value['pid'])
            self.assertFalse(Path('/proc',str(value['pid']),'fd','4096').exists())
        finally:
            os.close(high)
            os.close(fd)

    def setUp(self):
        self.folder=tempfile.TemporaryDirectory(prefix='sb-maintenance-')
        self.pidfile=Path(self.folder.name)/'squeezeplay.pid'
        self.environment=dict(os.environ,SB_TEST_PIDFILE=str(self.pidfile),SB_TEST_JIVE=JIVE)
        self.started=[]
    def tearDown(self):
        for pid in self.started:
            try:os.kill(pid,signal.SIGTERM)
            except ProcessLookupError:pass
        self.folder.cleanup()
    def call(self,*args,expected=0,binary=TEST):
        result=subprocess.run([binary,*map(str,args)],env=self.environment,
                              capture_output=True,text=True,timeout=5)
        self.assertEqual(result.returncode,expected,(args,result.stdout,result.stderr))
        return json.loads(result.stdout) if result.stdout else None
    def launch(self,command='launch-hold'):
        identity=self.call(command)
        self.started.append(identity['pid'])
        return identity
    def publish(self,identity):
        self.pidfile.write_text(str(identity['pid'])+'\n')
        self.pidfile.chmod(0o644)
    def swap(self,old,new,expected=0):
        return self.call('pid-swap',old['pid'],old['start'],new['pid'],new['start'],expected=expected)
    def wait_gone(self,identity):
        until=time.monotonic()+2
        while time.monotonic()<until:
            result=subprocess.run([TEST,'identity',str(identity['pid'])],capture_output=True)
            if result.returncode==2:return
            time.sleep(.02)
        self.fail('fixture process did not terminate')
    def test_detached_holder_survives_launcher_exit(self):
        identity=self.launch()
        self.assertEqual(os.getsid(identity['pid']),identity['pid'])
        for fd in range(3):
            self.assertEqual(os.readlink(f'/proc/{identity["pid"]}/fd/{fd}'),'/dev/null')
        again=self.call('identity',identity['pid'])
        self.assertEqual(again['start'],identity['start'])
    def test_hup_does_not_destroy_holder(self):
        identity=self.launch()
        os.kill(identity['pid'],signal.SIGHUP)
        time.sleep(.02)
        self.assertEqual(self.call('identity',identity['pid'])['start'],identity['start'])
    def test_jive_exec_is_independent_and_environment_is_explicit(self):
        identity=self.launch('launch-jive')
        self.assertEqual(identity['exe'],JIVE)
        self.assertEqual(os.getsid(identity['pid']),identity['pid'])
        environ=Path(f'/proc/{identity["pid"]}/environ').read_bytes().split(b'\0')
        self.assertIn(b'SQUEEZEPLAY_HOME=/etc/squeezeplay',environ)
        self.assertIn(b'SDL_NOMOUSE=1',environ)
        self.assertFalse(any(item.startswith(b'SB_TEST_') for item in environ))
    def test_pid_handoff_and_normal_restoration(self):
        old=self.launch('launch-jive');holder=self.launch()
        self.publish(old)
        self.swap(old,holder)
        self.assertEqual(int(self.pidfile.read_text()),holder['pid'])
        self.call('terminate',old['pid'],old['start']);self.wait_gone(old)
        new=self.launch('launch-jive')
        self.swap(holder,new)
        self.call('terminate',holder['pid'],holder['start']);self.wait_gone(holder)
        self.assertEqual(int(self.pidfile.read_text()),new['pid'])
        self.assertEqual(self.pidfile.stat().st_mode&0o777,0o644)
    def test_duplicate_handoffs_have_one_winner(self):
        old=self.launch('launch-jive');a=self.launch();b=self.launch();self.publish(old)
        def compete(new):
            return subprocess.run([TEST,'pid-swap',str(old['pid']),old['start'],
                                   str(new['pid']),new['start']],env=self.environment,
                                  capture_output=True,timeout=5).returncode
        with concurrent.futures.ThreadPoolExecutor(2) as executor:
            results=list(executor.map(compete,[a,b]))
        self.assertEqual(sorted(results),[0,2])
        self.assertIn(int(self.pidfile.read_text()),[a['pid'],b['pid']])
    def test_stale_start_time_cannot_signal(self):
        identity=self.launch()
        self.call('terminate',identity['pid'],str(int(identity['start'])+1),expected=2)
        self.assertEqual(self.call('identity',identity['pid'])['start'],identity['start'])
    def test_dead_replacement_preserves_live_watched_pid(self):
        old=self.launch('launch-jive');holder=self.launch();self.publish(old)
        self.call('terminate',holder['pid'],holder['start']);self.wait_gone(holder)
        self.swap(old,holder,expected=2)
        self.assertEqual(int(self.pidfile.read_text()),old['pid'])
    def test_stale_pidfile_is_not_guessed(self):
        old=self.launch('launch-jive');holder=self.launch();self.publish(old)
        self.pidfile.write_text('2147483000\n')
        self.swap(old,holder,expected=2)
        self.assertEqual(self.pidfile.read_text(),'2147483000\n')
    def test_emergency_recovery_requires_dead_holder_and_exact_replacement(self):
        old=self.launch('launch-jive');holder=self.launch();self.publish(old)
        self.swap(old,holder)
        self.call('pid-recover',holder['pid'],holder['start'],old['pid'],old['start'],expected=2)
        self.call('terminate',holder['pid'],holder['start']);self.wait_gone(holder)
        self.call('pid-recover',holder['pid'],holder['start'],old['pid'],str(int(old['start'])+1),expected=2)
        self.assertEqual(int(self.pidfile.read_text()),holder['pid'])
        self.call('pid-recover',holder['pid'],holder['start'],old['pid'],old['start'])
        self.assertEqual(int(self.pidfile.read_text()),old['pid'])
    def test_recovery_cannot_steal_live_pid_or_use_zero_identity(self):
        old=self.launch('launch-jive');new=self.launch();self.publish(old)
        self.call('pid-recover',old['pid'],old['start'],new['pid'],new['start'],expected=2)
        self.call('pid-recover',old['pid'],'0',new['pid'],new['start'],expected=2)
        self.assertEqual(int(self.pidfile.read_text()),old['pid'])
    def test_foreign_process_cannot_become_watched_jive(self):
        old=self.launch('launch-jive');self.publish(old)
        process=subprocess.Popen(['/bin/sleep','10'])
        try:
            foreign=self.call('identity',process.pid)
            self.swap(old,foreign,expected=2)
            self.assertEqual(int(self.pidfile.read_text()),old['pid'])
        finally:process.terminate();process.wait(timeout=5)
    def test_corrupt_or_writable_pidfile_is_refused(self):
        old=self.launch('launch-jive');new=self.launch();self.publish(old)
        self.pidfile.chmod(0o666);self.swap(old,new,expected=2)
        self.pidfile.chmod(0o644);self.pidfile.write_text('not a PID\n')
        self.swap(old,new,expected=2)
    def test_symlink_pidfile_is_refused(self):
        old=self.launch('launch-jive');new=self.launch()
        target=Path(self.folder.name)/'other';target.write_text(str(old['pid']))
        self.pidfile.symlink_to(target)
        self.swap(old,new,expected=2)
        self.assertEqual(target.read_text(),str(old['pid']))
    def test_crash_after_handoff_leaves_holder_alive(self):
        old=self.launch('launch-jive');holder=self.launch();self.publish(old)
        self.swap(old,holder)
        os.kill(old['pid'],signal.SIGKILL);self.wait_gone(old)
        new=self.launch('launch-jive');os.kill(new['pid'],signal.SIGKILL);self.wait_gone(new)
        self.swap(holder,new,expected=2)
        self.assertEqual(int(self.pidfile.read_text()),holder['pid'])
        self.assertEqual(self.call('identity',holder['pid'])['start'],holder['start'])
    def test_production_has_no_test_path_override_or_host_mutation(self):
        self.call('launch-hold',expected=2,binary=PRODUCTION)
        self.assertNotIn(b'SB_TEST_',Path(PRODUCTION).read_bytes())
        self.assertFalse(self.pidfile.exists())
    def test_metadata_audit_preserves_directory_and_file_atime(self):
        root=Path(self.folder.name)/'media';cache=root/'.Squeezebox'/'cache'
        cache.mkdir(parents=True)
        file=root/'user music.mp3';file.write_bytes(b'not opened or parsed')
        (cache/'existing').write_bytes(b'unchanged')
        for path in [root,root/'.Squeezebox',cache,file]:os.utime(path,(100000,100000))
        self.environment['SB_TEST_MEDIA']=str(root)
        observed=self.call('usb-audit')
        self.assertTrue(observed['complete'])
        self.assertEqual({v['path'] for v in observed['entries']},
                         {'.Squeezebox','user music.mp3','.Squeezebox/cache','.Squeezebox/cache/existing'})
        for path in [root,root/'.Squeezebox',cache,file]:
            self.assertEqual(path.stat().st_atime,100000)
    def test_audit_rejects_symlink_escape(self):
        root=Path(self.folder.name)/'media';outside=Path(self.folder.name)/'outside'
        root.mkdir();outside.mkdir();(root/'.Squeezebox').symlink_to(outside)
        self.environment['SB_TEST_MEDIA']=str(root)
        result=subprocess.run([TEST,'usb-audit'],env=self.environment,capture_output=True,text=True,timeout=5)
        self.assertEqual(result.returncode,2)
        self.assertFalse(json.loads(result.stdout)['complete'])
    def test_audit_is_bounded_and_reports_missing_cache(self):
        root=Path(self.folder.name)/'media';root.mkdir()
        self.environment['SB_TEST_MEDIA']=str(root)
        observed=self.call('usb-audit')
        self.assertTrue(observed['missing_squeezebox']);self.assertTrue(observed['missing_cache'])
        for n in range(513):(root/str(n)).touch()
        result=subprocess.run([TEST,'usb-audit'],env=self.environment,capture_output=True,text=True,timeout=5)
        self.assertEqual(result.returncode,2)
        self.assertFalse(json.loads(result.stdout)['complete'])

class IPC(unittest.TestCase):
    def test_readonly_existing_semaphore_set(self):
        libc=ctypes.CDLL(None,use_errno=True)
        libc.semget.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.c_int]
        # Docker IPC namespace is private; never modify an existing set.
        self.assertEqual(libc.semget(1234,0,0),-1)
        before=subprocess.run([TEST,'sem'],capture_output=True,timeout=5)
        self.assertEqual(before.returncode,2)
        identifier=libc.semget(1234,20,0o600|0o1000|0o2000)
        self.assertGreaterEqual(identifier,0)
        values=(ctypes.c_ushort*20)(*[1,0]*10)
        try:
            self.assertEqual(libc.semctl(identifier,0,17,ctypes.byref(values)),0) # SETALL
            result=subprocess.run([TEST,'sem'],capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,0,result.stderr)
            observed=json.loads(result.stdout)
            self.assertEqual([v['value'] for v in observed['slots']],list(values))
            after=(ctypes.c_ushort*20)()
            self.assertEqual(libc.semctl(identifier,0,13,ctypes.byref(after)),0) # GETALL
            self.assertEqual(list(after),list(values))
        finally:self.assertEqual(libc.semctl(identifier,0,0),0) # IPC_RMID

if __name__=='__main__':unittest.main()
