#!/usr/bin/env python3
"""Host SCM_RIGHTS feasibility only: no named socket, server or production IPC."""
import array
import fcntl
import os
from pathlib import Path
import socket
import tempfile

before=len(list(Path('/proc/self/fd').iterdir()))
for _ in range(100):
    left,right=socket.socketpair(socket.AF_UNIX,socket.SOCK_STREAM)
    with tempfile.TemporaryFile() as source:
        source.write(b'WP0 descriptor ownership');source.flush();source.seek(0)
        left.sendmsg([b'1'],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,array.array('i',[source.fileno()]))])
        payload,ancillary,flags,_=right.recvmsg(1,socket.CMSG_SPACE(array.array('i').itemsize))
        assert payload==b'1' and not flags & (socket.MSG_TRUNC|socket.MSG_CTRUNC)
        received=array.array('i');received.frombytes(ancillary[0][2][:received.itemsize])
        descriptor=received[0]
        try:
            # Explicit fallback avoids reliance on MSG_CMSG_CLOEXEC on old kernels.
            fcntl.fcntl(descriptor,fcntl.F_SETFD,fcntl.FD_CLOEXEC)
            assert os.pread(descriptor,24,0)==b'WP0 descriptor ownership'
            assert fcntl.fcntl(descriptor,fcntl.F_GETFD)&fcntl.FD_CLOEXEC
        finally:os.close(descriptor)
    left.close();right.close()
assert len(list(Path('/proc/self/fd').iterdir()))==before
print('100 descriptor transfers: no FD leak; host-kernel feasibility only')
