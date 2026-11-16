import hashlib, json, os, sys, zipfile
from pathlib import Path
from xml.etree import ElementTree as ET
root, target, archive = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
version=os.environ.get('SB_PACKAGE_VERSION',(root / 'VERSION').read_text().strip())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None, "Corrupt candidate archive"
    assert z.read("build-target").decode().strip() == target
    for name, source in (("Vpn.lua", "applet/Vpn.lua"), ("vpn.js", "native/sbwebserver/web/js/vpn.js"), ("index.html", "native/sbwebserver/web/index.html"), ("app.js", "native/sbwebserver/web/js/app.js")):
        assert z.read(name) == (root / source).read_bytes(), "Stale candidate asset: " + name
    for n in ("sbvpn", "sb-tinysc-control", "sbbase", "sbproxy", "sbwebserver", "svgpngrenderer", "sb-storage-helper", "sb-storage-updater"):
        assert z.read(n)[:4] == b"\x7fELF"
        assert (z.getinfo(n).external_attr >> 16) & 0o111
sha = hashlib.sha1(archive.read_bytes()).hexdigest()
base = os.environ.get("SB_RELEASE_BASE_URL", "")
# Relative URLs are local review metadata until a release base is supplied.
url = base.rstrip("/") + "/" + archive.name if base else archive.name
xml = ET.Element("extensions")
apps = ET.SubElement(xml, "applets")
a = ET.SubElement(apps, "applet", name="StandaloneBase", version=version, target=target, minTarget="7.7.2", maxTarget="*")
ET.SubElement(a, "title", lang="EN").text = "Standalone Base"
ET.SubElement(a, "desc", lang="EN").text = "Local standalone services for Squeezebox " + ("Touch." if target == "fab4" else "Radio.")
ET.SubElement(a, "changes", lang="EN").text = "Validate Radio Extended Storage dynamically from the live UBI inventory, mapped MTD geometry and free capacity, with strict firmware-volume protection."
if os.environ.get('SB_PACKAGE_UPNP') == '1':
    a.find('changes').text='UPnP My Music, original Now Playing playlist, optional renderer and transactional installer; controlled hardware acceptance candidate.'
ET.SubElement(a, "creator").text = "Sjoerd Brandsma"
ET.SubElement(a, "url").text = url
ET.SubElement(a, "sha").text = sha
ET.ElementTree(xml).write(archive.with_suffix(".xml"), encoding="utf-8", xml_declaration=True)
archive.with_suffix(".manifest.json").write_text(json.dumps(dict(target=target, version=version, sha1=sha, files=zipfile.ZipFile(archive).namelist()), indent=2))
