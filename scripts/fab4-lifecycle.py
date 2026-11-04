"""WP4a.2b qualified Fab4 lifecycle backup and deployment preparation.

Early boot TinySC and factory overlay loss are accepted by the owner. This tool
does not use the superseded six-file denial prototype installation policy.
"""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shlex
import struct
import tarfile
import time

REPO = Path(__file__).resolve().parents[1]
APP = "/usr/share/jive/applets/StandaloneBase"
REMOTE_BACKUP = "/mnt/storage/standalonebase/wp4a2b-backup"
POLICY = "/mnt/storage/standalonebase/tinysc-arbitration.enabled"
TARGETS = {
    "/etc/init.d/squeezecenter.stock": 0o400,
    APP + "/sb-tinysc-control": 0o755,
    APP + "/LifecycleHeartbeat.lua": 0o644,
    APP + "/sb-maintenance-helper": 0o755,
    APP + "/restart-actions.sh": 0o755,
    "/etc/init.d/squeezecenter": 0o755,
    "/usr/share/jive/applets/SqueezeCenter/SqueezeCenterApplet.lua": 0o644,
    "/etc/init.d/squeezeplay": 0o755,
    APP + "/StandaloneBaseApplet.lua": 0o644,
    APP + "/sbbase": 0o755,
    POLICY: 0o600,
}
AUDIT = {"/etc/watchdog.conf", "/usr/sbin/watchdog", "/etc/squeezeos.version",
         "/mnt/storage/standalonebase/config.json", APP + "/StandaloneBaseMeta.lua",
         APP + "/sbproxy", APP + "/sbwebserver", APP + "/sb-storage-helper",
         "/usr/share/jive/applets/SqueezeCenter/SqueezeCenterMeta.lua",
         "/usr/share/jive/applets/StandaloneRadio/StandaloneRadioMeta.lua",
         "/usr/share/jive/applets/SpotifyConnect/SpotifyConnectMeta.lua",
         "/usr/share/jive/applets/QobuzConnect/QobuzConnectMeta.lua"}
ALLOWED = set(TARGETS) | AUDIT


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


transport = module(REPO / "scripts/fab4-reliability.py", "fab4_readonly_transport")
patcher = module(REPO / "packaging/fab4-reliability/patch_tinysc_lifecycle.py", "fab4_lifecycle_patches")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + ".pending")
    with temporary.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(value, output, indent=2, sort_keys=True)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    temporary.replace(path)


class Device(transport.SSH):
    def __init__(self, host):
        if host != "192.168.1.244":
            raise ValueError("Only the owner-designated Fab4 development Touch is authorized")
        super().__init__(host)

    def script(self, text):
        # Legacy Dropbear rejects large exec command payloads. Keep the exec
        # request short and send generated, bounded shell source over stdin.
        return self.call("sh -s", data=text.encode("utf-8"))

    def inventory(self):
        expression = " ".join(shlex.quote(p) for p in sorted(ALLOWED))
        value = self.script('for p in ' + expression + '; do '
                          'if test -L "$p"; then printf "L:%s\\n" "$p"; '
                          'elif test -f "$p"; then printf "F:%s\\n" "$p"; '
                          'elif test -e "$p"; then printf "O:%s\\n" "$p"; '
                          'else printf "A:%s\\n" "$p"; fi; done').decode()
        rows = {}
        for line in value.splitlines():
            kind, path = line.split(":", 1)
            if path not in ALLOWED or path in rows or kind not in {"F", "A"}:
                raise RuntimeError("Unexpected/symlink target in read-only inventory: " + path)
            rows[path] = kind
        if set(rows) != ALLOWED:
            raise RuntimeError("Incomplete read-only inventory")
        return rows

    def read(self, path):
        if path not in ALLOWED:
            raise ValueError("Path outside lifecycle manifest")
        return self.call("test ! -L " + shlex.quote(path) + " && cat " + shlex.quote(path))


def backup(device, directory):
    version, mounts = device.profile()
    inventory = device.inventory()
    existing = sorted(p for p, kind in inventory.items() if kind == "F")
    archive = device.script("tar -cf - " + " ".join(map(shlex.quote, existing)) + "\n")
    directory.mkdir(mode=0o700, parents=True, exist_ok=False)
    (directory / "originals.tar").write_bytes(archive)
    records = []
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for member in source.getmembers():
            path = "/" + member.name.lstrip("/")
            if path not in existing or not member.isfile() or member.uid != 0 or member.gid != 0 or member.mode & ~0o777:
                raise RuntimeError("Unqualified archive member/ownership: " + path)
            data = source.extractfile(member).read()
            target = directory / "files" / path.lstrip("/")
            target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            target.write_bytes(data)
            records.append(dict(path=path, sha256=sha(data), bytes=len(data), mode=member.mode,
                                uid=member.uid, gid=member.gid, mtime=member.mtime))
    if {r["path"] for r in records} != set(existing) or len(records) != len(existing):
        raise RuntimeError("Incomplete/duplicate read-only backup")
    boot = device.call("cat /proc/sys/kernel/random/boot_id").decode().strip()
    (directory / "mounts.txt").write_text(mounts, encoding="utf-8")
    manifest = dict(schema="WP4A2B_SNAPSHOT_1", host="192.168.1.244", firmware=version,
                    boot=boot, created_utc=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                    archive_sha256=sha(archive), files=records,
                    absent=sorted(p for p, kind in inventory.items() if kind == "A"))
    save_json(directory / "snapshot.json", manifest)
    return manifest


