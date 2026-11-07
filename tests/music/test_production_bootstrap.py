"""Exercise production entry gates without mounting media or starting playback."""
from pathlib import Path
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile

base, web, launcher = map(Path, sys.argv[1:4])
def refused(binary, args, **kwargs):
    r = subprocess.run([str(binary), *args], capture_output=True, timeout=5, **kwargs)
    assert r.returncode == 2, (binary, args, r.returncode, r.stderr)

for flag in ('--test-music-db', '--test-music-root', '--test-music-mock-adapters',
             '--test-playback-ipc', '--test-nowplaying'):
    refused(base, [flag, '/tmp/forbidden'])
for args in ([], ['--music-root'], ['--music-scan', '--music-scan'],
             ['--music-idle-only'],['--music-idle-only','--music-idle-only'],
             ['--music-browse-only','--music-idle-only'],
             ['--music-idle-only','--music-scan-only','--ownership-token','/tmp/forbidden'],
             ['--music-browse-only'], ['--music-browse-only','--music-browse-only'],
             ['--music-browse-only','--ownership-token','/tmp/forbidden'],
             ['--music-browse-only','--music-scan-only'],
             ['--music-resource-fd', '2'], ['--music-http-port', '65536']):
    refused(base, args)
refused(web, ['--test-music-ipc', '3'])
refused(launcher, [])
refused(launcher, ['--base', '/bin/true', '--base', '/bin/true'])

with tempfile.TemporaryDirectory(prefix='sb-production-gates-') as directory:
    directory = Path(directory)
    os.chmod(directory, 0o700)
    token = directory / 'token'
    a, b = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    try:
        # A real authenticated bridge may be constructed, but a caller-supplied
        # ordinary directory cannot become a validated USB/RAM database root.
        refused(base, ['--ownership-token', str(token), '--music-root', str(directory),
                       '--music-database', str(directory / 'library.db'),
                       '--music-helper', '/bin/true', '--music-resource-fd', str(a.fileno()),
                       '--music-http-port', '80', '--music-scan'], pass_fds=(a.fileno(),))
        assert not token.exists(), 'failed startup must remove its capability'
        assert not (directory / 'library.db').exists()
    finally:
        a.close()
        b.close()

with tempfile.TemporaryDirectory(prefix='sb-production-session-') as directory:
    directory=Path(directory)
    os.chmod(directory,0o700)
    copies=[]
    for name,source in (('base',base),('web',web),('helper',Path('/bin/true'))):
        target=directory/name;shutil.copyfile(source,target);target.chmod(0o700);copies.append(target)
    port_socket=socket.socket();port_socket.bind(('127.0.0.1',0));port=port_socket.getsockname()[1];port_socket.close()
    catalog=directory/'catalog.json';catalog.write_text('{}')
    result=subprocess.run([str(launcher),'--base',str(copies[0]),'--web',str(copies[1]),
        '--helper',str(copies[2]),'--root',str(directory),'--database',str(directory/'library.db'),
        '--token',str(directory/'token'),'--catalog',str(catalog),'--web-config',str(directory),
        '--web-root',str(directory),'--port',str(port)],capture_output=True,text=True,timeout=10)
    children=re.search(r'MUSIC_SESSION base=(\d+) web=(\d+); no automatic restart',result.stderr)
    assert children,(result.returncode,result.stderr)
    assert result.returncode!=0 and not (directory/'token').exists(),result.stderr
    for pid in children.groups():
        assert not Path('/proc',pid).exists(),'partial startup leaked a session child'
    with socket.socket() as probe:
        assert probe.connect_ex(('127.0.0.1',port))!=0,'partial startup left a web listener'
print('Production binary rejects fixture flags, incomplete activation and unapproved roots; no database or capability remains')
