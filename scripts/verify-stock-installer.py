"""Use original Jive ZIP/SHA1/LTN12 modules against the production adapter.

The network delivery/platform identity are simulated. Native ZIP extraction,
stream hashing, transaction filesystem operations and full package gates are real.
Original modules are independently compiled from the documented reference commit;
they are test inputs and are not included in product ZIPs.
"""
import importlib.util
from pathlib import Path
import sys

base,fixture,reference,release=(Path(value).resolve() for value in sys.argv[1:5])
sys.argv=[sys.argv[0],str(base),str(fixture)]
spec=importlib.util.spec_from_file_location('stock_fixture',base/'tests/images/test_installer_transaction.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
for target in ('baby','fab4'):
    test=module.Transaction();test.setUp()
    try:
        test.run_stock_adapter(native_directory=reference,package_path=release/f'StandaloneBase-{(base/"VERSION").read_text().strip()}-{target}.zip')
        print(target,'original C zipfilter/SHA1 + original LTN12: streamed final ZIP, full native/Lua/target gate and commit passed',flush=True)
    finally:test.doCleanups()
for fault in ('checksum','disk-full'):
    test=module.Transaction();test.setUp()
    try:
        test.run_stock_adapter(native_directory=reference,checksum_ok=fault!='checksum',disk_full=fault=='disk-full')
        print(fault,'original native stream modules retained old applet and blocked stock activation',flush=True)
    finally:test.doCleanups()

# These archives have valid stream checksums; rejection must come from the runtime contract.
import tempfile,zipfile,json
with tempfile.TemporaryDirectory() as directory:
    baby=release/f'StandaloneBase-{(base/"VERSION").read_text().strip()}-baby.zip'
    wrong=Path(directory)/'wrong-wire.zip'
    with zipfile.ZipFile(baby) as original,zipfile.ZipFile(wrong,'w') as changed:
        for info in original.infolist():
            data=original.read(info.filename)
            if info.filename=='network-music.compat.json':
                value=json.loads(data);value['resource_wire']=2;data=json.dumps(value).encode()
            changed.writestr(info,data)
    for fault,arguments in [
        ('wrong-target',dict(package_path=baby,platform_override='fab4')),
        ('wrong-wire',dict(package_path=wrong)),
        ('chmod-failure',dict(package_path=baby,chmod_fail=True)),
    ]:
        test=module.Transaction();test.setUp()
        try:
            test.run_stock_adapter(native_directory=reference,expect_failure=True,**arguments)
            print(fault,'retained old tree and refused activation',flush=True)
        finally:test.doCleanups()
