"""Readonly ARM IPC/profiling checks via existing Docker binfmt emulation."""
import ctypes
import json
from pathlib import Path
import subprocess
import sys

libc=ctypes.CDLL(None,use_errno=True)
libc.semget.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.c_int]
assert libc.semget(1234,0,0)==-1,'private IPC namespace required'
identifier=libc.semget(1234,20,0o600|0o1000|0o2000)
assert identifier>=0
values=(ctypes.c_ushort*20)(*[1,2,1,3]*5)
try:
    assert libc.semctl(identifier,0,17,ctypes.byref(values))==0
    for name in sys.argv[1:]:
        binary=str(Path(name).resolve())
        result=subprocess.run([binary,'sem'],capture_output=True,text=True,timeout=10)
        assert result.returncode==0,(name,result.stderr)
        observed=json.loads(result.stdout)
        assert [v['value'] for v in observed['slots']]==list(values),observed
        refusal=subprocess.run([binary,'launch-hold'],capture_output=True,timeout=10)
        assert refusal.returncode==2,'production mutation must reject non-Fab4 profile'
        print(Path(binary).parent.name+': ARM readonly20-slot IPC and profile refusal passed')
    after=(ctypes.c_ushort*20)()
    assert libc.semctl(identifier,0,13,ctypes.byref(after))==0
    assert list(after)==list(values),'probe must not mutate semaphores'
finally:
    assert libc.semctl(identifier,0,0)==0
