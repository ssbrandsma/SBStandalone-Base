"""Offline immutable generation receipts. No SSH, installation or service action.

Hardware admission is deliberately separate: a valid artifact receipt is not
proof of a running process. An unreadable current runtime cannot be grandfathered.
"""
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
APP='/usr/share/jive/applets/StandaloneBase'
ROLES={'sbbase-music':'base','sbwebserver-music':'web','sbmusic-session':'session',
       'sbmusic-metadata-helper':'metadata'}
TOPOLOGY={'tcp':{'3483':'base','9000':'base','8765':'retained_proxy','80':'web'},'udp':{'3483':'base'},
          'parent':{'base':'session','web':'session'}}

def sha(raw):return hashlib.sha256(raw).hexdigest()
def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':')).encode()
def hexadecimal(value):return isinstance(value,str) and len(value)==64 and all(c in '0123456789abcdef' for c in value)
def source_identity():
    paths=[ROOT/'scripts/music_generation.py',ROOT/'scripts/fab4-lifecycle.py',ROOT/'tests/CMakeLists.txt']
    paths.extend(p for p in (ROOT/'native').rglob('*') if p.suffix in ('.c','.h','.inc','.txt'))
    paths.extend((ROOT/'applet').glob('*.lua'))
    return {p.relative_to(ROOT).as_posix():sha(p.read_bytes()) for p in sorted(paths)}

def validate_elf(raw):
    if not 1<=len(raw)<=2097152:raise ValueError('Executable exceeds native identity bound')
    # Reuse the existing bounded static ARM validator; no new runtime library.
    spec=importlib.util.spec_from_file_location('generation_lifecycle',ROOT/'scripts/fab4-lifecycle.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.static_arm(raw)

def build(output,artifacts,architecture,revision,build_id,previous,rollback):
    if architecture not in ('baby-armv5','fab4-armv6') or set(artifacts)!=set(ROLES):
        raise ValueError('Complete target-specific generation required')
    if not revision or not build_id or not hexadecimal(previous) or not hexadecimal(rollback):
        raise ValueError('Source/build and previous/rollback provenance required')
    files=[];payload={}
    for name in sorted(ROLES):
        raw=Path(artifacts[name]).read_bytes();validate_elf(raw)
        payload[name]=raw
        files.append(dict(name=name,role=ROLES[name],sha256=sha(raw),bytes=len(raw),mode=0o755,uid=0,gid=0))
    body=dict(schema='SB_MUSIC_GENERATION_1',architecture=architecture,source_revision=revision,
              build_identifier=build_id,sources=source_identity(),applet_compatibility='WP4A3B_IDLE_1',
              topology=TOPOLOGY,previous_generation=previous,rollback_receipt_sha256=rollback,files=files)
    generation=sha(canonical(body))
    receipt=dict(generation=generation,manifest=body,
                 executable_paths={name:APP+'/generations/'+generation+'/'+name for name in sorted(ROLES)},
                 activation_qualified=False)
    output=Path(output);output.mkdir(parents=True,exist_ok=False,mode=0o700)
    for name,raw in payload.items():(output/name).write_bytes(raw)
    (output/'receipt.json').write_bytes(canonical(receipt)+b'\n')
    return receipt

def verify(directory,expected_generation,expected_sources=None):
    directory=Path(directory)
    receipt_path=directory/'receipt.json'
    if receipt_path.is_symlink() or receipt_path.stat().st_size>262144:raise ValueError('Unsafe or oversized receipt')
    receipt=json.loads(receipt_path.read_text(encoding='utf-8'))
    body=receipt['manifest']
    if (not hexadecimal(expected_generation) or receipt['generation']!=expected_generation or
        sha(canonical(body))!=expected_generation or body['schema']!='SB_MUSIC_GENERATION_1' or
        body['architecture'] not in ('baby-armv5','fab4-armv6') or
        body['topology']!=TOPOLOGY or body['applet_compatibility']!='WP4A3B_IDLE_1' or
        not hexadecimal(body['previous_generation']) or not hexadecimal(body['rollback_receipt_sha256']) or
        receipt['activation_qualified'] is not False):
        raise ValueError('Generation/provenance mismatch')
    if body['sources']!=(source_identity() if expected_sources is None else expected_sources):
        raise ValueError('Stale source receipt')
    if len(body['files'])!=len(ROLES) or {row['name'] for row in body['files']}!=set(ROLES):
        raise ValueError('Missing/duplicate generation artifact')
    if receipt['executable_paths']!={name:APP+'/generations/'+expected_generation+'/'+name for name in ROLES}:
        raise ValueError('Unexpected executable path')
    for row in body['files']:
        if (row['role']!=ROLES[row['name']] or row['mode']!=0o755 or row['uid']!=0 or row['gid']!=0):
            raise ValueError('Untrusted file metadata')
        path=directory/row['name']
        if path.is_symlink() or not path.is_file():raise ValueError('Unsafe artifact')
        raw=path.read_bytes()
        if len(raw)!=row['bytes'] or sha(raw)!=row['sha256']:raise ValueError('Artifact hash/size mismatch')
        validate_elf(raw)
    return receipt

def verify_rollback(directory,expected_generation,expected_receipt_sha):
    path=Path(directory)/'receipt.json'
    if not hexadecimal(expected_receipt_sha) or sha(path.read_bytes())!=expected_receipt_sha:
        raise ValueError('Rollback receipt missing or changed')
    # Previous generation source hashes are compared against its independently
    # pinned receipt, not silently replaced with current checkout identities.
    body=json.loads(path.read_text(encoding='utf-8'))['manifest']
    return verify(directory,expected_generation,body['sources'])
