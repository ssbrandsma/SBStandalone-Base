#!/usr/bin/env python3
"""Rooted, write-instrumented layout/eligibility tests. Never access a device."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HELPER = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[2]
FIXTURE = json.loads((Path(__file__).parent / 'fixtures/baby_9_0_1_r17084_cramfs_bak_98lebs.json').read_text())
RUNNER = os.environ.get('SB_LAYOUT_RUNNER', '').split()


class LayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.spec = copy.deepcopy(FIXTURE)
        self.ubi = 'sys/class/ubi/ubi0/'
        self.vol = 'sys/class/ubi/ubi0_4/'
        self.populate()

    def put(self, name, value):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(str(value) + '\n')

    def populate(self):
        self.put('proc/cpuinfo', self.spec['cpuinfo'])
        self.put('proc/sys/kernel/architecture', 'armv5tejl')
        self.put('proc/sys/kernel/osrelease', self.spec['kernel'])
        self.put('etc/squeezeos.version', self.spec['firmware'])
        self.put('proc/mtd', self.spec['mtd'].rstrip())
        self.put('proc/mounts', self.spec['mounts'].format(root=self.root).rstrip())
        self.put('proc/filesystems', 'nodev\tubifs')
        self.put('etc/init.d/rcS', self.spec['boot_hook'].rstrip())
        self.put('dev/ubi0', '')
        self.put(self.ubi + 'dev', '253:0')
        # Synthetic format evidence, not a captured firmware payload.
        header = struct.pack('<IIII', 0x28cd3d45, 65536, 0, 0) + b'Compressed ROMFS' + bytes(32) + struct.pack('<III', 0x4000, 0, 0)
        (self.root / 'dev/mtd6').write_bytes(header)
        for key, value in self.spec['controller'].items():
            self.put(self.ubi + key, value)
        for volume in self.spec['volumes']:
            vid = volume['id']
            for key, value in dict(self.spec['volume_defaults'], **volume).items():
                if key != 'id':
                    self.put(f'sys/class/ubi/ubi0_{vid}/{key}', value)
            (self.root / self.ubi / f'ubi0_{vid}').symlink_to(f'../ubi0_{vid}', target_is_directory=True)
        self.app = self.root / 'usr/share/jive/applets/StandaloneBase'
        self.app.mkdir(parents=True)
        (self.app.parent / 'SetupAppletInstaller').mkdir()
        (self.root / 'mnt/storage').mkdir(parents=True)
        (self.root / 'tmp').mkdir()
        shutil.copy2(HELPER, self.app / 'sb-storage-helper')
        for src in ('scripts/storage-setup.sh', 'scripts/storage-boot.sh',
                    'artifacts/sbubifs-authorized.ko', 'artifacts/ubi/sbdata-empty.ubifs'):
            shutil.copy2(REPO / src, self.app / Path(src).name)
        # Any forbidden operation leaves evidence and fails. In particular no
        # stock UBI target may be written, including in mocked transactions.
        for tool in ('ubimkvol', 'ubiupdatevol', 'ubirmvol', 'ubirsvol',
                     'mount', 'umount', 'insmod', 'rmmod', 'flash_erase', 'nandwrite'):
            self.put('usr/sbin/' + tool, '#!/bin/sh\necho "$0 $*" >>"$SB_STORAGE_ROOT/tmp/mutations"\nexit 99')
            (self.root / 'usr/sbin' / tool).chmod(0o755)

    def env(self):
        return dict(os.environ, SB_STORAGE_ROOT=str(self.root), SB_STORAGE_APPLET_DIR=str(self.app),
                    PATH=str(self.root / 'usr/sbin') + ':' + os.environ['PATH'])

    def run_helper(self, expected=True):
        before = self.snapshot()
        r = subprocess.run(RUNNER + [str(HELPER), 'validate-layout'], env=self.env(), text=True, capture_output=True)
        self.assertEqual(r.returncode, 0 if expected else 2, r.stdout + r.stderr)
        self.assertEqual(before, self.snapshot(), 'native diagnostic wrote fixture files')
        self.assertFalse((self.root / 'tmp/mutations').exists())
        return r.stdout

    def snapshot(self, exclude_ram=False):
        return {str(p.relative_to(self.root)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in self.root.rglob('*') if p.is_file() and
                not (exclude_ram and str(p.relative_to(self.root)).startswith('tmp/'))}

    def check(self, command='check'):
        before = self.snapshot(exclude_ram=True)
        r = subprocess.run(['sh', str(self.app / 'storage-setup.sh'), command],
                           env=self.env(), text=True, capture_output=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(before, self.snapshot(exclude_ram=True), 'diagnostic changed persistent state')
        self.assertFalse((self.root / 'tmp/mutations').exists())
        return r.stdout

    def resize_backup(self, lebs=98, bad=1, target=0):
        self.put(self.vol + 'reserved_ebs', lebs)
        self.put(self.vol + 'data_bytes', lebs * 129024)
        self.put(self.ubi + 'bad_peb_count', bad)
        self.put(self.ubi + 'total_eraseblocks', 1014 - bad)
        self.put(self.ubi + 'avail_eraseblocks', 1014 - bad - (241 + lebs + target) - 14)
        mtd = self.spec['mtd'].replace('00c0f000', f'{lebs * 129024:08x}')
        self.put('proc/mtd', mtd.rstrip())

    def add_target(self, lebs):
        for key, value in dict(self.spec['volume_defaults'], name='sbdata',
                               reserved_ebs=lebs, data_bytes=lebs * 129024).items():
            self.put('sys/class/ubi/ubi0_5/' + key, value)
        self.put('dev/ubi0_5', '')
        self.put('sys/class/ubi/ubi0_5/dev', '253:6')
        self.resize_backup(target=lebs)

    def swapped_901(self, backup=113, bad=1, active=113):
        """Complete dependent inventory for the second user's A/B feedback."""
        spec = json.loads((Path(__file__).parent / 'fixtures/baby_9_0_1_r17084_ab_swapped_113lebs.json').read_text())
        spec['volumes'][1].update(reserved_ebs=backup, data_bytes=backup * 129024)
        spec['volumes'][4].update(reserved_ebs=active, data_bytes=active * 129024)
        for volume in spec['volumes']:
            for key, value in volume.items():
                if key != 'id':
                    self.put(f"sys/class/ubi/ubi0_{volume['id']}/{key}", value)
        self.put(self.ubi + 'total_eraseblocks', 1014 - bad)
        self.put(self.ubi + 'bad_peb_count', bad)
        self.put(self.ubi + 'avail_eraseblocks', 1014 - bad - sum(v['reserved_ebs'] for v in spec['volumes']) - 14)
        self.put('proc/mtd', 'dev: size erasesize name\nmtd0: 00080000 00020000 "redboot"\nmtd1: 07ec0000 00020000 "ubi"\n' +
                 ''.join(f"mtd{v['id']+2}: {v['data_bytes']:08x} 0001f800 \"{v['name']}\"\n" for v in spec['volumes']).rstrip())
        (self.root / 'dev/mtd6').rename(self.root / 'dev/mtd3')

    def test_feedback_swapped_901_645_free(self):
        self.swapped_901()
        out = self.run_helper()
        for expected in ('EXISTING_RESERVED_LEBS=354', 'AVAILABLE_LEBS=645',
                         'PLANNED_TARGET_LEBS=521', 'PLANNED_REMAINING_LEBS=124'):
            self.assertIn(expected, out)
        before = self.snapshot()
        # Native collect reads the device's canonical mount namespace; the
        # shell fixture uses rooted mountpoints to intercept host operations.
        mounts = self.root / 'proc/mounts'
        shell_mounts = mounts.read_bytes()
        self.put('proc/mounts', 'ubi0:ubifs /mnt/storage ubifs rw,noatime 0 0')
        native_before = self.snapshot()
        native = subprocess.run([str(HELPER), 'check'], env=self.env(), capture_output=True, text=True)
        self.assertEqual(native.returncode, 0, native.stdout + native.stderr)
        self.assertIn('compatible=true', native.stdout)
        self.assertEqual(native_before, self.snapshot())
        mounts.write_bytes(shell_mounts)
        self.assertEqual(before, self.snapshot())
        self.assertIn('INITIALIZATION_SUPPORTED=1', self.check())

    def test_feedback_frontend_is_independent_of_disabled_native_install(self):
        self.swapped_901()
        before = self.snapshot(exclude_ram=True)
        prepare = subprocess.run([str(HELPER), 'prepare'], env=self.env(), capture_output=True, text=True)
        self.assertIn('install_enabled=false', prepare.stdout)
        self.assertIn('install_scope=legacy_native_command', prepare.stdout)
        self.assertIn('initialization_frontend=storage-setup.sh', prepare.stdout)
        legacy = subprocess.run([str(HELPER), 'install'], env=self.env(), capture_output=True)
        self.assertEqual(legacy.returncode, 78)
        self.assertFalse((self.root / 'tmp/mutations').exists())
        initialize = subprocess.run(['sh', str(self.app / 'storage-setup.sh'), 'initialize',
                                     str(self.app / 'sbubifs-authorized.ko')],
                                    env=self.env(), capture_output=True)
        # The explicit frontend reaches only the mocked sbdata creation call.
        # Its intentional failure prevents any image write or mount.
        self.assertEqual(initialize.returncode, 31, initialize.stdout + initialize.stderr)
        calls = (self.root / 'tmp/mutations').read_text().splitlines()
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0], f'{self.root}/usr/sbin/ubimkvol {self.root}/dev/ubi0 -n 5 -N sbdata -S 521 -t dynamic')
        self.assertEqual(before, self.snapshot(exclude_ram=True))

    def test_feedback_swapped_901_backup_98(self):
        self.swapped_901(backup=98)
        self.run_helper()
        self.assertIn('INITIALIZATION_SUPPORTED=1', self.check())

    def test_feedback_swapped_901_backup_106(self):
        self.swapped_901(backup=106)
        self.run_helper()

    def test_feedback_swapped_901_599_free(self):
        self.swapped_901(bad=47)
        self.assertIn('insufficient_available_lebs', self.run_helper(False))

    def test_feedback_swapped_901_600_free(self):
        self.swapped_901(bad=46)
        self.run_helper()

    def test_feedback_swapped_901_601_free(self):
        self.swapped_901(bad=45)
        self.run_helper()

    def test_feedback_swapped_901_active_size_stays_strict(self):
        self.swapped_901(active=106)
        self.assertIn('volume_size_unsupported', self.run_helper(False))

    def test_feedback_swapped_901_mixed_bank_roles(self):
        self.swapped_901()
        self.put('sys/class/ubi/ubi0_1/name', 'cramfs')
        self.put('sys/class/ubi/ubi0_4/name', 'cramfs_bak')
        self.assertIn('volume_identity_mismatch', self.run_helper(False))

    def test_feedback_swapped_901_inconsistent_capacity(self):
        self.swapped_901()
        self.put(self.ubi + 'avail_eraseblocks', 646)
        self.assertIn('ubi_accounting_inconsistent', self.run_helper(False))

    def test_reported_eligibility_and_no_writes(self):
        out = self.run_helper()
        for line in ('DYNAMIC_LAYOUT_COMPATIBLE=1', 'EXISTING_RESERVED_LEBS=339',
                     'AVAILABLE_LEBS=660', 'PLANNED_TARGET_LEBS=521', 'RAW_AVAILABLE_BYTES=85155840'):
            self.assertIn(line, out)
        self.assertEqual(self.run_helper(), out)
        status = self.check()
        self.assertIn('INITIALIZATION_SUPPORTED=1', status)
        self.assertIn('STATUS=AVAILABLE', status)
        self.assertIn('DYNAMIC_LAYOUT_VALIDATION', self.check('analyze'))

    def test_minimum_600(self):
        self.resize_backup(106, bad=53)  # 1014 - 53 - 347 - 14 = 600
        self.run_helper()

    def test_below_minimum_599(self):
        self.resize_backup(106, bad=54)
        self.assertIn('insufficient_available_lebs', self.run_helper(False))

    def test_above_minimum_601(self):
        self.resize_backup(106, bad=52)
        self.run_helper()

    def test_concurrent_initialization(self):
        (self.root / 'tmp/sbstorage-operation.lock').mkdir()
        self.assertIn('INITIALIZATION_SUPPORTED=0', self.check())
        r = subprocess.run(['sh', str(self.app / 'storage-setup.sh'), 'initialize',
                            str(self.app / 'sbubifs-authorized.ko')], env=self.env(), capture_output=True)
        self.assertEqual(r.returncode, 20)
        self.assertFalse((self.root / 'tmp/mutations').exists())

    def test_stale_snapshot_extra_volume(self):
        self.assertIn('INITIALIZATION_SUPPORTED=1', self.check())
        self.put('sys/class/ubi/ubi0_6/name', 'foreign')
        r = subprocess.run(['sh', str(self.app / 'storage-setup.sh'), 'initialize',
                            str(self.app / 'sbubifs-authorized.ko')], env=self.env(), capture_output=True)
        self.assertEqual(r.returncode, 28)
        self.assertFalse((self.root / 'tmp/mutations').exists())

    def test_backup_cramfs_header(self):
        path = self.root / 'dev/mtd6'
        original = path.read_bytes()
        for data in (b'', b'x' * 76, original[:4] + struct.pack('<I', 20000000) + original[8:],
                     original[:8] + struct.pack('<I', 0x8000) + original[12:]):
            path.write_bytes(data)
            self.assertIn('backup_cramfs_image_unverified', self.run_helper(False))

    def test_multiline_firmware_identity(self):
        self.put('etc/squeezeos.version', FIXTURE['firmware'] + '\nbuilder date and source metadata\n' + 'x' * 200)
        self.run_helper()
        self.assertIn('INITIALIZATION_SUPPORTED=1', self.check())

    def test_active_target_without_device_node(self):
        self.add_target(521)
        (self.root / 'dev/ubi0_5').unlink()
        self.put('proc/filesystems', 'nodev ubifs\nnodev sbubifs')
        self.put('proc/mounts', FIXTURE['mounts'].format(root=self.root) +
                 f'ubi0:sbdata {self.root}/mnt/sbdata sbubifs rw 0 0\n' +
                 f'ubi0:sbdata {self.app.parent} sbubifs rw 0 0')
        out = self.check()
        self.assertIn('STATUS=ACTIVE', out)
        self.assertIn('REINITIALIZATION_SUPPORTED=0', out)

    def test_invalid_controller_device_identity(self):
        self.put(self.ubi + 'dev', 'invalid')
        self.assertIn('INITIALIZATION_SUPPORTED=0', self.check())

    def test_missing_target_device_identity(self):
        self.add_target(521)
        (self.root / 'sys/class/ubi/ubi0_5/dev').unlink()
        self.assertIn('REINITIALIZATION_SUPPORTED=0', self.check())

    def test_stale_target_character_node(self):
        self.add_target(521)
        (self.root / 'dev/ubi0_5').unlink()
        (self.root / 'dev/ubi0_5').symlink_to('/dev/null')
        r = subprocess.run([str(HELPER), 'validate-target-node'], env=self.env())
        self.assertEqual(r.returncode, 2)
        self.assertIn('REINITIALIZATION_SUPPORTED=0', self.check())
        r = subprocess.run(['sh', str(self.app / 'storage-setup.sh'), 'reinitialize',
                            '--confirm-erase-sbdata', str(self.app / 'sbubifs-authorized.ko')],
                           env=self.env(), capture_output=True)
        self.assertEqual(r.returncode, 21)
        self.assertFalse((self.root / 'tmp/mutations').exists())

    def test_module_image_pins(self):
        for name, field in [('sbubifs-authorized.ko', 'BUNDLED_MODULE_VALID'),
                            ('sbdata-empty.ubifs', 'SBDATA_IMAGE_VALID')]:
            p = self.app / name
            original = p.read_bytes()
            p.write_bytes(original + b'x')
            out = self.check()
            self.assertIn(field + '=0', out)
            self.assertIn('INITIALIZATION_SUPPORTED=0', out)
            p.write_bytes(original)

    def test_missing_stock_volume(self):
        shutil.rmtree(self.root / 'sys/class/ubi/ubi0_4')
        self.run_helper(False)

    def test_target_wrong_identity(self):
        self.add_target(521)
        self.put('sys/class/ubi/ubi0_5/name', 'foreign')
        self.run_helper(False)
        self.assertIn('REINITIALIZATION_SUPPORTED=0', self.check())

    def test_no_cpu_qualification(self):
        self.put('proc/cpuinfo', 'Processor : ARMv6\nHardware : Baby')
        (self.root / 'proc/sys/kernel/architecture').unlink()
        self.run_helper(False)

    def test_legacy_readonly_mtd_node(self):
        node = self.root / 'dev/mtd/6ro'
        node.parent.mkdir()
        (self.root / 'dev/mtd6').rename(node)
        self.run_helper()

    def test_flat_readonly_mtd_node(self):
        (self.root / 'dev/mtd6').rename(self.root / 'dev/mtd6ro')
        self.run_helper()

    def test_firmware_replacement_headroom(self):
        self.resize_backup(1, bad=158)  # 600 available; 79 retained < 112 needed
        self.assertIn('insufficient_firmware_replacement_capacity', self.run_helper(False))

    def test_stock_change_after_creation(self):
        self.put('usr/sbin/ubimkvol', '''#!/bin/sh
test "$*" = "$SB_STORAGE_ROOT/dev/ubi0 -n 5 -N sbdata -S 521 -t dynamic" || exit 91
v=$SB_STORAGE_ROOT/sys/class/ubi/ubi0_5
mkdir "$v"
echo 253:6 >"$v/dev"
for field in type alignment usable_eb_size corrupted upd_marker; do
    cp "$SB_STORAGE_ROOT/sys/class/ubi/ubi0_4/$field" "$v/$field"
done
echo sbdata >"$v/name"; echo 521 >"$v/reserved_ebs"; echo 67221504 >"$v/data_bytes"
touch "$SB_STORAGE_ROOT/dev/ubi0_5"
echo 139 >"$SB_STORAGE_ROOT/sys/class/ubi/ubi0/avail_eraseblocks"
echo changed >"$SB_STORAGE_ROOT/sys/class/ubi/ubi0_4/name"
''')
        r = subprocess.run(['sh', str(self.app / 'storage-setup.sh'), 'initialize',
                            str(self.app / 'sbubifs-authorized.ko')], env=self.env(), capture_output=True)
        self.assertEqual(r.returncode, 32, r.stdout + r.stderr)
        self.assertFalse((self.root / 'tmp/mutations').exists(), 'image writer reached after stock mutation')


