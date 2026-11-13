import hashlib, json, os, sys, zipfile
from pathlib import Path
from xml.etree import ElementTree as ET
root, target, archive = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
with zipfile.ZipFile(archive) as z:
    assert z.read("build-target").decode().strip() == target
    for n in ("sbbase", "sbproxy", "sbwebserver", "svgpngrenderer", "sb-storage-helper", "sb-storage-updater"):
        assert z.read(n)[:4] == b"\x7fELF"
        assert (z.getinfo(n).external_attr >> 16) & 0o111
sha = hashlib.sha1(archive.read_bytes()).hexdigest()
base = os.environ.get("SB_RELEASE_BASE_URL", "")
# Relative URLs are local review metadata until a release base is supplied.
url = base.rstrip("/") + "/" + archive.name if base else archive.name
xml = ET.Element("extensions")
apps = ET.SubElement(xml, "applets")
a = ET.SubElement(apps, "applet", name="StandaloneBase", version="0.7.1", target=target, minTarget="7.7.2", maxTarget="*")
ET.SubElement(a, "title", lang="EN").text = "Standalone Base"
ET.SubElement(a, "desc", lang="EN").text = "Local standalone services for Squeezebox " + ("Touch." if target == "fab4" else "Radio.")
ET.SubElement(a, "changes", lang="EN").text = "Remember this browser web access, readable access codes, standby power handling and Radio connection fixes."
ET.SubElement(a, "creator").text = "Sjoerd Brandsma"
ET.SubElement(a, "url").text = url
ET.SubElement(a, "sha").text = sha
ET.ElementTree(xml).write(archive.with_suffix(".xml"), encoding="utf-8", xml_declaration=True)
archive.with_suffix(".manifest.json").write_text(json.dumps(dict(target=target, version="0.7.1", sha1=sha, files=zipfile.ZipFile(archive).namelist()), indent=2))
