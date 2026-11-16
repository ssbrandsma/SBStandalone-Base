#!/usr/bin/env python3
"""Verify a local full release candidate; never publish or deploy it."""
import argparse
import hashlib
import json
import re
import stat
import subprocess
import tempfile
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET


def verify_lua_dependencies(members):
    # The explicit developer-only idle-test marker needs a separate test bundle.
    optional = {"MusicIdleFence.lua", "MusicProduction.lua"}
    pattern = rb'(?:require|tryRequire)\s*\(?\s*[\'\"]applets\.StandaloneBase\.([^\'\"]+)'
    for name, body in members.items():
        if not name.endswith(".lua"):
            continue
        for module in re.findall(pattern, body):
            required = module.decode() + ".lua"
            if required not in members and not (name == "StandaloneBaseApplet.lua" and required in optional):
                raise ValueError(f"{name} requires missing {required}")


def verify(root, directory, target):
    version = (root / "VERSION").read_text().strip()
    archive = directory / f"StandaloneBase-{version}-{target}.zip"
    binaries = {name: f"build-arm/{target}/{name}" for name in (
        "sbbase", "sbwebserver", "sbproxy", "sbvpn", "sb-tinysc-control",
        "sb-storage-helper", "sb-storage-updater", "svgpngrenderer")}
    binaries.update({"sbupnp": f"build-upnp/{target}/sbupnp",
                     "sbbase-network": f"build-arm/{target}/music-session/sbbase_music",
                     "sbwebserver-network": f"build-arm/{target}/music-session/sbwebserver_music"})
    with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory() as temporary:
        names = z.namelist()
        assert len(names) == len(set(names)) and z.testzip() is None
        assert all("/" not in n and "\\" not in n and n not in (".", "..") for n in names)
        assert not any(n.endswith((".ovpn", ".key")) or n in (
            "credentials", "device.crt", "session.json", "settings.json", "entropy.seed") for n in names)
        members = {name: z.read(name) for name in names}
        verify_lua_dependencies(members)
        assert members["build-target"].decode().strip() == target
        assert members["build-version"].decode().strip() == version
        compat = json.loads(members["network-music.compat.json"])
        assert compat == dict(schema="SB_NETWORK_MUSIC_1", version=version, target=target,
                              resource_wire=3, upnp_ipc=1, ownership_wire=1,
                              queue_limit=256, renderer_default=False)
        for name, source in binaries.items():
            data = members[name]
            assert data == (root / source).read_bytes(), f"Stale executable: {name}"
            assert stat.S_IMODE(z.getinfo(name).external_attr >> 16) == 0o755
            path = Path(temporary) / name
            path.write_bytes(data)
            header = subprocess.check_output(["readelf", "-h", str(path)], text=True)
            attrs = subprocess.check_output(["readelf", "-A", str(path)], text=True)
            assert "ARM" in header and "Version5 EABI" in header
            arches = ("v5TE", "v5TEJ") if target == "baby" else ("v6",)
            assert any(line.strip() == "Tag_CPU_arch: " + arch
                       for line in attrs.splitlines() for arch in arches)
            assert "VFP registers" not in attrs
            assert "INTERP" not in subprocess.check_output(["readelf", "-l", str(path)], text=True)
            assert "NEEDED" not in subprocess.check_output(["readelf", "-d", str(path)], text=True)
        assert version.encode() in members["sbbase"]
        assert ("sbubifs-authorized.ko" in members) == (target == "baby")
        for name in names:
            source = root / "applet" / name if name.endswith(".lua") else None
            if source and source.exists():
                assert members[name] == source.read_bytes(), f"Stale Lua: {name}"
        for name, source in {"secure.js": "js/secure.js", "vpn.js": "js/vpn.js",
                             "app.js": "js/app.js", "index.html": "index.html"}.items():
            assert members[name] == (root / "native/sbwebserver/web" / source).read_bytes()
        assert b"fence.snoop" not in members["PlaybackFenceV6.lua"]
        assert b"wolfSSL=5.9.4" in members["portal-tls-manifest.txt"]
        catalog = json.loads(members["config.json"])
        assert catalog["standalone_base_version"] == version
        assert not any(item.get("name") == "StandaloneBase" for item in catalog.get("applets", []))
        for line in members["network-music.sha256"].decode().splitlines():
            digest, name = line.split()
            assert hashlib.sha256(members[name]).hexdigest() == digest
        for notice in ("PUPNP-LICENSE", "SBUPNP-LICENSE", "UPNP-THIRD-PARTY-NOTICES",
                       "PUPNP-SOURCE-NOTICES", "PORTAL-TLS-LICENSE", "WOLFSSL-LICENSING",
                       "CURL-COPYING", "GPL-2.0", "GPL-3.0", "MPL-2.0", "CA-BUNDLE-NOTICE",
                       "MUSL-COPYRIGHT", "GCC-RUNTIME-EXCEPTION", "SBPROXY-LICENSE"):
            assert len(members[notice]) > 100, notice
        applet = ET.parse(archive.with_suffix(".xml")).find("./applets/applet")
        assert applet.attrib["version"] == version and applet.attrib["target"] == target
        assert applet.findtext("sha") == hashlib.sha1(archive.read_bytes()).hexdigest()
    cpu = "arm926" if target == "baby" else "arm1136"
    component = root / "build-upnp" / target
    component_version = subprocess.check_output(["qemu-arm", "-cpu", cpu, str(component / "sbupnp"), "--version"], text=True, timeout=20)
    assert component_version.startswith("sbupnp 0.3.1;")
    subprocess.run(["qemu-arm", "-cpu", cpu, str(component / "test_sbupnp_core")], check=True, timeout=60)
    return dict(target=target, version=version, file=archive.name,
                bytes=archive.stat().st_size, members=len(names), executables=len(binaries),
                sha256=hashlib.sha256(archive.read_bytes()).hexdigest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("packages", type=Path)
    parser.add_argument("target", choices=("baby", "fab4", "all"), nargs="?", default="all")
    args = parser.parse_args()
    targets = ("baby", "fab4") if args.target == "all" else (args.target,)
    result = [verify(args.source, args.packages, target) for target in targets]
    (args.packages / "release-verification.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
