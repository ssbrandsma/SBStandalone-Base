import http.client
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
for arguments in ([sys.argv[1]],[sys.argv[2]],[sys.argv[2],'--test-music-no-storage','--test-music-db','/tmp/unapproved-do-not-open.db']):
    with tempfile.TemporaryFile() as log:
        p=subprocess.Popen(arguments,stderr=log)
        try:
            for _ in range(100):
                try:
                    c=http.client.HTTPConnection('127.0.0.1',9000,timeout=1);c.request('GET','/health');r=c.getresponse();r.read();c.close();assert r.status==200;break
                except OSError:time.sleep(.02)
            status=Path(f'/proc/{p.pid}/status').read_text();assert '\nThreads:\t1\n' in status
            # /health's accepted socket can remain until the next server tick.
            # Wait for cleanup before checking that no library descriptors exist.
            deadline=time.monotonic()+1
            while len(list(Path(f'/proc/{p.pid}/fd').iterdir()))!=6 and time.monotonic()<deadline:time.sleep(.01)
            assert len(list(Path(f'/proc/{p.pid}/fd').iterdir()))==6
            c=http.client.HTTPConnection('127.0.0.1',9000,timeout=2)
            message=[{'channel':'/slim/request','clientId':'disabled','data':{'request':['',['menu',0,100]],'response':'/disabled/reply'}}]
            c.request('POST','/cometd',json.dumps(message));r=c.getresponse();data=json.loads(r.read());c.close()
            assert data[1]['data']['count']==2 and all('myMusic' not in x['id'] for x in data[1]['data']['item_loop'])
        finally:p.terminate();p.wait(timeout=4)
print('Production, test-disabled and Baby no-storage: no library/menu/worker activated')
