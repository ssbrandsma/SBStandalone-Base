"""Real executable roles/listener inodes; temporary processes, no audio/device."""
import hashlib,json,os,shutil,socket,subprocess,sys,tempfile,time,unittest
from pathlib import Path
HELPER,FIXTURE=map(lambda p:str(Path(p).resolve()),sys.argv[1:3]);del sys.argv[1:3]
class Stack(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='sb-stack-');self.root=Path(self.tmp.name)
  self.started=[];self.env=dict(os.environ,SB_TEST_BASE_ROOT=str(self.root),SB_TEST_JIVE=str(self.root/'jive'))
  holders=[socket.socket() for _ in range(4)]
  for sock in holders:sock.bind(('127.0.0.1',0))
  self.ports=[sock.getsockname()[1] for sock in holders]
  for sock in holders:sock.close()
  self.env['SB_TEST_STACK_PORTS']=','.join(map(str,self.ports))
  for name in ('jive','sbbase','sbbase-core','sbbase-music','sbproxy','sbwebserver','sbwebserver-music','sbmusic-session','foreign'):
   shutil.copyfile(FIXTURE,self.root/name);(self.root/name).chmod(0o700)
  self.jive=self.start('jive','daemon')
  self.start_time=Path(f'/proc/{self.jive.pid}/stat').read_text().rsplit(')',1)[1].split()[19]
 def start(self,name,*args):
  p=subprocess.Popen([str(self.root/name),*map(str,args)],stdout=subprocess.PIPE,env=self.env);self.started.append(p)
  self.assertEqual(p.stdout.readline(),b'ready\n');return p
 def tearDown(self):
  for path in Path('/proc').glob('[0-9]*/exe'):
   try:
    if os.readlink(path).startswith(str(self.root)+'/'):os.kill(int(path.parent.name),9)
   except (OSError,ValueError):pass
  for p in self.started:p.wait(timeout=3);p.stdout.close()
  self.tmp.cleanup()
 def verify(self):
  return subprocess.run([HELPER,'verify-stack',str(self.jive.pid),self.start_time],env=self.env,capture_output=True,text=True,timeout=3)
 def topology(self,base='sbbase-music',web='sbwebserver'):
  a,b,p,w=self.ports
  self.start(base,'listen',f'{a},{b}',a)
  self.start('sbproxy','listen',p,0)
  self.start(web,'listen',w,0)
 def test_current_browse_and_core_roles(self):
  self.topology('sbbase-core');r=self.verify();self.assertEqual(r.returncode,0,r.stderr);self.assertIn('base_pid',json.loads(r.stdout))
 def test_current_music_role(self):
  self.topology();self.assertEqual(self.verify().returncode,0)
 def test_duplicate_base_alias_is_refused(self):
  self.topology();self.start('sbbase-core','daemon');self.assertEqual(self.verify().returncode,2)
 def test_foreign_listener_is_refused(self):
  self.topology(base='foreign');self.start('sbbase-music','daemon');self.assertEqual(self.verify().returncode,2)
 def test_missing_listener_is_refused(self):
  self.topology();self.started[-1].kill();self.started[-1].wait();self.assertEqual(self.verify().returncode,2)
 def test_pid_presence_without_listeners_is_refused(self):
  for name in ('sbbase-music','sbproxy','sbwebserver'):self.start(name,'daemon')
  self.assertEqual(self.verify().returncode,2)
 def test_live_executable_receipt_and_stale_identity(self):
  p=self.start('sbbase-music','daemon')
  start=Path(f'/proc/{p.pid}/stat').read_text().rsplit(')',1)[1].split()[19]
  digest=hashlib.sha256(Path(FIXTURE).read_bytes()).hexdigest()
  def verify(stamp,sha):
   return subprocess.run([HELPER,'verify-exe',str(p.pid),str(stamp),sha],env=self.env,capture_output=True).returncode
  self.assertEqual(verify(start,digest),0)
  self.assertEqual(verify(int(start)+1,digest),2)
  self.assertEqual(verify(start,'0'*64),2)
  # Replacing the on-disk path must not hide the old running inode.
  (self.root/'sbbase-music').unlink();(self.root/'sbbase-music').write_bytes(b'new revision')
  self.assertEqual(verify(start,digest),0)
  self.assertEqual(verify(start,hashlib.sha256(b'new revision').hexdigest()),2)
 def test_session_children_are_required(self):
  self.topology(web='sbwebserver-music');self.start('sbmusic-session','daemon');self.assertEqual(self.verify().returncode,2)
 def test_real_session_parent_child_relationship(self):
  a,b,p,w=self.ports
  self.start('sbproxy','listen',p,0)
  self.start('sbmusic-session','session',self.root/'sbbase-music',self.root/'sbwebserver-music',f'{a},{b}',w)
  until=time.monotonic()+3
  while True:
   r=self.verify()
   if r.returncode==0:
    self.assertNotEqual(json.loads(r.stdout)['session_pid'],0);break
   self.assertLess(time.monotonic(),until,r.stderr);time.sleep(.02)
 def generation(self,key):
  directory=self.root/'generations'/(key*64);directory.mkdir(parents=True)
  for name in ('sbbase-music','sbwebserver-music','sbmusic-session'):
   shutil.copyfile(FIXTURE,directory/name);(directory/name).chmod(0o700)
  return directory
 def test_versioned_generation_topology(self):
  directory=self.generation('a');a,b,p,w=self.ports
  self.start('sbproxy','listen',p,0)
  self.start(str(directory.relative_to(self.root)/'sbmusic-session'),'session',directory/'sbbase-music',directory/'sbwebserver-music',f'{a},{b}',w)
  until=time.monotonic()+3
  while self.verify().returncode:
   self.assertLess(time.monotonic(),until);time.sleep(.02)
 def test_mixed_generation_is_refused(self):
  first,second=self.generation('a'),self.generation('b');a,b,p,w=self.ports
  self.start('sbproxy','listen',p,0)
  self.start(str(first.relative_to(self.root)/'sbmusic-session'),'session',first/'sbbase-music',second/'sbwebserver-music',f'{a},{b}',w)
  self.assertEqual(self.verify().returncode,2)
if __name__=='__main__':unittest.main()