def negative(name, path, value):
    def test(self):
        if value is None:
            (self.root / path).unlink()
        else:
            self.put(path, value)
        self.run_helper(False)
        self.assertIn('INITIALIZATION_SUPPORTED=0', self.check())
    setattr(LayoutTests, 'test_reject_' + name, test)


for field in FIXTURE['volume_defaults'] | {'reserved_ebs': 98, 'data_bytes': 12644352}:
    path = 'sys/class/ubi/ubi0_4/' + field
    negative(field + '_missing', path, None)
    negative(field + '_malformed', path, 'junk')
    if field != 'type':
        for tag, value in [('negative', '-1'), ('overflow', '4294967296'),
                           ('huge', '9' * 200), ('suffix', '1x'), ('empty', '')]:
            negative(field + '_' + tag, path, value)
for field in FIXTURE['controller']:
    path = 'sys/class/ubi/ubi0/' + field
    for tag, value in [('missing', None), ('negative', '-1'), ('overflow', '4294967296'), ('suffix', '1x')]:
        negative('controller_' + field + '_' + tag, path, value)
negative('mtd_attachment', 'sys/class/ubi/ubi0/mtd_num', 2)
negative('target_name_elsewhere', 'sys/class/ubi/ubi0_6/name', 'sbdata')
negative('unexpected_controller', 'sys/class/ubi/ubi1/total_eraseblocks', 1014)
negative('backup_header_missing', 'dev/mtd6', None)
negative('numeric_embedded_nul', 'sys/class/ubi/ubi0_4/reserved_ebs', '98\x00junk')
negative('firmware_embedded_nul', 'etc/squeezeos.version', '9.0.1 r17084\x00junk')
for name, path, value in [
    ('hardware', 'proc/cpuinfo', 'Processor : ARM926EJ-S\nHardware : Fab4'),
    ('kernel', 'proc/sys/kernel/osrelease', '2.6.26.9'),
    ('firmware', 'etc/squeezeos.version', '9.0.1 unknown'),
    ('identity', 'sys/class/ubi/ubi0_4/name', 'sbdata'),
    ('duplicate_name', 'sys/class/ubi/ubi0_4/name', 'cramfs'),
    ('static', 'sys/class/ubi/ubi0_4/type', 'static'),
    ('alignment', 'sys/class/ubi/ubi0_4/alignment', 2),
    ('corruption', 'sys/class/ubi/ubi0_4/corrupted', 1),
    ('update', 'sys/class/ubi/ubi0_4/upd_marker', 1),
    ('bad_reserve', 'sys/class/ubi/ubi0/reserved_for_bad', 9),
    ('accounting', 'sys/class/ubi/ubi0/avail_eraseblocks', 659),
    ('physical_mtd', 'proc/mtd', FIXTURE['mtd'].replace('07ec0000', '08000000')),
    ('mapped_mtd', 'proc/mtd', FIXTURE['mtd'].replace('00c0f000', '00d0b000')),
    ('mtd_overflow', 'proc/mtd', FIXTURE['mtd'].replace('07ec0000', '100000000')),
    ('mtd_duplicate', 'proc/mtd', FIXTURE['mtd'] + 'mtd1: 07ec0000 00020000 "ubi"\n'),
]:
    negative(name, path, value)

