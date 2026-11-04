"""Fab4 reliability recovery. No service start/stop/reboot is performed.

Backup and verify use read-only SSH. Restore preserves installed TinySC guards
by default. Installation is refused while qualification.json is unqualified.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import secrets
import shlex
import tarfile

PACKAGE = Path(__file__).resolve().parents[1] / 'packaging' / 'fab4-reliability'
POLICY = json.loads((PACKAGE / 'qualification.json').read_text())
TINY = set(POLICY['targets'])
APPS = {'/usr/share/jive/applets/'+name+'/'+name+'Meta.lua' for name in
        ('StandaloneBase','StandaloneRadio','SpotifyConnect')}
APPS |= {'/usr/share/jive/applets/StandaloneBase/'+name for name in ('sbbase','sbwebserver','sbproxy')}
APPS.add('/mnt/storage/standalonebase/config.json')
ALLOWED = TINY | APPS

def sha(data):
    return hashlib.sha256(data).hexdigest()

def installation_gate():
    if not POLICY.get('qualified') or not POLICY.get('hardware_installation_allowed'):
        raise RuntimeError('INSTALLATION REFUSED: '+POLICY['blocker'])

class SSH:
    def __init__(self, host):
        import paramiko
        self.client = paramiko.SSHClient()
        self.client.load_system_host_keys()
        self.client.load_host_keys(str(Path.home()/'.ssh/known_hosts'))
        self.client.set_missing_host_key_policy(paramiko.RejectPolicy())
        self.client.connect(host, username='root', password=os.environ.get('SB_DEVICE_PASSWORD'),
                            timeout=10, auth_timeout=15, look_for_keys=True)
    def close(self):
        self.client.close()
    def call(self, command, data=None):
        marker='__SB_DONE_'+secrets.token_hex(12)+':'
        wrapped='sh -c '+shlex.quote(command)+'; result=$?; printf '+shlex.quote('\n'+marker+'%d\n')+' "$result"'
        inp,out,err=self.client.exec_command(wrapped,timeout=20)
        if data is not None:
            inp.write(data)
            inp.channel.shutdown_write()
        value=out.read(8*1024*1024+1)
        error=err.read(65536)
        out.channel.recv_exit_status() # Legacy Dropbear may return -1; marker is authoritative.
        at=value.rfind(('\n'+marker).encode())
        if len(value)>8*1024*1024 or at<0:
            raise RuntimeError('missing/bounded SSH completion')
        code=int(value[at+len(marker)+1:].strip())
        if code:
            raise RuntimeError('remote operation failed: '+str(code)+' '+error.decode(errors='replace'))
        return value[:at]
    def read(self,path):
        if path not in ALLOWED:
            raise ValueError('path outside recovery allowlist')
        return self.call('test ! -L '+shlex.quote(path)+' && cat '+shlex.quote(path))
    def profile(self):
        cpu=self.call('cat /proc/cpuinfo').decode()
        version=self.call('head -1 /etc/squeezeos.version').decode().strip()
        mounts=self.call('cat /proc/self/mountinfo').decode()
        if 'Logitech Fab4 Board' not in cpu or version!='7.7.3 r16676':
            raise RuntimeError('target profile mismatch')
        if not any(' / rw ' in line and ' - unionfs ' in line for line in mounts.splitlines()):
            raise RuntimeError('expected writable union overlay absent')
        return version,mounts
    def atomic(self,path,data,expected,mode,uid,gid):
        if path not in ALLOWED or sha(self.read(path))!=expected:
            raise RuntimeError('refuse foreign/stale file')
        if uid!=0 or gid!=0 or mode & ~0o777:
            raise RuntimeError('unexpected file metadata')
        temporary=path+'.sbrecovery-'+secrets.token_hex(8)
        quoted=shlex.quote(temporary)
        self.call('umask 077; set -C; cat > '+quoted,data)
        # Reading temporary paths is permitted only for this generated leaf.
        if self.call('cat '+quoted)!=data:
            raise RuntimeError('upload hash/read-back mismatch; original retained')
        self.call('chown 0:0 '+quoted+' && chmod '+oct(mode)[2:]+' '+quoted+
                  ' && mv -f '+quoted+' '+shlex.quote(path))
        if self.read(path)!=data:
            raise RuntimeError('restore verification failed')

def snapshot_load(directory):
    manifest=json.loads((directory/'files.json').read_text())
    records=[]
    seen=set()
    for row in manifest['files']:
        path=row['path']
        if path not in ALLOWED or path in seen or row.get('symlink') is not None:
            raise ValueError('invalid recovery manifest')
        seen.add(path)
        data=(directory/'files'/path.lstrip('/')).read_bytes()
        if sha(data)!=row['sha256'] or len(data)!=row['bytes']:
            raise ValueError('backup hash mismatch')
        records.append((row,data))
    return records

def usb_attached(mounts):
    return any(len(fields:=line.split())>6 and fields[4].startswith('/media/')
               for line in mounts.splitlines())

def restore(device,records,mounts,restore_tiny=False,ack=False):
    if restore_tiny and usb_attached(mounts) and not ack:
        raise RuntimeError('USB ATTACHED: restoring original TinySC files re-enables startup. '
                           'Explicit --ack-tinysc-with-usb is required; no files changed.')
    if restore_tiny:
        print('WARNING: original firmware TinySC startup will become available, including future reboots.')
    actions=[]
    # Verify every current version before any mutation; no blind overwrite.
    for row,data in records:
        path=row['path'];current=device.read(path)
        allowed={row['sha256']}
        if path in TINY:
            allowed.add(POLICY['sha256'][POLICY['targets'][path]])
        if sha(current) not in allowed:
            raise RuntimeError('unrecognized current file: '+path)
        if path in TINY and not restore_tiny:
            actions.append((path,'retained'));continue
        if current!=data:
            actions.append((path,(data,sha(current),int(row['mode'],0),row['uid'],row['gid'])))
    for path,action in actions:
        if action!='retained':device.atomic(path,*action)
    return {'guard_policy':'originals restored' if restore_tiny else 'existing guards retained',
            'changed':sum(action!='retained' for _,action in actions),
            'services_restarted':False}

def backup(device,directory):
    directory.mkdir(mode=0o700,parents=False,exist_ok=False)
    archive=device.call('tar -cf - '+' '.join(shlex.quote(p) for p in sorted(ALLOWED)))
    (directory/'originals.tar').write_bytes(archive)
    records=[]
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for member in source.getmembers():
            path='/'+member.name.lstrip('/')
            if path not in ALLOWED or not member.isfile():
                raise RuntimeError('unexpected archive member')
            data=source.extractfile(member).read()
            target=directory/'files'/path.lstrip('/');target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(data)
            records.append({'path':path,'bytes':len(data),'sha256':sha(data),'mode':oct(member.mode),
                            'uid':member.uid,'gid':member.gid,'symlink':None})
    if {r['path'] for r in records}!=ALLOWED:
        raise RuntimeError('incomplete backup')
    (directory/'files.json').write_text(json.dumps({'files':records,'archive_sha256':sha(archive)},indent=2)+'\n')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['backup','verify','restore','install'])
    parser.add_argument('--host',required=True)
    parser.add_argument('--snapshot',type=Path,required=True)
    parser.add_argument('--restore-tinysc',action='store_true')
    parser.add_argument('--ack-tinysc-with-usb',action='store_true')
    args=parser.parse_args()
    if args.command=='install':
        installation_gate() # Refuse BEFORE connecting or mutating.
        raise RuntimeError('No qualified installer is provided for this blocked prototype')
    device=SSH(args.host)
    try:
        version,mounts=device.profile()
        if args.command=='backup':backup(device,args.snapshot);print('Read-only device backup complete');return
        records=snapshot_load(args.snapshot)
        if args.command=='verify':
            differences=[r['path'] for r,data in records if device.read(r['path'])!=data]
            print(json.dumps({'firmware':version,'differences':differences,'device_modified':False}));return
        print(json.dumps(restore(device,records,mounts,args.restore_tinysc,args.ack_tinysc_with_usb)))
    finally:device.close()
if __name__=='__main__':
    try:main()
    except (RuntimeError,ValueError) as error:
        import sys
        print(str(error),file=sys.stderr)
        sys.exit(2)
