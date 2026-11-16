"""Regression for the ownership modules omitted from earlier network packages."""
import importlib.util
from pathlib import Path
import sys
import unittest
root=Path(sys.argv.pop(1))
spec=importlib.util.spec_from_file_location('release_check',root/'scripts/verify-release.py')
check=importlib.util.module_from_spec(spec);spec.loader.exec_module(check)
class Dependencies(unittest.TestCase):
    def test_network_runtime_closure(self):
        names=['NetworkMusic','NetworkProfile','PlaybackFenceV6','OwnershipHooks','OwnershipRuntime','OwnershipClient','OwnershipHttp','InstallerCompat','InstallerTransaction','Platform']
        members={name+'.lua':(root/'applet'/(name+'.lua')).read_bytes() for name in names}
        check.verify_lua_dependencies(members)
        for missing in ['OwnershipHooks.lua','OwnershipRuntime.lua','OwnershipClient.lua','OwnershipHttp.lua','InstallerTransaction.lua']:
            incomplete=dict(members);incomplete.pop(missing)
            with self.assertRaises(ValueError):check.verify_lua_dependencies(incomplete)
if __name__=='__main__':unittest.main()
