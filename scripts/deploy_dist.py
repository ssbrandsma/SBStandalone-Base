#!/usr/bin/env python3
"""Deploy a StandaloneBase dist ZIP to a Squeezebox Radio over SSH."""

from __future__ import annotations

import argparse
import getpass
import hashlib
import io
import json
import os
import re
import shlex
import sys
import time
import tarfile
import warnings
import zipfile
from pathlib import Path

try:
    from cryptography.utils import CryptographyDeprecationWarning

    warnings.filterwarnings("ignore", category=CryptographyDeprecationWarning)
    import paramiko
except ImportError:
    sys.stderr.write("deploy_dist requires Paramiko: python -m pip install paramiko\n")
    raise SystemExit(2)


REQUIRED = {
    "StandaloneBaseMeta.lua",
    "StandaloneBaseApplet.lua",
    "config.json",
    "sb-storage-helper",
    "sbubifs-authorized.ko",
    "sbdata-empty.ubifs",
    "storage-setup.sh",
    "storage-boot.sh",
    "patch-squeezeplay-artwork.sh",
}
EXECUTABLES = {
    "svgpngrenderer",
    "sbbase",
    "sbwebserver",
    "sbproxy",
    "sb-storage-helper",
    "sb-storage-updater",
    "storage-setup.sh",
    "storage-boot.sh",
    "patch-squeezeplay-artwork.sh",
}
# Install through the merged UnionFS path only. On SqueezeOS, /mnt/storage is
# the writable branch beneath that union. Addressing /mnt/storage/usr through
# the merged namespace can create /mnt/storage/mnt/storage/usr recursively and
# consume the small UBIFS volume with a duplicate applet tree.
TARGETS = ("/usr/share/jive/applets/StandaloneBase",)
APPLET_FILES = {
    "StandaloneBaseApplet.lua", "StandaloneBaseMeta.lua", "Platform.lua",
    "StorageManager.lua", "ExtendedStorageState.lua", "TimeSync.lua", "TimeResolver.lua",
    "RemoteBridge.lua", "ServerMigration.lua", "NetworkAddress.lua", "ImageRender.lua",
    "ArtworkUrlNormalizer.lua", "Artwork.lua", "InstallerCompat.lua", "HttpTrace.lua",
    "FirmwareUpdates.lua", "AlarmPlayback.lua", "LifecycleHeartbeat.lua", "strings.txt", "build-target",
}


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="deploy_dist",
        description="Validate and deploy a StandaloneBase dist ZIP over SSH.",
    )
    parser.add_argument("zip_file", type=Path, help="ZIP from the dist directory")
    parser.add_argument("target", help="SSH destination in user@host form")
    parser.add_argument("-p", "--password", help="SSH password; -pPASSWORD is accepted")
    parser.add_argument("--port", type=int, default=22, help="SSH port (default: 22)")
    parser.add_argument(
        "--accept-new-host-key",
        action="store_true",
        help="trust an unknown host key for this run (known hosts are verified by default)",
    )
    parser.add_argument("--no-restart", action="store_true", help="do not restart SqueezePlay")
    parser.add_argument("--dry-run", action="store_true", help="validate locally without connecting")
    parser.add_argument("--applet-only", action="store_true", help="copy only qualified Lua/data files; requires --no-restart and --backup-dir")
    parser.add_argument("--backup-dir", type=Path, help="new local directory for verified original applet files")
    parser.add_argument("--restore-backup", type=Path, help="restore a matching applet-only backup without loading/restarting Jive")
    return parser.parse_args()


def validate_package(path: Path, applet_only=False) -> tuple[list[str], str]:
    path = path.resolve()
    if not path.is_file():
        raise ValueError(f"package not found: {path}")
    match = re.fullmatch(r"StandaloneBase-([0-9]+\.[0-9]+\.[0-9]+)(?:-fab4)?\.zip", path.name)
    if not match:
        raise ValueError("package name must be StandaloneBase-X.Y.Z.zip")
    if path.parent.name.lower() != "dist":
        raise ValueError("package must be located in a dist directory")
    with zipfile.ZipFile(path) as archive:
        infos = archive.infolist()
        names = [entry.filename for entry in infos if not entry.is_dir()]
        if len(names) != len(set(names)):
            raise ValueError("package contains duplicate entries")
        if any("/" in name or "\\" in name or name in (".", "..") for name in names):
            raise ValueError("package entries must all be flat file names")
        missing = sorted((APPLET_FILES if applet_only else REQUIRED).difference(names))
        if not applet_only and tuple(map(int, match.group(1).split('.'))) >= (0, 5, 7) and "InstallerCompat.lua" not in names:
            missing.append("InstallerCompat.lua")
        if missing:
            raise ValueError("package is missing: " + ", ".join(missing))
        bad = [entry.filename for entry in infos if entry.is_dir()]
        if bad:
            raise ValueError("package must not contain directories")
        if archive.testzip() is not None:
            raise ValueError("package CRC validation failed")
        if applet_only and archive.read("build-target").strip() != b"fab4":
            raise ValueError("applet-only smoke deployment requires a Fab4 package")
    return sorted(APPLET_FILES) if applet_only else names, match.group(1)


