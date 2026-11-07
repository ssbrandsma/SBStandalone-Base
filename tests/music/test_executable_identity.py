"""Kernel executable proof and controlled fault models; no physical device."""
import hashlib,json,os,shutil,subprocess,sys,tempfile,time,unittest
from pathlib import Path
HELPER,FIXTURE=map(lambda p:str(Path(p).resolve()),sys.argv[1:3]);del sys.argv[1:3]

class Executable(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
  self.app=self.root/'usr/share/jive/applets/StandaloneBase';self.app.mkdir(parents=True)
  self.stage=self.root/'stage';self.stage.mkdir(mode=0o700)
  self.env=dict(os.environ,SB_TEST_BASE_ROOT=str(self.app),SB_TEST_INSTALL_TARGET=str(self.root),SB_TEST_INSTALL_STAGE=str(self.stage))
  self.path=self.app/'sbbase-music';shutil.copyfile(FIXTURE,self.path);self.path.chmod(0o700)
  self.sha=hashlib.sha256(self.path.read_bytes()).hexdigest();self.size=self.path.stat().st_size
  self.p=subprocess.Popen([str(self.path),'daemon'],stdout=subprocess.PIPE)
  self.assertEqual(self.p.stdout.readline(),b'ready\n')
  self.start=Path(f'/proc/{self.p.pid}/stat').read_text().rsplit(')',1)[1].split()[19]
 def tearDown(self):
  if self.p.poll() is None:self.p.terminate()
  self.p.wait(timeout=3);self.p.stdout.close();self.tmp.cleanup()
 def command(self,**changes):
  args=dict(pid=self.p.pid,start=self.start,parent=os.getpid(),size=self.size,sha=self.sha);args.update(changes)
  return [HELPER,'inspect-exe',*[str(args[k]) for k in ('pid','start','parent','size','sha')]]
 def inspect(self,fault=None,**changes):
  env=dict(self.env)
  if fault:env['SB_TEST_EXE_FAULT']=fault
  p=subprocess.run(self.command(**changes),env=env,capture_output=True,text=True,timeout=5)
  v=json.loads(p.stdout);self.assertEqual(p.returncode,0 if v['allowed'] else 2);return v
 def test_normal_live_hash_and_parent(self):
  v=self.inspect();self.assertEqual(v['level'],1);self.assertEqual(v['sha256'],self.sha);self.assertEqual(v['bytes_hashed'],self.size)
 def test_readlink_failure_is_distinct(self):
  v=self.inspect('readlink');self.assertEqual(v['failure'],'readlink');self.assertEqual(v['readlink_errno'],13)
 def test_readlink_success_open_failure_is_not_path_hash_proof(self):
  v=self.inspect('open');self.assertEqual(v['failure'],'proc_exe_open');self.assertEqual(v['open_errno'],2);self.assertEqual(v['level'],3)
 def test_open_success_read_failure(self):
  v=self.inspect('read');self.assertEqual(v['failure'],'read');self.assertEqual(v['read_errno'],5)
 def test_hash_size_parent_and_start_mismatch(self):
  for args,reason in ((dict(sha='0'*64),'artifact_mismatch'),(dict(size=self.size+1),'artifact_mismatch'),
                      (dict(parent=1),'supervisor_mismatch'),(dict(start=int(self.start)+1),'start_mismatch')):
   self.assertEqual(self.inspect(**args)['failure'],reason)
 def test_unknown_role_is_refused(self):
  other=self.app/'foreign';self.path.rename(other)
  self.assertEqual(self.inspect()['failure'],'unknown_role')
 def pin(self,action,fault=None):
  env=dict(self.env,SB_TEST_EXE_PAUSE_MS='250')
  if fault:env['SB_TEST_EXE_FAULT']=fault
  command=self.command();command[1]='inspect-pin'
  p=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,text=True)
  self.assertEqual(p.stdout.readline(),'PIN_READY\n');action()
  v=json.loads(p.stdout.readline());code=p.wait(timeout=5);p.stdout.close();self.assertEqual(code,0 if v['allowed'] else 2);return v
 def test_atomic_rename_keeps_original_live_image_not_current_path(self):
  def replace():
   new=self.stage/'candidate';new.write_bytes(b'unrelated candidate');os.replace(new,self.path)
  v=self.pin(replace);self.assertEqual(v['level'],1);self.assertEqual(v['sha256'],self.sha);self.assertEqual(v['file_nlink'],0)
 def test_unlink_keeps_kernel_correlated_live_descriptor(self):
  self.assertEqual(self.pin(self.path.unlink)['level'],1)
 def test_unionfs_correlation_failure_refuses_even_with_pinned_hash(self):
  v=self.pin(lambda:None,'correlation');self.assertEqual(v['stat_errno'],2);self.assertEqual(v['sha256'],self.sha)
  self.assertEqual(v['level'],3);self.assertFalse(v['level2_enabled'])
 def test_process_exit_during_pin_refuses(self):
  v=self.pin(lambda:(self.p.terminate(),self.p.wait(timeout=3)));self.assertEqual(v['level'],3)
 def test_copyup_mapping_cannot_substitute_current_path_inode(self):
  v=self.inspect('mapping');self.assertEqual(v['failure'],'mapping_inode_mismatch');self.assertEqual(v['level'],3)
 def test_active_executable_copy_and_remove_are_refused(self):
  source=self.stage/'new';source.write_bytes(b'candidate')
  for args in (['install-file',str(source),str(self.path),'448',self.sha],['remove-file',str(self.path)],['check-inactive',str(self.path)]):
   r=subprocess.run([HELPER,*args],env=self.env,capture_output=True);self.assertEqual(r.returncode,2)
  inactive=subprocess.run([HELPER,'check-inactive',str(self.app/'sbbase-core')],env=self.env,capture_output=True)
  self.assertEqual(inactive.returncode,0,inactive.stderr)
  self.assertEqual(hashlib.sha256(self.path.read_bytes()).hexdigest(),self.sha)
 def test_generation_artifacts_are_write_once(self):
  directory=self.app/'generations'/('a'*64);directory.mkdir(parents=True)
  target=directory/'sbwebserver-music';source=self.stage/'new';source.write_bytes(b'candidate')
  args=[HELPER,'install-file',str(source),str(target),'448','absent']
  self.assertEqual(subprocess.run(args,env=self.env,capture_output=True).returncode,0)
  self.assertEqual(subprocess.run(args,env=self.env,capture_output=True).returncode,2)

if __name__=='__main__':unittest.main()