def current_files(device):
    inventory = device.inventory()
    existing = {p for p, kind in inventory.items() if kind == "F"}
    archive = device.script("tar -cf - " + " ".join(map(shlex.quote, sorted(existing))) + "\n")
    records = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for member in source.getmembers():
            path = "/" + member.name.lstrip("/")
            if path not in existing or path in records or not member.isfile():
                raise ValueError("Unexpected current archive member")
            data = source.extractfile(member).read()
            records[path] = (dict(path=path, sha256=sha(data), mode=member.mode,
                                  uid=member.uid, gid=member.gid, mtime=member.mtime), data)
    if set(records) != existing:
        raise ValueError("Incomplete current target inventory")
    return records


def verify_state(snapshot, candidate, current, initial=False):
    baseline, original = load_snapshot(snapshot)
    plan, _ = load_plan(candidate)
    if sha((snapshot / "snapshot.json").read_bytes()) != plan["snapshot_sha256"]:
        raise ValueError("Plan does not match original snapshot")
    planned = {r["path"]: r for r in plan["files"]}
    for path in sorted(ALLOWED):
        seen = current.get(path)
        old = original.get(path)
        accepted = set()
        if old:
            record = old[0]
            accepted.add((record["sha256"], record["mode"], record["uid"], record["gid"]))
        if not initial and path in planned:
            row = planned[path]
            accepted.add((row["sha256"], row["mode"], 0, 0))
            if path == APP + "/StandaloneBaseApplet.lua":
                accepted.add((plan["recovery"]["applet_sha256"], 0o644, 0, 0))
        if seen:
            record = seen[0]
            if (record["sha256"], record["mode"], record["uid"], record["gid"]) not in accepted:
                raise RuntimeError("Unrecognized current file or metadata: " + path)
        elif old and (initial or path in AUDIT):
            raise RuntimeError("Required original/audit file is missing: " + path)
    return baseline, plan


def source_fingerprint():
    paths = {REPO / "CMakeLists.txt", REPO / "tests/CMakeLists.txt",
             REPO / "native/sbbase/CMakeLists.txt", REPO / "native/sbbase/src/main.c",
             REPO / "native/sbbase/src/music_runtime.inc", REPO / "native/sbbase/src/music_playback_glue.inc",
             REPO / "native/sbmusic/music_storage_linux.c", REPO / "native/sbstorage/sb_storage_helper.c"}
    for folder in ("native/common", "native/sblifecycle", "native/sbmaintenance", "applet", "tests/music"):
        paths.update(p for p in (REPO / folder).glob("*") if p.suffix in {".h", ".c", ".inc", ".py", ".lua"})
    paths.update((REPO / "scripts").glob("build*arm.sh"))
    paths.update(REPO / "scripts" / name for name in ("fab4-lifecycle.py", "fab4-reliability.py"))
    paths.update(p for p in (REPO / "packaging/fab4-reliability").glob("*") if p.suffix in {".py", ".sh"})
    paths.add(REPO / "packaging/build-package.sh")
    return {p.relative_to(REPO).as_posix(): sha(p.read_bytes()) for p in sorted(paths)}


REQUIRED_CHECKS = {"host_ctest", "lifecycle", "maintenance", "coordinator", "internal_copy",
                   "lua_overlay", "rollback", "baby_cross", "fab4_cross", "qemu", "sanitizers"}


def qualification_gate(candidate):
    plan, _ = load_plan(candidate)
    receipt = json.loads((candidate / "host-qualification.json").read_text())
    if receipt.get("schema") != "WP4A2B_HOST_QUALIFICATION_1" or receipt.get("sources") != source_fingerprint():
        raise RuntimeError("Host qualification is absent or source files changed")
    if sha((candidate / "plan.json").read_bytes()) != receipt.get("plan_sha256"):
        raise RuntimeError("Candidate changed after host qualification")
    if set(receipt.get("checks", {})) != REQUIRED_CHECKS or not all(receipt["checks"].values()):
        raise RuntimeError("Required host/recovery/build checks have not all passed")
    return plan


def helper(device, *arguments):
    command = " ".join(map(shlex.quote, [APP + "/sb-maintenance-helper", *map(str, arguments)]))
    return device.call(command)


