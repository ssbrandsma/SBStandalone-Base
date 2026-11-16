"""Verify LAN selection when a tunnel owns the default route, in a private netns only."""
import os
import subprocess
import sys

if os.geteuid() or os.readlink('/proc/self/ns/net') == os.readlink('/proc/1/ns/net'):
    print('Requires root in an isolated network namespace; host routes left untouched')
    raise SystemExit(77)

fixture = sys.argv[1]
baseline = subprocess.check_output([fixture], text=True).strip()
created = False
try:
    subprocess.run(['ip', 'link', 'add', 'tun0', 'type', 'dummy'], check=True)
    created = True
    subprocess.run(['ip', 'addr', 'add', '10.8.0.2/24', 'dev', 'tun0'], check=True)
    subprocess.run(['ip', 'link', 'set', 'tun0', 'up', 'multicast', 'on'], check=True)
    subprocess.run(['ip', 'route', 'replace', 'default', 'dev', 'tun0'], check=True)
    assert subprocess.check_output([fixture], text=True).strip() == baseline
    assert subprocess.run([fixture, 'tun0'], capture_output=True).returncode == 1
    print('UPnP retains its LAN interface when the VPN takes the default route')
finally:
    if created:
        subprocess.run(['ip', 'route', 'replace', 'default', 'dev', baseline], check=True)
        subprocess.run(['ip', 'link', 'delete', 'tun0'], check=True)