def split_target(value: str) -> tuple[str, str]:
    if value.count("@") != 1:
        raise ValueError("target must use user@host syntax")
    user, host = value.split("@", 1)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", user):
        raise ValueError("invalid SSH user")
    if not re.fullmatch(r"[A-Za-z0-9_.:-]+", host):
        raise ValueError("invalid SSH host")
    return user, host


def digest(data):
    return hashlib.sha256(data).hexdigest()


def applet_inventory(radio):
    root = TARGETS[0]
    # Same merged UnionFS destination as normal deployment; reject redirects.
    parents = ["/usr", "/usr/share", "/usr/share/jive", "/usr/share/jive/applets", root]
    radio.run(" && ".join("test -d " + shlex.quote(p) + " && test ! -L " + shlex.quote(p) for p in parents), attempts=1)
    files = []
    for name in sorted(APPLET_FILES):
        path = shlex.quote(root + "/" + name)
        result = radio.run(f'if test -L {path}; then echo LINK; elif test -f {path}; then echo FILE; '
                           f'elif test -e {path}; then echo OTHER; else echo ABSENT; fi', attempts=1)
        if result not in {"FILE", "ABSENT"}:
            raise RuntimeError("applet target is not a regular file: " + name)
        if result == "FILE":
            files.append(name)
    archive = radio.run("tar -cpf - -C " + shlex.quote(root) + " " + " ".join(map(shlex.quote, files)), attempts=1, binary=True)
    records = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for item in source:
            if item.name not in files or item.name in records or not item.isfile() or item.uid != 0 or item.gid != 0 or item.mode & ~0o755 or item.size > 2097152:
                raise RuntimeError("unsafe applet backup member")
            data = source.extractfile(item).read()
            records[item.name] = dict(sha256=digest(data), mode=item.mode, uid=item.uid, gid=item.gid, mtime=item.mtime)
    if set(records) != set(files):
        raise RuntimeError("incomplete applet backup")
    return archive, records


def load_applet_backup(directory, host):
    manifest = json.loads((directory / "manifest.json").read_text())
    archive = (directory / "originals.tar").read_bytes()
    if (manifest.get("schema") != "SB_APPLET_ONLY_1" or manifest.get("host") != host or
            manifest.get("root") != TARGETS[0] or set(manifest["new"]) != APPLET_FILES or
            not set(manifest["original"]).issubset(APPLET_FILES) or digest(archive) != manifest["archive_sha256"]):
        raise ValueError("applet backup manifest mismatch")
    found = set()
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for item in source:
            if item.name not in manifest["original"] or item.name in found or not item.isfile():
                raise ValueError("unexpected original archive entry")
            record = manifest["original"][item.name]
            if (item.uid, item.gid, item.mode, item.mtime) != (0, 0, record["mode"], record["mtime"]) or item.mode & ~0o755:
                raise ValueError("original archive metadata mismatch")
            if digest(source.extractfile(item).read()) != record["sha256"]:
                raise ValueError("original archive hash mismatch")
            found.add(item.name)
    if found != set(manifest["original"]):
        raise ValueError("incomplete original archive")
    return manifest


def applet_replace(radio, source, name, mode):
    atomic_replace(radio, source, TARGETS[0] + "/" + name, mode)


def atomic_replace(radio, source, target, mode):
    """Shared checked-stage copy primitive; caller supplies verified targets."""
    temporary = target + ".new"
    radio.run("test ! -e " + shlex.quote(temporary) + " && test ! -L " + shlex.quote(temporary) +
              " && ( trap " + shlex.quote("rm -f " + shlex.quote(temporary)) + " EXIT; cp -p " + shlex.quote(source) + " " + shlex.quote(temporary) +
              " && chmod " + format(mode, "o") + " " + shlex.quote(temporary) +
              " && mv -f " + shlex.quote(temporary) + " " + shlex.quote(target) + " )", attempts=1)


