#!/usr/bin/env python3
"""Read-only verification of private Baby/Fab4 candidate packages, not publication."""
import argparse, hashlib, json, stat, subprocess, tempfile, zipfile
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('source',type=Path);parser.add_argument('packages',type=Path);args=parser.parse_args()
assets={'StandaloneBaseApplet.lua':'applet/StandaloneBaseApplet.lua','strings.txt':'applet/strings.txt','secure.js':'native/sbwebserver/web/js/secure.js','style.css':'native/sbwebserver/web/css/style.css','Vpn.lua':'applet/Vpn.lua','vpn.js':'native/sbwebserver/web/js/vpn.js','index.html':'native/sbwebserver/web/index.html','app.js':'native/sbwebserver/web/js/app.js'}
executables=['sbvpn','sb-tinysc-control','sbbase','sbwebserver','sbproxy','sb-storage-helper','sb-storage-updater','svgpngrenderer']
version=(args.source/'VERSION').read_text().strip()
results=[]
for target,arch in [('baby','v5TE'),('fab4','v6')]:
 archive=args.packages/f'StandaloneBase-{version}-{target}.zip'
 with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory() as temporary:
  assert z.testzip() is None
  names=z.namelist();assert len(names)==len(set(names));assert all('/' not in n and n not in ['credentials','settings.json','session.json','network.json','process.json','client.ovpn','control.sock','device.key','device.crt','metadata.json','entropy.seed'] for n in names)
  assert z.read('build-target').decode().strip()==target
  assert ('sbubifs-authorized.ko' in names)==(target=='baby')
  assert ('sbdata-empty.ubifs' in names)==(target=='baby')
  assert b'wolfSSL=5.9.4\n' in z.read('portal-tls-manifest.txt')
  assert b'archive_sha256=82dbbbb89862c1e29cfe25259d9a1c781e9ecc5db72aeb50a535bc2b819ccbc7' in z.read('portal-tls-manifest.txt')
  assert len(z.read('PORTAL-TLS-LICENSE')) > 1000
  for name,source in assets.items():assert z.read(name)==(args.source/source).read_bytes(),name
  assert b'vpn.js?v=4' in z.read('index.html') and b'vpn:window.vpnPage' in z.read('app.js')
  for name in executables:
   data=z.read(name);assert data==(args.source/'build-arm'/target/name).read_bytes(),name
   assert stat.S_IMODE(z.getinfo(name).external_attr>>16)==0o755,name
   f=Path(temporary)/name;f.write_bytes(data)
   header=subprocess.check_output(['readelf','-h',str(f)],text=True)
   assert 'ARM' in header and 'Version5 EABI' in header,name
   attributes=subprocess.check_output(['readelf','-A',str(f)],text=True)
   allowed_arches=('v5TE','v5TEJ') if target=='baby' else ('v6',)
   assert any(line.strip()=='Tag_CPU_arch: '+value for line in attributes.splitlines() for value in allowed_arches),name
   assert 'Tag_ABI_VFP_args: VFP registers' not in attributes,name
   assert 'INTERP' not in subprocess.check_output(['readelf','-l',str(f)],text=True),name
   assert 'NEEDED' not in subprocess.check_output(['readelf','-d',str(f)],text=True),name
  results.append(dict(target=target,archive=archive.name,sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),bytes=archive.stat().st_size,files=len(names),verified_executables=len(executables)))
print(json.dumps({'private_candidates':results,'published':False},indent=2))