for size in (34, 80, 98, 99, 106, 112, 113):
    def test(self, size=size):
        self.resize_backup(size)
        self.run_helper()
    setattr(LayoutTests, f'test_backup_size_{size}', test)
for size in (0, 114):
    def test(self, size=size):
        self.resize_backup(size)
        self.run_helper(False)
    setattr(LayoutTests, f'test_reject_backup_size_{size}', test)
for bad in (0, 1, 2, 10, 50):
    def test(self, bad=bad):
        self.resize_backup(bad=bad)
        self.run_helper()
    setattr(LayoutTests, f'test_bad_blocks_{bad}', test)
for size in (14, 256, 520, 521):
    def test(self, size=size):
        self.add_target(size)
        self.run_helper()
        self.assertIn('INITIALIZATION_SUPPORTED=0', self.check())
    setattr(LayoutTests, f'test_existing_size_{size}', test)
for size in (0, 13, 522, 1015):
    def test(self, size=size):
        self.add_target(size)
        self.run_helper(False)
    setattr(LayoutTests, f'test_reject_existing_size_{size}', test)

for name, mount in [
    ('missing', ''),
    ('readonly', 'ubi0:ubifs {root}/mnt/storage ubifs ro 0 0'),
    ('wrong_source', 'ubi1:ubifs {root}/mnt/storage ubifs rw 0 0'),
    ('stale_target', FIXTURE['mounts'] + 'ubi0:sbdata {root}/elsewhere sbubifs rw 0 0'),
    ('foreign_target', FIXTURE['mounts'] + '/dev/other {root}/mnt/sbdata ext3 rw 0 0'),
    ('bind', FIXTURE['mounts'] + 'ubi0:sbdata {root}/usr/share/jive/applets sbubifs rw 0 0'),
]:
    def test(self, mount=mount):
        self.put('proc/mounts', mount.format(root=self.root).rstrip())
        self.assertIn('INITIALIZATION_SUPPORTED=0', self.check())
    setattr(LayoutTests, 'test_mount_' + name, test)