def preflight(device, boot=None):
    version, mounts = device.profile()
    current_boot = device.call("cat /proc/sys/kernel/random/boot_id").decode().strip()
    if boot and boot != current_boot:
        raise RuntimeError("Boot changed since the qualified snapshot; refresh preflight")
    if " / /var rw - ramfs ramfs rw" not in mounts or " / /dev rw - tmpfs none rw" not in mounts:
        raise RuntimeError("Unexpected runtime filesystem mounts")
    status = json.loads(device.call("wget -q -O - http://127.0.0.1:9000/api/nowplaying"))
    if status.get("active") or status.get("state") != "stopped":
        raise RuntimeError("Audio is active or its idle state is unverified")
    # Read-only checks: no external Lua/boot extension or syslog redirection.
    device.script('set -eu\ntest ! -e /etc/init.d/rcS.local\n'
                  'test ! -e /media/sda1/squeezeos-boot.sh\n'
                  'test ! -d /media/sda1/squeezeplay\n'
                  'test ! -d /media/sda1/log\n'
                  'test ! -L /var/log/messages\n')
    device.script('set -eu\nfor p in /proc/[0-9]*/comm; do '
                  'test -r "$p" || continue; name=$(cat "$p" 2>/dev/null) || continue; '
                  'case "$name" in librespot|ogg-http-bridge|qobuz*|Qobuz*) '
                  'echo "Unexpected active audio application: $name" >&2; exit 1;; esac; done\n')
    if not any(' /media/sda1 ' in line and ' - vfat /dev/sda1 ' in line for line in mounts.splitlines()):
        raise RuntimeError("Expected USB mount is not present")
    filesystem = device.call("df -k /mnt/storage").decode().splitlines()[-1].split()
    if len(filesystem) < 6 or not filesystem[3].isdigit() or int(filesystem[3]) < 4096:
        raise RuntimeError("At least 4 MiB free internal overlay storage is required")
    return dict(firmware=version, boot=current_boot, nowplaying=status)


def attempt(candidate, kind, before):
    path = candidate / "hardware-attempts.json"
    value = json.loads(path.read_text()) if path.exists() else {}
    if kind in value:
        raise RuntimeError("An attempt is already recorded; no automatic retry: " + kind)
    if kind == "reboot" and (value.get("restart", {}).get("result", {}).get("state") != "COMPLETED" or
                             "after" not in value.get("restart", {})):
        raise RuntimeError("A verified successful Jive restart is required before reboot")
    value[kind] = dict(dispatched_utc=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), before=before)
    save_json(path, value)  # Durable before sending any restart/reboot request.
    return value


def restart(device, snapshot, candidate):
    qualification_gate(candidate)
    initial = preflight(device)
    verify_state(snapshot, candidate, current_files(device))
    profile, services = stack(device)
    initial.update(profile=profile, services=services)
    # The native preflight is repeated by the detached coordinator. No watched
    # process is terminated by the SSH client itself.
    records = attempt(candidate, "restart", initial)
    helper(device, "launch-restart")
    deadline = time.monotonic() + 100
    while time.monotonic() < deadline:
        status = json.loads(device.call("cat /var/run/standalonebase/restart.status"))
        if status["state"] in {"COMPLETED", "RECOVERED", "FAILED_HELD", "REFUSED"}:
            records["restart"]["result"] = status
            save_json(candidate / "hardware-attempts.json", records)
            if status["state"] != "COMPLETED":
                raise RuntimeError("Restart qualification failed; no reboot permitted: " + json.dumps(status))
            after = preflight(device, initial["boot"])
            current, current_services = stack(device)
            if current["jive_pid"] == profile["jive_pid"] or any(current[k] != profile[k] for k in ("feeder_pid", "feeder_start")):
                raise RuntimeError("Replacement Jive / unchanged feeder identity did not verify")
            after.update(profile=current, services=current_services,
                         lifecycle=json.loads(device.call("wget -q -O - http://127.0.0.1:9000/api/tinysc/lifecycle")))
            device.call("sh " + shlex.quote(REMOTE_BACKUP + "/check-installed.sh"))
            records["restart"]["after"] = after
            save_json(candidate / "hardware-attempts.json", records)
            return after
        time.sleep(0.5)
    raise RuntimeError("Restart status deadline expired; inspect supervision, do not retry")