def applet_restore(radio, manifest, stage, names=None):
    _, current = applet_inventory(radio)
    for name in reversed(sorted(APPLET_FILES if names is None else names)):
        record = current.get(name)
        old = manifest["original"].get(name)
        allowed = {manifest["new"][name]}
        if old:
            allowed.add(old["sha256"])
        if record and record["sha256"] not in allowed:
            raise RuntimeError("refuse restoring unknown applet revision: " + name)
        if old:
            applet_replace(radio, stage + "/original/" + name, name, old["mode"])
        elif record:
            radio.run("rm -f " + shlex.quote(TARGETS[0] + "/" + name), attempts=1)
    _, restored = applet_inventory(radio)
    if restored != manifest["original"]:
        raise RuntimeError("original applet metadata/hash restoration did not verify")


def deploy_applet_only(radio, package, backup, restore=None):
    """Narrow mode of the existing SCP/stage/atomic-copy workflow. No reload."""
    if restore:
        backup = restore
        manifest = load_applet_backup(backup, radio.host)
    else:
        archive, original = applet_inventory(radio)
        backup.mkdir(parents=True, exist_ok=False)
        (backup / "originals.tar").write_bytes(archive)
        with zipfile.ZipFile(package) as source, zipfile.ZipFile(backup / "applet.zip", "w", zipfile.ZIP_DEFLATED) as selected:
            new = {}
            for name in sorted(APPLET_FILES):
                data = source.read(name)
                if len(data) > 2097152:
                    raise ValueError("oversized applet file")
                new[name] = digest(data)
                selected.writestr(name, data)
        manifest = dict(schema="SB_APPLET_ONLY_1", host=radio.host, root=TARGETS[0],
                        original=original, new=new, archive_sha256=digest(archive))
        with (backup / "manifest.json").open("x") as output:
            json.dump(manifest, output, indent=2, sort_keys=True)
            output.flush()
            os.fsync(output.fileno())
        load_applet_backup(backup, radio.host)
    stage = radio.run("mktemp -d /tmp/standalonebase-deploy.XXXXXX", attempts=1)
    if not re.fullmatch(r"/tmp/standalonebase-deploy\.[A-Za-z0-9]+", stage):
        raise RuntimeError("unsafe staging path")
    qstage = shlex.quote(stage)
    try:
        radio.upload(backup / "originals.tar", stage, "originals.tar")
        if digest(radio.run("cat " + qstage + "/originals.tar", attempts=1, binary=True)) != manifest["archive_sha256"]:
            raise RuntimeError("uploaded backup mismatch")
        radio.run("mkdir " + qstage + "/original && tar -xpf " + qstage + "/originals.tar -C " + qstage + "/original", attempts=1)
        if restore:
            applet_restore(radio, manifest, stage)
            return dict(state="RESTORED_ON_DISK", files=sorted(APPLET_FILES), activated=False)
        radio.upload(backup / "applet.zip", stage, "package.zip")
        radio.run("mkdir " + qstage + "/files && unzip -oq " + qstage + "/package.zip -d " + qstage + "/files", attempts=1)
        _, current = applet_inventory(radio)
        if current != manifest["original"]:
            raise RuntimeError("applet changed after backup")
        attempted = []
        try:
            for name in sorted(APPLET_FILES):
                if manifest["original"].get(name, {}).get("sha256") == manifest["new"][name]:
                    continue
                source = stage + "/files/" + name
                if digest(radio.run("cat " + shlex.quote(source), attempts=1, binary=True)) != manifest["new"][name]:
                    raise RuntimeError("uploaded applet mismatch: " + name)
                attempted.append(name)
                applet_replace(radio, source, name, 0o644)
            _, after = applet_inventory(radio)
            if any(after[name]["sha256"] != manifest["new"][name] or after[name]["mode"] != 0o644 for name in APPLET_FILES):
                raise RuntimeError("installed applet hash/mode mismatch")
        except Exception:
            applet_restore(radio, manifest, stage, attempted)
            raise
        result = dict(state="DEPLOYED_ON_DISK", files_changed=attempted, hashes=after, activated=False)
        (backup / "deployment.json").write_text(json.dumps(result, indent=2))
        return result
    finally:
        radio.run("rm -rf " + qstage, attempts=1)


