import hashlib,json,subprocess,sys,tempfile
from pathlib import Path
from wp2_fixtures import mp3
tool,helper,driver=map(lambda p:str(Path(p).resolve()),sys.argv[1:4])
with tempfile.TemporaryDirectory(dir='/dev/shm',prefix='sb-view-media-') as temporary:
 root=Path(temporary);(root/'one.mp3').write_bytes(mp3(4))
 def scan():
  p=subprocess.run([tool,'scan',str(root),'.standalone-music',helper,'4'],capture_output=True,text=True,timeout=20)
  assert p.returncode==0,(p.stdout,p.stderr)
 scan()
 process=subprocess.Popen([driver,str(root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 try:
  assert process.stdout.readline()=='READY\n'
  def command(text):
   process.stdin.write(text+'\n');process.stdin.flush()
   line=process.stdout.readline();assert line,(process.poll(),process.stderr.read())
   return json.loads(line)
  first=command('summary');assert first['status']==0 and first['tracks']==1
  unchanged=command('refresh');assert unchanged['status']==0 and unchanged['changed']==0
  (root/'two.mp3').write_bytes(mp3(3));scan()
  updated=command('refresh');assert updated['status']==0 and updated['changed']==1,updated
  assert command('summary')['tracks']==2
  assert command('offline')['status']==4
  assert command('summary')['status']==4
  online=command('online');assert online['status']==0 and online['generation']>updated['generation'],online
  assert command('summary')['tracks']==2
  assert command('foreign')['status']==4
  restored=command('online');assert restored['status']==0 and restored['generation']>online['generation']
  assert command('summary')['tracks']==2
  # A newly corrupted generation goes offline once; explicit recovery preserves
  # the corrupt bytes and reopens the verified previous generation without scan.
  store=root/'.standalone-music'
  latest=max(store.glob('*.snapshot'),key=lambda p:int(p.read_bytes()[:192].split()[1]))
  corrupt=bytearray(latest.read_bytes());corrupt[-1]^=255;latest.write_bytes(corrupt)
  # Trigger another generation hint while keeping the hash invalid.
  header=bytes(corrupt[:192]).split(b'\0',1)[0].decode().split();header[1]=str(int(header[1])+1)
  encoded=(' '.join(header)+'\n').encode();latest.write_bytes(encoded+bytes(192-len(encoded))+corrupt[192:])
  assert command('refresh')['status']==8
  assert command('summary')['status']==4
  fixed=subprocess.run([tool,'recover',str(root),'.standalone-music',helper,'4'],capture_output=True,text=True,timeout=20)
  assert fixed.returncode==0,(fixed.stdout,fixed.stderr)
  assert command('refresh')['status']==0
  assert command('summary')['tracks']==1
  command('exit') if False else None
  process.stdin.write('exit\n');process.stdin.flush();stdout,stderr=process.communicate(timeout=10)
  assert process.returncode==0,(stdout,stderr)
  print(json.dumps(dict(snapshot_refresh='passed',initial=first,updated=updated,reinsert=online,foreign_rejected=True)))
 finally:
  if process.poll() is None:process.kill();process.communicate()