def reboot(device, snapshot, candidate):
    qualification_gate(candidate)
    initial = preflight(device)
    profile, services = stack(device)
    initial.update(profile=profile, services=services)
    verify_state(snapshot, candidate, current_files(device))
    device.call("sh " + shlex.quote(REMOTE_BACKUP + "/check-installed.sh"))
    lifecycle = json.loads(device.call("wget -q -O - http://127.0.0.1:9000/api/tinysc/lifecycle"))
    if not (lifecycle.get("enabled") and lifecycle.get("operational") and lifecycle.get("fresh") and
            lifecycle.get("count") == 0 and lifecycle.get("state") == "STOPPED_VERIFIED"):
        raise RuntimeError("Arbitration is not verified before reboot")
    records = attempt(candidate, "reboot", initial)
    # SSH may close before its completion marker. That is not permission to
    # send reboot again. The local attempt journal already records dispatch.
    try:
        device.call("/sbin/reboot")
    except Exception:
        pass
    device.close()
    deadline = time.monotonic() + 180
    last = "not reconnected"
    while time.monotonic() < deadline:
        time.sleep(2)
        connection = None
        try:
            connection = Device("192.168.1.244")
            after = preflight(connection)
            if after["boot"] == initial["boot"]:
                raise RuntimeError("Waiting for the authorized reboot")
            current, current_services = stack(connection)
            health = json.loads(connection.call("wget -q -O - http://127.0.0.1:9000/api/tinysc/lifecycle"))
            if not (health.get("enabled") and health.get("operational") and health.get("fresh") and
                    health.get("count") == 0 and health.get("state") == "STOPPED_VERIFIED"):
                raise RuntimeError("Arbitration not yet ready after reboot")
            connection.call("sh " + shlex.quote(REMOTE_BACKUP + "/check-installed.sh"))
            after.update(profile=current, services=current_services, lifecycle=health)
            records["reboot"]["result"] = dict(state="COMPLETED", after=after)
            save_json(candidate / "hardware-attempts.json", records)
            return after
        except Exception as error:
            last = str(error)
        finally:
            if connection:
                connection.close()
    raise RuntimeError("Reboot qualification failed; do not retry: " + last)


def file_rollback(plan, observed, invoke, paths=None):
    """Restore only known revisions, in reverse deployment order."""
    for row in reversed(plan["files"]):
        path = row["path"]
        if paths is not None and path not in paths:
            continue
        current = observed[path][0]["sha256"] if path in observed else "absent"
        old = row["original"]
        allowed = {row["sha256"], "absent"}
        if old:
            allowed.add(old["sha256"])
        if path == APP + "/StandaloneBaseApplet.lua":
            allowed.add(plan["recovery"]["applet_sha256"])
        if current not in allowed:
            raise RuntimeError("Refuse rollback of an unrecognized revision: " + path)
        if old:
            invoke("install-file", REMOTE_BACKUP + "/" + old["source"], path, old["mode"], current)
        elif current != "absent":
            invoke("remove-file", path)


def bootstrap_helper(device, candidate):
    plan, _ = load_plan(candidate)
    ram = "/dev/standalonebase-wp4a2b"
    device.script('set -eu\numask 077\ntest ! -L /dev\ntest ! -e ' + shlex.quote(ram) +
                  '\nmkdir -m 700 ' + shlex.quote(ram) + '\n')
    row = next(r for r in plan["files"] if r["path"] == APP + "/sb-maintenance-helper")
    path = ram + "/helper"
    device.call('umask 077; set -C; cat > ' + shlex.quote(path), data=(candidate / row["source"]).read_bytes())
    if sha(device.call("cat " + shlex.quote(path))) != row["sha256"]:
        raise RuntimeError("RAM helper SHA-256 mismatch")
    device.call("chmod 500 " + shlex.quote(path))
    return lambda *args: device.call(" ".join(map(shlex.quote, [path, *map(str, args)])))


def cleanup_bootstrap(device):
    # These two fixed paths are created exclusively by bootstrap_helper.
    device.script('set -eu\nrm -f /dev/standalonebase-wp4a2b/helper\n'
                  'rmdir /dev/standalonebase-wp4a2b\n')


def stack(device):
    profile = json.loads(helper(device, "restart-preflight"))
    services = json.loads(helper(device, "verify-stack", profile["jive_pid"], profile["jive_start"]))
    return profile, services


def wait_base(device, profile, previous_pid, arbitration, timeout=60):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            current = json.loads(helper(device, "restart-preflight"))
            if any(current[key] != profile[key] for key in ("jive_pid", "jive_start", "feeder_pid", "feeder_start")):
                raise RuntimeError("Jive or watchdog changed during Base-only operation")
            services = json.loads(helper(device, "verify-stack", current["jive_pid"], current["jive_start"]))
            if services["base_pid"] == previous_pid:
                raise RuntimeError("Waiting for the existing applet supervisor to replace Base")
            if arbitration:
                health = json.loads(device.call("wget -q -O - http://127.0.0.1:9000/api/tinysc/lifecycle"))
                if not (health.get("enabled") and health.get("operational") and health.get("fresh") and
                        health.get("state") == "STOPPED_VERIFIED" and health.get("count") == 0 and
                        health.get("pid") == services["base_pid"] and health.get("start") == services["base_start"]):
                    raise RuntimeError("Base arbitration is not freshly verified")
                return dict(profile=current, services=services, lifecycle=health)
            status = json.loads(device.call("wget -q -O - http://127.0.0.1:9000/api/nowplaying"))
            if status.get("active") or status.get("state") != "stopped":
                raise RuntimeError("Recovered Base does not report inactive playback")
            return dict(profile=current, services=services, nowplaying=status)
        except Exception as error:
            last = str(error)
        time.sleep(0.5)
    raise RuntimeError("Base-only operation did not verify: " + str(last))


