#!/usr/bin/env python3
"""Prepare local review catalogs from verified ZIPs; never upload or replace live catalogs."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit
from xml.etree import ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("packages", type=Path)
args = parser.parse_args()
version = (args.source / "VERSION").read_text().strip()
proof = {row["target"]: row for row in json.loads((args.packages / "release-verification.json").read_text())}
value = json.loads((args.source / "config.json").read_text())
value["standalone_base_version"] = version
output = args.packages / "catalog-review"
output.mkdir(exist_ok=True)
extensions = ET.Element("extensions")
applets = ET.SubElement(extensions, "applets")
for item in value["applets"]:
    if item.get("name") != "StandaloneBase":
        continue
    target = item["target"]
    archive = args.packages / f"StandaloneBase-{version}-{target}.zip"
    assert proof[target]["version"] == version and proof[target]["sha256"] == hashlib.sha256(archive.read_bytes()).hexdigest()
    digest = hashlib.sha1(archive.read_bytes()).hexdigest()
    item["version"], item["sha"] = version, digest
    old = urlsplit(item["url"])
    path = old.path.rsplit("/", 1)[0] + "/" + archive.name
    item["url"] = urlunsplit((old.scheme, old.netloc, path, "", ""))
    item["changes"] = "UPnP My Music and original playlist, optional renderer, transactional installer, VPN and secure HTTPS management."
    node = ET.parse(archive.with_suffix(".xml")).find("./applets/applet")
    assert node.attrib["version"] == version and node.findtext("sha") == digest
    node.find("url").text = item["url"]
    applets.append(copy.deepcopy(node))
    target_xml = ET.Element("extensions")
    ET.SubElement(target_xml, "applets").append(copy.deepcopy(node))
    ET.ElementTree(target_xml).write(output / f"extensions-{target}.xml", encoding="utf-8", xml_declaration=True)
(output / "config.json").write_text(json.dumps(value, indent=2) + "\n")
ET.ElementTree(extensions).write(output / "extensions.xml", encoding="utf-8", xml_declaration=True)
print(output)