class Radio:
    def __init__(self, user: str, host: str, password: str, port: int, accept_new: bool):
        self.user, self.host, self.password, self.port = user, host, password, port
        self.accept_new = accept_new
        self.client = self._new_client()

    def _new_client(self):
        client = paramiko.SSHClient()
        client.load_system_host_keys()
        client.set_missing_host_key_policy(
            paramiko.AutoAddPolicy() if self.accept_new else paramiko.RejectPolicy()
        )
        return client

    def connect(self) -> None:
        self.client.connect(
            self.host,
            port=self.port,
            username=self.user,
            password=self.password,
            look_for_keys=False,
            allow_agent=False,
            timeout=12,
        )

    def run(self, command: str, attempts: int = 3, binary: bool = False):
        last: Exception | None = None
        marker = "__STANDALONEBASE_DEPLOY_RC="
        for attempt in range(attempts):
            try:
                if not self.client.get_transport() or not self.client.get_transport().is_active():
                    self.client.close()
                    self.client = self._new_client()
                    self.connect()
                # A newline terminates either a normal command or a trailing
                # background '&'. Adding ';' here formed invalid '&;' syntax
                # on BusyBox ash for the scheduled SqueezePlay restart.
                wrapped = (
                    "{\n" + command + "\n}\n"
                    "sb_deploy_rc=$?\n"
                    "printf '\\n" + marker + "%s\\n' \"$sb_deploy_rc\""
                )
                _, stdout, stderr = self.client.exec_command(wrapped)
                raw = stdout.read(16 * 1024 * 1024 + 1)
                if len(raw) > 16 * 1024 * 1024:
                    raise RuntimeError("bounded deployment response exceeded")
                output, error = raw.decode(errors="replace"), stderr.read().decode(errors="replace")
                status = stdout.channel.recv_exit_status()
                if marker in output:
                    body, result = output.rsplit(marker, 1)
                    remote_status = int(result.strip().splitlines()[0])
                    if remote_status:
                        raise RuntimeError(f"remote command failed ({remote_status}): {error or body}")
                    if binary:
                        return raw.rsplit(("\n" + marker).encode(), 1)[0]
                    return body.rstrip("\r\n")
                if status:
                    raise RuntimeError(f"remote command failed ({status}): {error or output}")
                raise RuntimeError("remote command ended without a completion marker")
            except Exception as exc:  # Dropbear occasionally closes a session.
                last = exc
                self.client.close()
                if attempt + 1 < attempts:
                    time.sleep(1)
        raise RuntimeError(str(last))

    def upload(self, source: Path, remote_directory: str, remote_name: str) -> None:
        payload = source.read_bytes()
        for attempt in range(3):
            try:
                if not self.client.get_transport() or not self.client.get_transport().is_active():
                    self.connect()
                channel = self.client.get_transport().open_session()
                channel.exec_command("scp -t " + shlex.quote(remote_directory))
                self._scp_ok(channel)
                channel.sendall(f"C0644 {len(payload)} {remote_name}\n".encode())
                self._scp_ok(channel)
                channel.sendall(payload)
                channel.sendall(b"\0")
                self._scp_ok(channel)
                channel.close()
                return
            except Exception:
                self.client.close()
                if attempt == 2:
                    raise
                self.client = self._new_client()
                time.sleep(1)

    @staticmethod
    def _scp_ok(channel) -> None:
        response = channel.recv(1)
        if response != b"\0":
            detail = channel.recv(1024).decode(errors="replace")
            raise RuntimeError("SCP rejected transfer: " + detail)