def activate(device, snapshot, candidate):
    qualification_gate(candidate)
    preflight(device)
    verify_state(snapshot, candidate, current_files(device))
    device.call("sh " + shlex.quote(REMOTE_BACKUP + "/check-installed.sh"))
    profile, services = stack(device)
    # Existing Jive is the sole service launcher; do not race it with a shell.
    helper(device, "terminate", services["base_pid"], services["base_start"])
    return wait_base(device, profile, services["base_pid"], True)


def rollback(device, snapshot, candidate):
    plan, _ = load_plan(candidate)
    preflight(device)
    observed = current_files(device)
    verify_state(snapshot, candidate, observed)
    profile, services = stack(device)
    invoke = bootstrap_helper(device, candidate)
    try:
        # Restore Base first, before its sole applet supervisor can relaunch it.
        base = next(r for r in plan["files"] if r["path"] == APP + "/sbbase")
        old = base["original"]
        invoke("install-file", REMOTE_BACKUP + "/" + old["source"], base["path"], old["mode"],
               observed[base["path"]][0]["sha256"])
        if POLICY in observed:
            invoke("remove-file", POLICY)
        invoke("terminate", services["base_pid"], services["base_start"])
        # Controller must release the quarantine while still installed. It
        # refuses a live Base lease or a native Music process.
        deadline = time.monotonic() + 10
        while True:
            try:
                device.call(shlex.quote(APP + "/sb-tinysc-control") + " release")
                break
            except Exception:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.2)
        file_rollback(plan, current_files(device), invoke)
        verify_state(snapshot, candidate, current_files(device), initial=True)
        # The on-disk maintenance helper has now been removed. Check through
        # the verified RAM copy until the original Base is supervised again.
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            current = json.loads(invoke("restart-preflight"))
            if any(current[k] != profile[k] for k in ("jive_pid", "jive_start", "feeder_pid", "feeder_start")):
                raise RuntimeError("Watchdog/Jive changed during rollback")
            try:
                restored = json.loads(invoke("verify-stack", current["jive_pid"], current["jive_start"]))
                if restored["base_pid"] != services["base_pid"]:
                    return dict(state="ROLLED_BACK", profile=current, services=restored, jive_restarted=False)
            except RuntimeError:
                pass
            time.sleep(0.5)
        raise RuntimeError("Original Base supervision did not return after rollback")
    finally:
        cleanup_bootstrap(device)


def install(device, snapshot, candidate):
    device.call("test ! -e /dev/standalonebase-wp4a2b && test ! -L /dev/standalonebase-wp4a2b")
    try:
        return install_staged(device, snapshot, candidate)
    finally:
        # The directory was absent before this operation; never recursively
        # remove it or follow a symlink during cleanup.
        device.script('if test -d /dev/standalonebase-wp4a2b && '
                      'test ! -L /dev/standalonebase-wp4a2b; then '
                      'test ! -L /dev/standalonebase-wp4a2b/helper && '
                      'rm -f /dev/standalonebase-wp4a2b/helper && '
                      'rmdir /dev/standalonebase-wp4a2b; fi\n')


