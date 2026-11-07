"""Exercise the real session argument routing with an inert executable."""
import json,subprocess,sys,tempfile
from pathlib import Path
binary=str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='sb-store-launch-') as temporary:
 root=Path(temporary);capture=root/'argv.txt';base=root/'base'
 base.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > '+str(capture)+'\n');base.chmod(0o700)
 args=[binary,'--base',str(base),'--root','/media/test','--catalog',str(root/'config.json'),'--store','.standalone-music']
 p=subprocess.run(args,capture_output=True,timeout=10);assert p.returncode==0,p.stderr
 routed=capture.read_text().splitlines()
 assert '--music-open' in routed and '--music-browse-only' in routed and '--music-store' in routed
 assert not any(x in routed for x in ('--music-scan','--ownership-token','--music-resource-fd','--music-http-port'))
 for additional in (['--token','forbidden'],['--web',str(base)],['--database-limit-mib','0'],['--database-limit-mib','65'],['--database-limit-mib','bad']):
  capture.unlink(missing_ok=True)
  p=subprocess.run([*args,*additional],capture_output=True,timeout=10)
  assert p.returncode==2 and not capture.exists(),(additional,p.returncode,p.stderr)
 print(json.dumps(dict(browse_store_routing='passed',ownership_mixing_refused=True)))
