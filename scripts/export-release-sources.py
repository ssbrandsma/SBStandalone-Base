#!/usr/bin/env python3
"""Export the current source snapshot and consumed dependency provenance for review."""
import argparse
import hashlib
import io
import json
import shutil
import subprocess
import tarfile
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def export(root, build, output, sdk):
    output.mkdir(parents=True, exist_ok=True)
    version = (root / "VERSION").read_text().strip()
    records = []

    def record(path, **metadata):
        records.append(dict(file=path.name, bytes=path.stat().st_size,
                            sha256=sha(path), **metadata))

    names = subprocess.check_output([
        "git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard"
    ]).decode().splitlines()
    files = {}
    archive = output / f"StandaloneBase-{version}-working-source.tar.gz"
    with tarfile.open(archive, "w:gz") as bundle:
        for name in sorted(set(names)):
            path = root / name
            if (not path.is_file() or path.is_symlink() or
                    name.startswith((".local/", ".downloads/", "ovpn/", "dist/", "build-")) or
                    path.suffix in (".ko", ".ubifs", ".ovpn", ".key", ".pyc")):
                continue
            data = path.read_bytes()
            if data[:4] == b"\x7fELF":
                continue
            # Match the Linux build checkout while recording each archived byte stream.
            if path.suffix in (".c", ".h", ".sh", ".py", ".lua", ".txt", ".cmake",
                               ".js", ".html", ".json", ".xml", ".md"):
                data = data.replace(b"\r\n", b"\n")
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o755 if path.suffix == ".sh" else 0o644
            bundle.addfile(info, io.BytesIO(data))
            files[name] = hashlib.sha256(data).hexdigest()
    record(archive, kind="working-tree-source", files=files,
           base_commit=subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"]).decode().strip(),
           committed=False)

    pupnp = root / ".downloads/pupnp"
    revision = subprocess.check_output(["git", "-C", str(pupnp), "rev-parse", "HEAD"]).decode().strip()
    assert revision == "fac0494691434c35243131f66a0b27ea0c29f700"
    assert not subprocess.check_output(["git", "-C", str(pupnp), "status", "--porcelain"])
    archive = output / "pupnp-22.2.2-source.tar.gz"
    archive.write_bytes(subprocess.check_output(["git", "-C", str(pupnp), "archive", "--format=tar.gz", "HEAD"]))
    record(archive, kind="pinned-upstream-source", revision=revision)

    portal = root / ".downloads/wolfssl-5.9.4-stable.zip"
    assert sha(portal) == "82dbbbb89862c1e29cfe25259d9a1c781e9ecc5db72aeb50a535bc2b819ccbc7"
    destination = output / portal.name
    shutil.copyfile(portal, destination)
    record(destination, kind="pinned-upstream-source")

    for name, url, tag, commit in (
        ("zlib", "https://github.com/madler/zlib.git", "v1.3.2", "da607da739fa6047df13e66a2af6b8bec7c2a498"),
        ("libpng", "https://github.com/pnggroup/libpng.git", "v1.6.59", "cd952f49f95bb27154ae77dbb103032d95f6e580"),
        ("libjpeg-turbo", "https://github.com/libjpeg-turbo/libjpeg-turbo.git", "3.2.0", "c85e6b905bf237038faa936dab160ebfc5da0344"),
    ):
        source = root / ".downloads/release-sources" / name
        if not source.exists():
            source.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "clone", "--depth", "1", "--branch", tag, url, str(source)], check=True)
        assert subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"]).decode().strip() == commit
        # Windows-seeded caches may contain CRLF; archive pinned Git objects,
        # while still refusing substantive edits or untracked files.
        assert not subprocess.check_output(["git", "-c", "core.autocrlf=true", "-C", str(source), "status", "--porcelain"])
        archive = output / f"{name}-{commit[:12]}-source.tar.gz"
        archive.write_bytes(subprocess.check_output(["git", "-C", str(source), "archive", "--format=tar.gz", "HEAD"]))
        record(archive, kind="pinned-upstream-source", revision=commit)

    for name, source in (("curl-8.18.0", sdk / "build/vendor/curl-8.18.0"),
                         ("wolfssl-5.8.2-outbound", sdk / "build/vendor-arm/wolfssl-5.8.2-stable")):
        assert source.is_dir(), f"Missing supplied source: {source}"
        archive = output / f"{name}-supplied-source.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            for path in sorted(source.rglob("*")):
                relative = path.relative_to(source)
                if (not path.is_file() or path.is_symlink() or
                        any(part in (".git", ".deps", ".libs", "CMakeFiles", "__pycache__") for part in relative.parts) or
                        path.suffix in (".o", ".a", ".so", ".pyc", ".exe")):
                    continue
                with path.open("rb") as stream:
                    if stream.read(4) == b"\x7fELF":
                        continue
                bundle.add(path, arcname=str(relative), recursive=False)
        record(archive, kind="supplied-source", upstream_pristine_verified=False)

    inputs = {}
    for component, prefix in (("portal", root / "build-portal-tls/prefix"),
                              ("outbound", root / "build-proxy-tls/prefix")):
        inputs[component + "-options.h"] = prefix / "include/wolfssl/options.h"
        inputs[component + "-library.sha256"] = {"sha256": sha(prefix / "lib/libwolfssl.a")}
    inputs["portal-tls-manifest.txt"] = root / "build-portal-tls/prefix/portal-tls-manifest.txt"
    for library in ("libz.a", "libpng16.a", "libjpeg.a"):
        inputs["image-" + library + ".sha256"] = {"sha256": sha(root / "build-image-arm/prefix/lib" / library)}
    for target in ("baby", "fab4"):
        inputs[target + "-base-manifest.txt"] = build / f"build-arm/{target}/build-manifest.txt"
        inputs[target + "-network-manifest.txt"] = build / f"build-arm/{target}/music-session/manifest.txt"
        inputs[target + "-upnp-manifest.txt"] = build / f"build-upnp/{target}/manifest.txt"
    provenance = {name: ({"sha256": sha(path), "text": path.read_text()} if isinstance(path, Path) else path)
                  for name, path in inputs.items()}
    path = output / "consumed-build-inputs.json"
    path.write_text(json.dumps(provenance, indent=2) + "\n")
    record(path, kind="consumed-configuration-and-hashes")
    inventory = dict(version=version, status="review source snapshot; distribution holds are not cleared",
                     holds=["Authorized Baby sbubifs module matching vendor source/configuration remains unverified",
                            "Mongoose GPLv2 and portal wolfSSL GPLv3 static-link licensing requires clearance"],
                     sources=records)
    (output / "source-inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    return inventory


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("sdk", type=Path)
    args = parser.parse_args()
    result = export(args.source.resolve(), args.build.resolve(), args.output.resolve(), args.sdk.resolve())
    print(json.dumps({"version": result["version"], "archives": len(result["sources"]),
                      "status": result["status"]}, indent=2))
