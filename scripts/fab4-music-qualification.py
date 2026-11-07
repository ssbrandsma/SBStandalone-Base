"""WP4a.3b fixed-target preparation, reusing lifecycle transport and copier.

Preparation is offline. It never issues restart/stop/play commands. A plan is
not an installation authorization or a hardware qualification receipt.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("music_lifecycle", ROOT / "scripts/fab4-lifecycle.py")
life = importlib.util.module_from_spec(spec)
spec.loader.exec_module(life)
APP = life.APP
STAGE = "/mnt/storage/standalonebase/wp4a3b-backup"
LUA = ("MusicProduction", "MusicIdleFence", "MusicOwnership", "NativeMusicOwnership",
       "OwnershipBinding", "OwnershipClient", "OwnershipDecoder", "OwnershipHooks",
       "OwnershipHttp", "OwnershipRuntime", "LifecycleHeartbeat", "StandaloneBaseApplet")
TARGETS = {APP + "/" + n + ".lua": 0o644 for n in LUA}
TARGETS.update({APP + "/loadPriority.lua": 0o644,
                APP + "/sbbase-music": 0o755,
                APP + "/sbwebserver-music": 0o755,
                APP + "/sbmusic-session": 0o755,
                APP + "/sbmusic-metadata-helper": 0o755,
                APP + "/sb-maintenance-helper": 0o755,
                APP + "/restart-actions.sh": 0o755,
                "/etc/init.d/squeezeplay": 0o755,
                APP + "/sbbase": 0o755})
APPLICATIONS = {
    "/usr/share/jive/applets/StandaloneRadio/StreamPlayer.lua": ROOT.parents[1] / "squeezebox/applet/StandaloneRadio/StreamPlayer.lua",
    "/usr/share/jive/applets/SpotifyConnect/SpotifyPlayback.lua": ROOT.parents[1] / "SBSpotifyConnect/applet/SpotifyPlayback.lua",
    "/usr/share/jive/applets/SpotifyConnect/SpotifyService.lua": ROOT.parents[1] / "SBSpotifyConnect/applet/SpotifyService.lua",
}
TARGETS.update({path: 0o644 for path in APPLICATIONS})
AUDIT = life.ALLOWED - set(TARGETS)
AUDIT.add(APP + "/sbbase-core")


def configure():
    # The imported legacy workflow is isolated to this process. No legacy
    # receipt is accepted as qualification for the Music candidate.
    life.TARGETS = TARGETS
    life.AUDIT = AUDIT
    life.ALLOWED = set(TARGETS) | AUDIT
    life.REMOTE_BACKUP = STAGE


def sources():
    paths = {p for folder in ("native", "applet", "tests/music", "packaging/music-qualification")
             for p in (ROOT / folder).rglob("*")
             if p.is_file() and p.suffix in {".c", ".h", ".inc", ".lua", ".py", ".sh"}}
    paths.update(ROOT / n for n in ("CMakeLists.txt", "tests/CMakeLists.txt",
                 "scripts/fab4-music-qualification.py", "scripts/fab4-lifecycle.py"))
    paths.update((ROOT / "scripts").glob("build*arm.sh"))
    result = {p.relative_to(ROOT).as_posix(): life.sha(p.read_bytes()) for p in sorted(paths)}
    result.update({"installed-application:" + path: life.sha(source.read_bytes()) for path, source in APPLICATIONS.items()})
    return result


def check(path, digest):
    return 'test "$("$h" sha256-internal ' + shlex.quote(path) + ')" = ' + shlex.quote(digest) + '\n'


def recovery_wrapper():
    # The saved ELF accepts only library.db. Preserve that closed original
    # with an exact RAM-only rename, then reconstruct the known album once.
    return ('''#!/bin/sh
set -eu
root=/usr/share/jive/applets/StandaloneBase
if test -f /var/run/standalonebase/wp4a3b-recovery.enabled && (umask 077; set -C; : > /var/run/standalonebase/wp4a3b-recovery.attempted) 2>/dev/null; then
 db=/dev/standalone-music-test/library.db
 saved=/dev/standalone-music-test/library-before-wp4a3b-recovery.db
 test -f "$db" && test ! -L "$db"
 test ! -e "$saved" && test ! -L "$saved"
 test ! -e "$db-journal" && test ! -e "$db-wal" && test ! -e "$db-shm"
 mv "$db" "$saved"
 exec "$root/sbbase-music" "$@" --tinysc-arbitration --music-browse-only --music-scan --music-root '/media/sda1/mp3/Fleetwood Mac - Rumors-1977-FAF' --music-database /dev/standalone-music-test/library.db --music-helper "$root/sbmusic-metadata-helper"
fi
exec "$root/sbbase-core" "$@"
''').encode()


def running_script(expected):
    text = '#!/bin/sh\nset -eu\nh=' + shlex.quote(APP + "/sb-maintenance-helper") + '\n'
    text += 'stack=$("$h" verify-stack "$1" "$2")\n'
    for role in ("base", "proxy", "web", "session"):
        text += 'pid=$(printf "%s\\n" "$stack" | sed -n \'s/.*"' + role + '_pid":\\([0-9][0-9]*\\).*/\\1/p\')\n'
        text += 'start=$(printf "%s\\n" "$stack" | sed -n \'s/.*"' + role + '_start":"\\([0-9][0-9]*\\)".*/\\1/p\')\n'
        if expected.get(role):
            text += '"$h" verify-exe "$pid" "$start" ' + shlex.quote(expected[role]) + '\n'
        else:
            text += 'test "$pid:$start" = 0:0\n'
    return text.encode()


def scripts(plan, original):
    header = '#!/bin/sh\nset -eu\numask 077\nh=' + shlex.quote(APP + "/sb-maintenance-helper") + '\n'
    installed, recovery = header, header
    for row in plan["files"]:
        installed += check(STAGE + "/" + row["source"], row["sha256"])
        installed += check(row["path"], row["sha256"])
        if row["original"]:
            old = row["original"]
            recovery += check(STAGE + "/" + old["source"], old["sha256"])
    for path in sorted(AUDIT):
        if path in original:
            installed += check(path, original[path][0]["sha256"])
        else:
            installed += 'test ! -e ' + shlex.quote(path) + ' && test ! -L ' + shlex.quote(path) + '\n'
    recovery += check(STAGE + "/recovery/base", plan["recovery_wrapper_sha256"])
    recover = header + 'sh ' + shlex.quote(STAGE + "/check-recovery.sh") + '\n'
    recover += 'rm -f /var/run/standalonebase/music-idle-test.enabled\n'
    recover += '(set -C; : > /var/run/standalonebase/wp4a3b-recovery.enabled)\n'
    # The coordinator has already quiesced Jive and every captured child.
    # Retain qualified maintenance primitives and the TinySC quarantine.
    keep = {APP + "/sb-maintenance-helper", APP + "/restart-actions.sh", "/etc/init.d/squeezeplay", APP + "/sbbase"}
    for row in reversed(plan["files"]):
        if row["path"] in keep:
            continue
        old = row["original"]
        if old:
            recover += '"$h" install-file ' + ' '.join(map(shlex.quote,
                       [STAGE + "/" + old["source"], row["path"], str(old["mode"]), row["sha256"]])) + '\n'
            recover += check(row["path"], old["sha256"])
        else:
            recover += check(row["path"], row["sha256"])
            recover += '"$h" remove-file ' + shlex.quote(row["path"]) + '\n'
    wrapper = next(row for row in plan["files"] if row["path"] == APP + "/sbbase")
    recover += '"$h" install-file ' + ' '.join(map(shlex.quote, [STAGE + "/recovery/base", APP + "/sbbase", "493", wrapper["sha256"]])) + '\n'
    recover += check(APP + "/sbbase", plan["recovery_wrapper_sha256"])
    before = {"base": original[APP + "/sbbase-music"][0]["sha256"],
              "web": original[APP + "/sbwebserver"][0]["sha256"],
              "proxy": original[APP + "/sbproxy"][0]["sha256"]}
    by_path = {row["path"]: row for row in plan["files"]}
    after = {"base": by_path[APP + "/sbbase-music"]["sha256"],
             "web": by_path[APP + "/sbwebserver-music"]["sha256"],
             "session": by_path[APP + "/sbmusic-session"]["sha256"], "proxy": before["proxy"]}
    return {"check-installed.sh": installed.encode(), "check-recovery.sh": recovery.encode(), "recover-base.sh": recover.encode(),
            "check-running-before.sh": running_script(before), "check-running-after.sh": running_script(after),
            "check-running-recovery.sh": running_script(before)}


def prepare(snapshot, output):
    configure()
    manifest, original = life.load_snapshot(snapshot)
    for name in ("sbbase", "sbbase-core", "sbbase-music", "sbmusic-metadata-helper", "StandaloneBaseApplet.lua"):
        if APP + "/" + name not in original:
            raise ValueError("Current browsing recovery member missing: " + name)
    if b"LifecycleHeartbeat" not in original[APP + "/StandaloneBaseApplet.lua"][1]:
        raise ValueError("Saved recovery applet lacks independent UI heartbeat")
    payload = {APP + "/" + name + ".lua": (ROOT / "applet" / (name + ".lua")).read_bytes() for name in LUA}
    payload.update({path: source.read_bytes() for path, source in APPLICATIONS.items()})
    payload[APP + "/loadPriority.lua"] = (ROOT / "packaging/music-qualification/loadPriority.lua").read_bytes()
    for name, source in {"sbbase-music": "music-session/sbbase_music", "sbwebserver-music": "music-session/sbwebserver_music",
                         "sbmusic-session": "music-session/sbmusic-session", "sbmusic-metadata-helper": "music/sbmusic-metadata-helper",
                         "sb-maintenance-helper": "maintenance/sb-maintenance-helper"}.items():
        data = (ROOT / "build-arm/fab4" / source).read_bytes()
        life.static_arm(data)
        payload[APP + "/" + name] = data
    stock = original["/etc/init.d/squeezeplay"][1]
    patched = life.sha(stock) == "cfaee0ebc2c40daba44f316cbd1789dd3a34849ef7f7ba1c352a9db97c0f3d45"
    payload["/etc/init.d/squeezeplay"] = stock if patched else life.patcher.patch_squeezeplay(stock)
    payload[APP + "/sbbase"] = original[APP + "/sbbase"][1]
    actions = (ROOT / "packaging/fab4-reliability/restart-actions.sh").read_text()
    actions = actions.replace("wp4a2b-backup", "wp4a3b-backup")
    actions = actions.replace('    "$helper" verify-stack "$2" "$3" >/dev/null\n',
                              '    sh "$backup/check-running-before.sh" "$2" "$3"\n')
    actions = actions.replace(' health)\n    ui_stack "$@"\n',
                              ' health)\n    ui_stack "$@"\n    sh "$backup/check-running-after.sh" "$2" "$3"\n')
    actions = actions.replace(' health-recovery)\n    ui_stack "$@"\n',
                              ' health-recovery)\n    ui_stack "$@"\n    sh "$backup/check-running-recovery.sh" "$2" "$3"\n')
    payload[APP + "/restart-actions.sh"] = actions.encode()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    rows = []
    for index, (path, mode) in enumerate(TARGETS.items()):
        source = f"new/{index:03d}"
        (output / "new").mkdir(mode=0o700, exist_ok=True)
        data = payload[path]
        (output / source).write_bytes(data)
        row = dict(path=path, source=source, mode=mode, sha256=life.sha(data), bytes=len(data), original=None)
        if path in original:
            old, data = original[path]
            old_source = f"original/{index:03d}"
            (output / "original").mkdir(mode=0o700, exist_ok=True)
            (output / old_source).write_bytes(data)
            row["original"] = dict(old, source=old_source)
        rows.append(row)
    (output / "recovery").mkdir(mode=0o700)
    (output / "recovery/base").write_bytes(recovery_wrapper())
    plan = dict(schema="WP4A3B_PLAN_1", host="192.168.1.244", boot=manifest["boot"],
                snapshot_sha256=life.sha((snapshot / "snapshot.json").read_bytes()),
                sources=sources(), files=rows, qualified_for_installation=False,
                recovery_wrapper_sha256=life.sha(recovery_wrapper()), scripts=[])
    plan["recovery"] = dict(applet_sha256=original[APP + "/StandaloneBaseApplet.lua"][0]["sha256"])
    for name, data in scripts(plan, original).items():
        (output / name).write_bytes(data)
        plan["scripts"].append(dict(source=name, sha256=life.sha(data), bytes=len(data)))
    life.save_json(output / "plan.json", plan)
    return plan


def load_plan(directory):
    plan = json.loads((directory / "plan.json").read_text())
    if plan.get("schema") != "WP4A3B_PLAN_1" or plan.get("host") != "192.168.1.244" or plan.get("sources") != sources():
        raise ValueError("Stale or invalid production Music plan")
    if [r["path"] for r in plan["files"]] != list(TARGETS):
        raise ValueError("Unexpected Music deployment target/order")
    for index, row in enumerate(plan["files"]):
        if row["mode"] != TARGETS[row["path"]] or row["source"] != f"new/{index:03d}":
            raise ValueError("Unexpected candidate member")
        data = (directory / row["source"]).read_bytes()
        if len(data) != row["bytes"] or life.sha(data) != row["sha256"]:
            raise ValueError("Candidate changed")
        old = row["original"]
        if old:
            if old["source"] != f"original/{index:03d}" or old["path"] != row["path"] or old["uid"] or old["gid"]:
                raise ValueError("Unexpected recovery reference")
            if life.sha((directory / old["source"]).read_bytes()) != old["sha256"]:
                raise ValueError("Recovery changed")
    if {r["source"] for r in plan["scripts"]} != {"check-installed.sh", "check-recovery.sh", "recover-base.sh",
              "check-running-before.sh", "check-running-after.sh", "check-running-recovery.sh"}:
        raise ValueError("Incomplete recovery scripts")
    for row in plan["scripts"]:
        if life.sha((directory / row["source"]).read_bytes()) != row["sha256"]:
            raise ValueError("Recovery script changed")
    if life.sha((directory / "recovery/base").read_bytes()) != plan["recovery_wrapper_sha256"]:
        raise ValueError("Recovery wrapper changed")
    return plan


def members(plan):
    result = {row["source"] for row in plan["files"]}
    result.update(row["original"]["source"] for row in plan["files"] if row["original"])
    result.update(row["source"] for row in plan["scripts"])
    result.add("recovery/base")
    return result


CHECKS = {"host_ctest", "music_fence", "lua_composition", "radio", "spotify",
          "cross_baby_fab4", "qemu", "sanitizers"}


def qualification_gate(directory):
    plan = load_plan(directory)
    receipt = json.loads((directory / "host-qualification.json").read_text())
    if (receipt.get("schema") != "WP4A3B_HOST_QUALIFICATION_1" or receipt.get("sources") != sources() or
            receipt.get("plan_sha256") != life.sha((directory / "plan.json").read_bytes()) or
            set(receipt.get("checks", {})) != CHECKS):
        raise RuntimeError("Current production qualification receipt required")
    for row in receipt["checks"].values():
        path = directory.parent / row["log"]
        if row["returncode"] != 0 or life.sha(path.read_bytes()) != row["sha256"]:
            raise RuntimeError("Missing, failed or changed qualification evidence")
    return plan


def install(device, snapshot, candidate):
    plan = qualification_gate(candidate)
    initial = life.preflight(device, plan["boot"])
    observed = life.current_files(device)
    life.verify_state(snapshot, candidate, observed, initial=True)
    # Count no space credit for old files/live inodes: these already consume
    # the measured filesystem usage. Allow all new destinations plus a full
    # atomic rollback copy while retaining the staged recovery, and 512 KiB.
    block = lambda size: ((size + 4095) // 4096) * 4096
    staged = sum(block((candidate / path).stat().st_size) for path in members(plan))
    targets = sum(block(row["bytes"]) for row in plan["files"]
                  if not row["original"] or row["sha256"] != row["original"]["sha256"])
    temporary = max(block(max(row["bytes"], row["original"]["bytes"] if row["original"] else 0)) for row in plan["files"])
    required = staged + targets + temporary + 524288
    free = int(device.call("df -k /mnt/storage").decode().splitlines()[-1].split()[3]) * 1024
    life.save_json(candidate / "space-plan.json", dict(required=required, available=free, preflight=initial))
    if required > free:
        raise RuntimeError("Insufficient overlay space for durable recovery, live inodes and atomic replacement")
    return life.install(device, snapshot, candidate, individual=True)


def restart_space_kib(plan):
    # Everything needed for recovery is already staged and hash-checked.
    # Only one atomic replacement is live at a time. Never credit the old
    # executable inodes becoming free after the coordinator stops Jive.
    largest=max(max(row["bytes"],row["original"]["bytes"] if row["original"] else 0) for row in plan["files"])
    return ((largest+4095)//4096)*4+512


def bind_lifecycle(candidate=None):
    configure()
    life.load_plan = lambda directory: (load_plan(directory), members(load_plan(directory)))
    life.qualification_gate = qualification_gate
    if candidate is not None:
        plan=qualification_gate(candidate)
        original_preflight=life.preflight
        life.preflight=lambda device,boot=None: original_preflight(device,boot,minimum_free_kib=restart_space_kib(plan))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("backup", "prepare", "verify", "install", "restart"))
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    configure()
    if args.command == "backup":
        device = life.Device("192.168.1.244")
        try:
            life.backup(device, args.snapshot)
        finally:
            device.close()
    elif args.command == "prepare":
        if not args.output:
            parser.error("prepare requires --output")
        prepare(args.snapshot, args.output)
    elif args.command == "verify":
        if not args.output:
            parser.error("verify requires --output")
        load_plan(args.output)
    else:
        if not args.output:
            parser.error("installation/restart requires --output")
        bind_lifecycle(args.output if args.command=="restart" else None)
        # The operator's prior idle confirmation is recorded separately from
        # the native proof. It never grants decoder ownership.
        if os.environ.get("SB_MUSIC_OWNER_CONFIRMED_IDLE") != "1":
            raise RuntimeError("Owner idle confirmation required before service replacement")
        device = life.Device("192.168.1.244")
        try:
            if args.command == "install":
                value = install(device, args.snapshot, args.output)
            else:
                plan = qualification_gate(args.output)
                life.preflight(device, plan["boot"])
                # All persistent recovery is installed and verified first.
                device.call("sh " + shlex.quote(STAGE + "/check-installed.sh"))
                device.call("sh " + shlex.quote(STAGE + "/check-recovery.sh"))
                device.script('set -eu\numask 077\n'
                    'test ! -e /dev/standalone-music-test/library-wp4a3b.db\n'
                    'test ! -e /dev/standalone-music-test/library-before-wp4a3b-recovery.db\n'
                    'test ! -e /var/run/sb-music-owner && test ! -L /var/run/sb-music-owner\n'
                    'mkdir -m 700 /var/run/sb-music-owner\n'
                    'set -C\n: > /var/run/standalonebase/music-idle-test.enabled\n')
                value = life.restart(device, args.snapshot, args.output)
            life.save_json(args.output / (args.command + "-result.json"), value)
            print(json.dumps(value))
        finally:
            device.close()
        return
    print(json.dumps(dict(state="PREPARED_UNQUALIFIED", device_modified=False)))


if __name__ == "__main__":
    main()