def install_staged(device, snapshot, candidate):
    plan = qualification_gate(candidate)
    before = preflight(device, plan["boot"])
    verify_state(snapshot, candidate, current_files(device), initial=True)
    # Private tmpfs bootstrap: only the qualified production maintenance ELF.
    ram = "/dev/standalonebase-wp4a2b"
    device.script('set -eu\numask 077\ntest ! -L /dev\ntest ! -e ' + shlex.quote(ram) +
                  '\nmkdir -m 700 ' + shlex.quote(ram) + '\n')
    binary_row = next(r for r in plan["files"] if r["path"] == APP + "/sb-maintenance-helper")
    binary = (candidate / binary_row["source"]).read_bytes()
    temporary_helper = ram + "/helper"
    device.call('umask 077; set -C; cat > ' + shlex.quote(temporary_helper), data=binary)
    if sha(device.call("cat " + shlex.quote(temporary_helper))) != binary_row["sha256"]:
        raise RuntimeError("RAM bootstrap SHA-256 mismatch; no overlay files changed")
    device.call("chmod 500 " + shlex.quote(temporary_helper))

    def bootstrap(*arguments):
        return device.call(" ".join(map(shlex.quote, [temporary_helper, *map(str, arguments)])))

    watchdog = json.loads(bootstrap("restart-preflight"))
    services = json.loads(bootstrap("verify-stack", watchdog["jive_pid"], watchdog["jive_start"]))
    save_json(candidate / "hardware-preflight.json", dict(preflight=before, watchdog=watchdog, services=services))

    # Probe the actual parent without touching the USB filesystem. All copies
    # use descriptor-relative native rename and file/directory fsync.
    for parent in ("/etc/init.d", APP, "/usr/share/jive/applets/SqueezeCenter", "/mnt/storage/standalonebase"):
        bootstrap("sync-internal", parent)
    device.script('set -eu\numask 077\ntest ! -e ' + shlex.quote(REMOTE_BACKUP) +
                  '\nmkdir -m 700 ' + shlex.quote(REMOTE_BACKUP) + '\n')
    archive = payload_archive(candidate)
    if len(archive) > 4194304:
        raise RuntimeError("Recovery payload exceeds its 4 MiB bound")
    archive_path = REMOTE_BACKUP + "/payload.tar"
    device.call('umask 077; set -C; cat > ' + shlex.quote(archive_path), data=archive)
    if sha(device.call("cat " + shlex.quote(archive_path))) != sha(archive):
        raise RuntimeError("Recovery upload SHA-256 mismatch")
    device.call("tar -xpf " + shlex.quote(archive_path) + " -C " + shlex.quote(REMOTE_BACKUP))
    _, sources = load_plan(candidate)
    for source in sorted(sources):
        remote = REMOTE_BACKUP + "/" + source
        expected = sha((candidate / source).read_bytes())
        if bootstrap("sha256-internal", remote).decode().strip() != expected:
            raise RuntimeError("Unpacked recovery SHA-256 mismatch: " + source)
        bootstrap("sync-internal", remote)
    for path in (REMOTE_BACKUP + "/new", REMOTE_BACKUP + "/original", REMOTE_BACKUP + "/recovery",
                 REMOTE_BACKUP, "/mnt/storage/standalonebase"):
        bootstrap("sync-internal", path)
    # The complete known-good recovery is durable before the first replacement.
    device.script('set -eu\numask 077\ntest ! -L /var\ntest ! -L /var/run\n'
                  'if test -e /var/run/standalonebase; then test ! -L /var/run/standalonebase; '
                  'test -d /var/run/standalonebase; else mkdir -m 700 /var/run/standalonebase; fi\n'
                  'chown 0:0 /var/run/standalonebase\nchmod 700 /var/run/standalonebase\n')
    changed = []
    try:
        for row in plan["files"]:
            changed.append(row["path"])
            expected = row["original"]["sha256"] if row["original"] else "absent"
            bootstrap("install-file", REMOTE_BACKUP + "/" + row["source"], row["path"], row["mode"], expected)
            if bootstrap("sha256-internal", row["path"]).decode().strip() != row["sha256"]:
                raise RuntimeError("Installed SHA-256 mismatch: " + row["path"])
        verify_state(snapshot, candidate, current_files(device))
    except Exception:
        # This is file-only rollback. No Jive restart, reboot, playback or TinySC
        # start is attempted. The old GUI remains under original supervision.
        file_rollback(plan, current_files(device), bootstrap, changed)
        raise
    return dict(state="INSTALLED", device_modified=True, files=changed, preflight=before,
                jive_restarted=False, music_runtime=False)


def load_snapshot(directory):
    manifest = json.loads((directory / "snapshot.json").read_text())
    if manifest.get("schema") != "WP4A2B_SNAPSHOT_1" or manifest.get("host") != "192.168.1.244" or manifest.get("firmware") != "7.7.3 r16676":
        raise ValueError("Unqualified snapshot profile")
    if sha((directory / "originals.tar").read_bytes()) != manifest["archive_sha256"]:
        raise ValueError("Snapshot archive SHA-256 mismatch")
    files = {}
    for record in manifest["files"]:
        path = record["path"]
        if path not in ALLOWED or path in files or record["uid"] != 0 or record["gid"] != 0 or record["mode"] & ~0o777:
            raise ValueError("Invalid snapshot member")
        data = (directory / "files" / path.lstrip("/")).read_bytes()
        if sha(data) != record["sha256"] or len(data) != record["bytes"]:
            raise ValueError("Backup SHA-256 mismatch: " + path)
        files[path] = (record, data)
    absent = manifest["absent"]
    if len(absent) != len(set(absent)) or set(files) & set(absent) or set(files) | set(absent) != ALLOWED:
        raise ValueError("Incomplete snapshot membership")
    return manifest, files


def static_arm(data, music_forbidden=False):
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 40:
        raise ValueError("Expected 32-bit little-endian ARM ELF")
    offset = struct.unpack_from("<I", data, 28)[0]
    size, count = struct.unpack_from("<HH", data, 42)
    if size != 32 or not count or count > 64 or offset + size * count > len(data):
        raise ValueError("Invalid ARM program header bounds")
    for index in range(count):
        if struct.unpack_from("<I", data, offset + size * index)[0] in (2, 3):
            raise ValueError("Dynamic/interpreter dependency in maintenance candidate")
    if any(value in data for value in (b"SB_TEST_", b"SB_TS_TEST_")):
        raise ValueError("Host fixture override in production candidate")
    if music_forbidden and b"--music-root" in data:
        raise ValueError("Music playback runtime must not be installed in WP4a.2b")