def main() -> int:
    args = arguments()
    try:
        if args.applet_only and (not args.no_restart or args.accept_new_host_key or not (args.backup_dir or args.restore_backup)):
            raise ValueError("applet-only requires --no-restart, trusted SSH and --backup-dir or --restore-backup")
        if args.restore_backup and not args.applet_only:
            raise ValueError("--restore-backup requires --applet-only")
        names, version = validate_package(args.zip_file, args.applet_only)
        user, host = split_target(args.target)
    except (ValueError, zipfile.BadZipFile) as exc:
        sys.stderr.write(f"deploy_dist: {exc}\n")
        return 2

    print(f"Validated StandaloneBase {version}: {args.zip_file.resolve()} ({len(names)} files)")
    if args.dry_run:
        print("Dry run complete; no connection or changes made.")
        return 0

    password = args.password if args.password is not None else os.environ.get("SB_DEVICE_PASSWORD")
    if password is None:
        password = getpass.getpass("SSH password: ")
    radio = Radio(user, host, password, args.port, args.accept_new_host_key)
    stage = ""
    try:
        radio.connect()
        if args.applet_only:
            if "Logitech Fab4 Board" not in radio.run("cat /proc/cpuinfo", attempts=1):
                raise RuntimeError("applet-only smoke deployment requires the verified Fab4 board")
            idle = json.loads(radio.run("wget -q -O - http://127.0.0.1:9000/api/nowplaying", attempts=1))
            if idle.get("active") or idle.get("state") != "stopped":
                raise RuntimeError("playback must be verified inactive before applet deployment")
            if not args.restore_backup:
                radio.run("test ! -e /mnt/storage/standalonebase/tinysc-arbitration.enabled && "
                          "test ! -L /mnt/storage/standalonebase/tinysc-arbitration.enabled && "
                          "test ! -e /var/run/standalonebase/tinysc.latch", attempts=1)
            result = deploy_applet_only(radio, args.zip_file, args.backup_dir, args.restore_backup)
            print(__import__('json').dumps(result))
            print("No Jive reload/restart; native binaries, configuration and firmware patches unchanged.")
            return 0
        candidate = radio.run("mktemp -d /tmp/standalonebase-deploy.XXXXXX").strip()
        if not re.fullmatch(r"/tmp/standalonebase-deploy\.[A-Za-z0-9]+", candidate):
            raise RuntimeError(f"unsafe staging path returned by radio: {candidate!r}")
        stage = candidate
        radio.upload(args.zip_file.resolve(), stage, "package.zip")
        qstage = shlex.quote(stage)
        # Old Dropbear connections can disappear after the command completed,
        # causing Radio.run() to retry it. Keep extraction idempotent so a
        # successful first attempt does not make the retry fail at mkdir.
        radio.run(f"mkdir -p {qstage}/files && unzip -oq {qstage}/package.zip -d {qstage}/files")
        radio.run(
            f"chmod 755 {qstage}/files/sb-storage-helper && "
            f"{qstage}/files/sb-storage-helper verify-module {qstage}/files/sbubifs-authorized.ko && "
            f"{qstage}/files/sb-storage-helper verify-sbdata-image {qstage}/files/sbdata-empty.ubifs"
        )
        for destination in TARGETS:
            radio.run("mkdir -p " + shlex.quote(destination))
            for name in names:
                source = f"{stage}/files/{name}"
                target = f"{destination}/{name}"
                mode = "755" if name in EXECUTABLES else "644"
                radio.run(
                    f"cp {shlex.quote(source)} {shlex.quote(target + '.new')} && "
                    f"chmod {mode} {shlex.quote(target + '.new')} && "
                    f"mv {shlex.quote(target + '.new')} {shlex.quote(target)}"
                )
        radio.run(
            "mkdir -p /mnt/storage/standalonebase && "
            f"cp {qstage}/files/config.json /mnt/storage/standalonebase/config.json.new && "
            "chmod 644 /mnt/storage/standalonebase/config.json.new && "
            "mv /mnt/storage/standalonebase/config.json.new /mnt/storage/standalonebase/config.json"
        )
        # Patch before restart: SlimServer.lua is loaded early in SqueezePlay,
        # before the StandaloneBase applet itself is initialized.
        radio.run(
            "/usr/share/jive/applets/StandaloneBase/patch-squeezeplay-artwork.sh"
        )
        if not args.no_restart:
            # The native services outlive SqueezePlay, so stop only the PIDs
            # whose command lines match this applet before restarting Jive.
            # Schedule this after the SSH command returns because old Dropbear
            # otherwise closes the channel without reporting an exit status.
            radio.run(
                "nohup sh -c 'sleep 1; "
                "for n in sbproxy sbbase sbwebserver; do "
                "p=/tmp/standalonebase/$n.pid; pid=$(cat \"$p\" 2>/dev/null); "
                "if test -n \"$pid\" && test -r /proc/$pid/cmdline && "
                "grep -q \"^/usr/share/jive/applets/StandaloneBase/$n\" /proc/$pid/cmdline; "
                "then kill \"$pid\"; fi; rm -f \"$p\"; done; "
                "/etc/init.d/squeezeplay restart' "
                ">/tmp/standalonebase/jive-restart.log 2>&1 </dev/null &"
            )
        print(f"Deployed StandaloneBase {version} to {user}@{host}.")
        print("SqueezePlay restart skipped." if args.no_restart else "SqueezePlay restarted; no radio reboot is required.")
        return 0
    except Exception as exc:
        sys.stderr.write(f"deploy_dist: deployment failed: {exc}\n")
        return 1
    finally:
        if stage:
            try:
                radio.run("rm -rf " + shlex.quote(stage), attempts=2)
            except Exception as exc:
                sys.stderr.write(f"deploy_dist: warning: could not remove {stage}: {exc}\n")
        radio.client.close()


if __name__ == "__main__":
    raise SystemExit(main())
