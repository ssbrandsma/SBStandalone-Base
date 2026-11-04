"""Verify/stage the captured hardware rollback without touching the device.

Only regular files listed in the capture manifest are accepted. Firmware
reference files can be verified but are never staged as restoration targets.
SSH atomic replacement and playback recovery must be validated separately.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile

ALLOWED = ('/usr/share/jive/applets/StandaloneBase/',
           '/usr/share/jive/applets/StandaloneRadio/',
           '/usr/share/jive/applets/SpotifyConnect/')
CONFIG = '/mnt/storage/standalonebase/config.json'
def verify(archive, manifest):
    raw = archive.read_bytes()
    if len(raw) != manifest['archive_bytes'] or hashlib.sha256(raw).hexdigest() != manifest['archive_sha256']:
        raise ValueError('archive length/hash mismatch')
    records = {}
    with tarfile.open(archive) as source:
        members = {m.name: m for m in source.getmembers() if m.isfile()}
        for row in manifest['files']:
            path = row['device_path']
            parts = PurePosixPath(path).parts
            if not path.startswith('/') or '..' in parts or path in records:
                raise ValueError('unsafe or duplicate captured path')
            member = members[path.lstrip('/')]
            data = source.extractfile(member).read()
            if len(data) != row['size'] or hashlib.sha256(data).hexdigest() != row['sha256']:
                raise ValueError('file length/hash mismatch: ' + path)
            records[path] = (row, data)
    return records

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--stage', type=Path, help='new local directory for known-good applet files')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    records = verify(args.archive, manifest)
    staged = []
    if args.stage:
        args.stage.mkdir(mode=0o700, parents=False, exist_ok=False)
        for path, (row, data) in records.items():
            if not (path.startswith(ALLOWED) or path == CONFIG):
                continue
            target = args.stage / path.lstrip('/')
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as output:
                output.write(data)
            target.chmod(row['mode'] & 0o777)
            staged.append(path)
    print(json.dumps({'verified': len(records), 'staged': len(staged), 'device_modified': False}))
if __name__ == '__main__':
    main()