for name, command, code in [
    ('extra', 'mkdir "$SB_STORAGE_ROOT/sys/class/ubi/ubi0_6"', 28),
    ('free', 'echo 659 >"$SB_STORAGE_ROOT/sys/class/ubi/ubi0/avail_eraseblocks"', 28),
    ('stock', 'echo 1 >"$SB_STORAGE_ROOT/sys/class/ubi/ubi0_4/data_bytes"', 28),
    ('mount', 'echo >"$SB_STORAGE_ROOT/proc/mounts"', 28),
    ('module', 'echo x >>"$SB_STORAGE_APPLET_DIR/sbubifs-authorized.ko"', 22),
    ('image', 'echo x >>"$SB_STORAGE_APPLET_DIR/sbdata-empty.ubifs"', 23),
]:
    def test(self, command=command, code=code):
        self.assertIn('INITIALIZATION_SUPPORTED=1', self.check())
        self.put('bin/logger', '#!/bin/sh\nmessage=$(cat)\ncase "$message" in *STAGE=CREATING_VOLUME*)\n' + command + '\n;; esac\n')
        (self.root / 'bin/logger').chmod(0o755)
        r = subprocess.run(['sh', str(self.app / 'storage-setup.sh'), 'initialize',
                            str(self.app / 'sbubifs-authorized.ko')], env=self.env(), capture_output=True)
        self.assertEqual(r.returncode, code, r.stdout + r.stderr)
        self.assertFalse((self.root / 'tmp/mutations').exists())
    setattr(LayoutTests, 'test_change_immediately_before_create_' + name, test)

if __name__ == '__main__':
    unittest.main()