def prepare(snapshot, arm, output):
    manifest, original = load_snapshot(snapshot)
    for required in ("/etc/init.d/squeezecenter", "/etc/init.d/squeezeplay",
                     "/usr/share/jive/applets/SqueezeCenter/SqueezeCenterApplet.lua",
                     APP + "/StandaloneBaseApplet.lua", APP + "/sbbase"):
        if required not in original:
            raise ValueError("Required original missing: " + required)
    binaries = {}
    for name in ("sbbase", "sb-tinysc-control", "sb-maintenance-helper"):
        data = (arm / name).read_bytes()
        static_arm(data, music_forbidden=name == "sbbase")
        abi = b"SBMAINT-WP4A2B-1" if name == "sb-maintenance-helper" else b"SBTS-WP4A2B-1"
        if abi not in data:
            raise ValueError("Missing WP4a.2b interface in candidate: " + name)
        if name == "sb-maintenance-helper" and any(token not in data for token in
                (b"install-file", b"sync-internal", b"sha256-internal", b"verify-stack", b"coordinate")):
            raise ValueError("Incomplete maintenance helper candidate")
        binaries[name] = data
    payload = {
        "/etc/init.d/squeezecenter.stock": patcher.patch_service(original["/etc/init.d/squeezecenter"][1]),
        "/etc/init.d/squeezecenter": b'#!/bin/sh\nexec /usr/share/jive/applets/StandaloneBase/sb-tinysc-control service "$@"\n',
        "/etc/init.d/squeezeplay": patcher.patch_squeezeplay(original["/etc/init.d/squeezeplay"][1]),
        "/usr/share/jive/applets/SqueezeCenter/SqueezeCenterApplet.lua": patcher.patch_applet(
            original["/usr/share/jive/applets/SqueezeCenter/SqueezeCenterApplet.lua"][1]),
        APP + "/StandaloneBaseApplet.lua": patcher.patch_base(original[APP + "/StandaloneBaseApplet.lua"][1]),
        APP + "/LifecycleHeartbeat.lua": (REPO / "applet/LifecycleHeartbeat.lua").read_bytes(),
        APP + "/restart-actions.sh": (REPO / "packaging/fab4-reliability/restart-actions.sh").read_bytes(),
        POLICY: b"WP4a.2b explicit arbitration opt-in; Music remains disabled\n",
    }
    payload.update({APP + "/" + name: data for name, data in binaries.items()})
    if set(payload) != set(TARGETS):
        raise ValueError("Incomplete deployment candidate")
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    rows = []
    for index, (path, mode) in enumerate(TARGETS.items()):
        relative = f"new/{index:03d}"
        target = output / relative
        target.parent.mkdir(mode=0o700, exist_ok=True)
        target.write_bytes(payload[path])
        row = dict(path=path, source=relative, sha256=sha(payload[path]), bytes=len(payload[path]), mode=mode)
        if path in original:
            record, data = original[path]
            old_source = f"original/{index:03d}"
            old_path = output / old_source
            old_path.parent.mkdir(mode=0o700, exist_ok=True)
            old_path.write_bytes(data)
            row["original"] = dict(record, source=old_source)
        else:
            row["original"] = None
        rows.append(row)
    recovery = output / "recovery"
    recovery.mkdir(mode=0o700)
    (recovery / "base").write_bytes(original[APP + "/sbbase"][1])
    (recovery / "applet").write_bytes(patcher.patch_base(original[APP + "/StandaloneBaseApplet.lua"][1], recovery=True))
    plan = dict(schema="WP4A2B_PLAN_1", host=manifest["host"], boot=manifest["boot"],
                snapshot_sha256=sha((snapshot / "snapshot.json").read_bytes()), files=rows,
                music_runtime=False, qualified_for_installation=False,
                recovery=dict(base_sha256=sha((recovery / "base").read_bytes()),
                              applet_sha256=sha((recovery / "applet").read_bytes())))
    scripts = build_recovery_scripts(plan, original)
    plan["scripts"] = []
    for name, data in scripts.items():
        (output / name).write_bytes(data)
        plan["scripts"].append(dict(source=name, sha256=sha(data), bytes=len(data)))
    save_json(output / "plan.json", plan)
    return plan


