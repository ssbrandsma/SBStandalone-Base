#!/usr/bin/env python3
"""Fetch the pinned official wolfSSL tag archive for the portal-only build."""
import hashlib, sys, urllib.request, zipfile
from pathlib import Path
VERSION = "5.9.4"
SHA256 = "82dbbbb89862c1e29cfe25259d9a1c781e9ecc5db72aeb50a535bc2b819ccbc7"
URL = "https://codeload.github.com/wolfSSL/wolfssl/zip/refs/tags/v5.9.4-stable"
root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=True)
archive = root / ("wolfssl-" + VERSION + "-stable.zip")
if not archive.exists():
    with urllib.request.urlopen(URL, timeout=60) as response:
        data = response.read(100 * 1024 * 1024 + 1)
    if len(data) > 100 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit("Portal TLS archive integrity check failed")
    temporary = archive.with_suffix(".download")
    temporary.write_bytes(data); temporary.replace(archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
    raise SystemExit("Cached portal TLS archive has the wrong SHA-256")
source = root / ("wolfssl-" + VERSION + "-stable")
marker = source / ".standalone-archive-sha256"
if not marker.exists() or marker.read_text().strip() != SHA256:
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            if name.startswith("/") or ".." in Path(name).parts:
                raise SystemExit("Unsafe portal TLS archive path")
        package.extractall(root)
    marker.write_text(SHA256 + "\n")
print(source)
