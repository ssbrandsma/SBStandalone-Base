#!/usr/bin/env python3
"""Prepare matching Baby/Fab4 release catalogs without repacking ZIPs."""
import argparse, copy, hashlib, json, re, zipfile
from pathlib import Path
from xml.etree import ElementTree as ET


def replace_entries(items, entries):
    names={a['name'] for a in entries}
    result=[]; inserted=False
    for item in items:
        if item['name'] in names:
            if not inserted: result.extend(copy.deepcopy(entries)); inserted=True
        else: result.append(item)
    if not inserted: result.extend(copy.deepcopy(entries))
    return result


def xml_entry(a):
    e=ET.Element('applet',name=a['name'],version=a['version'],target=a['target'],minTarget=a['min_target_version'],maxTarget='*')
    for key in ('title','desc','changes','creator','url','sha'):
        child=ET.SubElement(e,key,{'lang':'EN'} if key in ('title','desc','changes') else {})
        child.text=a.get(key,'')
    return e


def update_xml(path, entries):
    root=ET.parse(path).getroot() if path.exists() else ET.Element('extensions')
    apps=root.find('applets')
    if apps is None: apps=ET.SubElement(root,'applets')
    names={a['name'] for a in entries}
    for a in list(apps):
        if a.tag=='applet' and a.get('name') in names: apps.remove(a)
    for a in entries: apps.append(xml_entry(a))
    ET.indent(root,space='  ')
    ET.ElementTree(root).write(path,encoding='utf-8',xml_declaration=True)
    path.with_suffix(path.suffix+'.sha1').write_text(hashlib.sha1(path.read_bytes()).hexdigest()+'\n')


def prepare(args):
    version=args.version
    if not re.fullmatch(r'\d+\.\d+\.\d+',version): raise ValueError('Invalid release version')
    entries=[]; artifacts=[]
    radio=args.radio_zip
    match=re.fullmatch(r'StandaloneRadio-(\d+\.\d+\.\d+)\.zip',radio.name)
    if not match: raise ValueError('Invalid Radio package name')
    with zipfile.ZipFile(radio) as z:
        if z.testzip() or any(z.read(n)[:4]==b'\x7fELF' for n in z.namelist()):
            raise ValueError('Expected validated Lua-only Radio package')
    for target in ('baby','fab4'):
        archive=args.output/f'StandaloneBase-{version}-{target}.zip'
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            assert z.read('build-target').decode().strip()==target
            for binary in ('sbbase','sbproxy','sbwebserver','svgpngrenderer','sb-storage-helper','sb-storage-updater'):
                assert z.read(binary)[:4]==b'\x7fELF'
        device='Radio' if target=='baby' else 'Touch'
        a=dict(name='StandaloneBase',title='Standalone Base',version=version,target=target,min_target_version='7.7.2',
               url=args.base_url.rstrip('/')+'/'+archive.name,sha=hashlib.sha1(archive.read_bytes()).hexdigest(),
               desc=f'Local LMS-compatible infrastructure for Squeezebox {device}.',
               changes='Dual-platform Baby/Fab4 builds and live Virtual Touch display with touchscreen controls.',creator='Sjoerd Brandsma',email='')
        entries.append(a)
        artifacts.append(dict(file=archive.name,target=target,sha1=a['sha'],sha256=hashlib.sha256(archive.read_bytes()).hexdigest()))
    base_entries=entries.copy()
    radio_entries=[]
    for target in ('baby','fab4'):
        radio_entries.append(dict(name='StandaloneRadio',title='Standalone Radio',version=match[1],target=target,min_target_version='7.7',
            url=args.radio_url.rstrip('/')+'/'+radio.name,sha=hashlib.sha1(radio.read_bytes()).hexdigest(),
            desc='Standalone internet radio playback for Squeezebox '+('Radio.' if target=='baby' else 'Touch.'),
            changes='Managed LMS handoff and local playback-owner fixes, with user stations and exact-name logo fallback.',creator='Sjoerd Brandsma',email=''))
    entries.extend(radio_entries)
    for path in args.config:
        c=json.loads(path.read_text(encoding='utf-8-sig'))
        c['applets']=replace_entries(c.get('applets',[]),entries)
        if 'standalone_base_version' in c:c['standalone_base_version']=version
        path.write_text(json.dumps(c,indent=2,ensure_ascii=False)+'\n',encoding='utf-8',newline='\n')
    for path in args.extensions:update_xml(path,entries)
    update_xml(args.output/'extensions.xml',base_entries)
    update_xml(args.output/'radio-extensions.xml',radio_entries)
    artifacts.append(dict(file=radio.name,target='baby,fab4',sha1=radio_entries[0]['sha'],sha256=hashlib.sha256(radio.read_bytes()).hexdigest()))
    manifest=dict(version=version,applets=entries,artifacts=artifacts)
    (args.output/f'release-{version}.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--version',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--base-url',required=True);p.add_argument('--radio-url',required=True)
    p.add_argument('--radio-zip',type=Path,required=True)
    p.add_argument('--config',type=Path,action='append',default=[])
    p.add_argument('--extensions',type=Path,action='append',default=[])
    prepare(p.parse_args())