def build_recovery_scripts(plan, original):
    """All executable paths are fixed; original hashes come from the snapshot."""
    header = '#!/bin/sh\nset -eu\numask 077\nh=' + shlex.quote(APP + "/sb-maintenance-helper") + '\n'

    def check(path, digest):
        return 'test "$("$h" sha256-internal ' + shlex.quote(path) + ')" = ' + shlex.quote(digest) + '\n'

    installed, recovery = header, header
    for row in plan["files"]:
        installed += check(REMOTE_BACKUP + "/" + row["source"], row["sha256"])
        installed += check(row["path"], row["sha256"])
        if row["original"]:
            old = row["original"]
            recovery += check(REMOTE_BACKUP + "/" + old["source"], old["sha256"])
    for path in sorted(AUDIT):
        if path in original:
            installed += check(path, original[path][0]["sha256"])
        else:
            installed += 'test ! -e ' + shlex.quote(path) + ' && test ! -L ' + shlex.quote(path) + '\n'
    recovery += check(REMOTE_BACKUP + "/recovery/base", plan["recovery"]["base_sha256"])
    recovery += check(REMOTE_BACKUP + "/recovery/applet", plan["recovery"]["applet_sha256"])
    recover = header + 'sh ' + shlex.quote(REMOTE_BACKUP + "/check-recovery.sh") + '\n'
    for source, path, mode, digest in (
        ("recovery/base", APP + "/sbbase", 0o755, plan["recovery"]["base_sha256"]),
        ("recovery/applet", APP + "/StandaloneBaseApplet.lua", 0o644, plan["recovery"]["applet_sha256"]),
    ):
        recover += '"$h" install-file ' + shlex.quote(REMOTE_BACKUP + "/" + source) + ' ' + shlex.quote(path) + ' ' + str(mode) + '\n'
        recover += check(path, digest)
    # This is known-good Base recovery with quarantine retained, not full
    # owner rollback. It never starts TinySC or installs a Music runtime.
    return {"check-installed.sh": installed.encode(), "check-recovery.sh": recovery.encode(),
            "recover-base.sh": recover.encode()}


def load_plan(directory):
    value = json.loads((directory / "plan.json").read_text())
    if value.get("schema") != "WP4A2B_PLAN_1" or value.get("host") != "192.168.1.244" or value.get("music_runtime") is not False:
        raise ValueError("Invalid lifecycle plan")
    rows = value["files"]
    if [r["path"] for r in rows] != list(TARGETS):
        raise ValueError("Invalid deployment target/order")
    allowed_sources = set()
    for row in rows:
        if row["mode"] != TARGETS[row["path"]] or row["source"] != f'new/{list(TARGETS).index(row["path"]):03d}':
            raise ValueError("Invalid deployment file metadata")
        allowed_sources.add(row["source"])
        data = (directory / row["source"]).read_bytes()
        if sha(data) != row["sha256"] or len(data) != row["bytes"]:
            raise ValueError("Candidate SHA-256 mismatch: " + row["path"])
        if row["original"]:
            record = row["original"]
            expected = f'original/{list(TARGETS).index(row["path"]):03d}'
            if record["source"] != expected or record["path"] != row["path"] or record["uid"] != 0 or record["gid"] != 0:
                raise ValueError("Invalid original reference")
            allowed_sources.add(expected)
            if sha((directory / expected).read_bytes()) != record["sha256"]:
                raise ValueError("Original reference SHA-256 mismatch")
    if {r["source"] for r in value["scripts"]} != {"check-installed.sh", "check-recovery.sh", "recover-base.sh"}:
        raise ValueError("Incomplete recovery scripts")
    for row in value["scripts"]:
        allowed_sources.add(row["source"])
        if sha((directory / row["source"]).read_bytes()) != row["sha256"]:
            raise ValueError("Recovery script SHA-256 mismatch")
    for name in ("base", "applet"):
        allowed_sources.add("recovery/" + name)
        if sha((directory / "recovery" / name).read_bytes()) != value["recovery"][name + "_sha256"]:
            raise ValueError("Known-good recovery SHA-256 mismatch")
    return value, allowed_sources


def payload_archive(directory):
    plan, sources = load_plan(directory)
    mtimes = {r["original"]["source"]: int(r["original"]["mtime"])
              for r in plan["files"] if r["original"]}
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for folder in ("new", "original", "recovery"):
            entry = tarfile.TarInfo(folder)
            entry.type = tarfile.DIRTYPE
            entry.uid = entry.gid = 0
            entry.mode = 0o700
            archive.addfile(entry)
        for path in sorted(sources):
            data = (directory / path).read_bytes()
            entry = tarfile.TarInfo(path)
            entry.uid = entry.gid = 0
            entry.mode = 0o400
            entry.mtime = mtimes.get(path, 0)
            entry.size = len(data)
            archive.addfile(entry, io.BytesIO(data))
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["backup", "prepare", "install", "activate", "restart", "reboot", "rollback"])
    parser.add_argument("--host", default="192.168.1.244")
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--arm", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--candidate", type=Path)
    args = parser.parse_args()
    if args.command == "backup":
        device = Device(args.host)
        try:
            value = backup(device, args.snapshot)
        finally:
            device.close()
        print(json.dumps(dict(snapshot=str(args.snapshot), files=len(value["files"]), device_modified=False)))
    elif args.command == "prepare":
        if not args.arm or not args.output:
            parser.error("prepare requires --arm and --output")
        value = prepare(args.snapshot, args.arm, args.output)
        print(json.dumps(dict(candidate=str(args.output), files=len(value["files"]),
                              qualified_for_installation=False, device_modified=False)))
    else:
        if not args.candidate:
            parser.error("device operations require --candidate")
        device = Device(args.host)
        try:
            value = globals()[args.command](device, args.snapshot, args.candidate)
            save_json(args.candidate / (args.command + "-result.json"), value)
            print(json.dumps(value))
        finally:
            device.close()


if __name__ == "__main__":
    main()
