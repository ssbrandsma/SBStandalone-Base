"""Exercise publication/refusal/rollback against temporary local files only."""
import hashlib
import json
import os
from pathlib import Path
import pwd
import shlex
import subprocess
import sys
import tempfile
import unittest
from xml.etree import ElementTree as ET

SCRIPT = Path(sys.argv.pop(1)).resolve()


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base, self.radio, self.stage = [self.root / name for name in ('base', 'radio', 'stage')]
        for directory in (self.base, self.radio, self.stage, self.root / 'bin'):
            directory.mkdir()
        self.entries, self.artifacts = [], []
        for target in ('baby', 'fab4'):
            name = f'StandaloneBase-0.7.2-{target}.zip'
            (self.base / name).write_bytes(('old-' + target).encode())
            data = ('new-' + target).encode()
            (self.stage / name).write_bytes(data)
            self.artifacts.append(dict(file=name, sha1=hashlib.sha1(data).hexdigest(), sha256=hashlib.sha256(data).hexdigest()))
            self.entries.append(dict(name='StandaloneBase', target=target, version='0.7.2', url='http://example/' + name, sha=hashlib.sha1(data).hexdigest()))
        name = 'StandaloneRadio-0.9.20.zip'
        data = b'unchanged-radio'
        (self.radio / name).write_bytes(data)
        (self.stage / name).write_bytes(data)
        self.artifacts.append(dict(file=name, sha1=hashlib.sha1(data).hexdigest(), sha256=hashlib.sha256(data).hexdigest()))
        for target in ('baby', 'fab4'):
            self.entries.append(dict(name='StandaloneRadio', target=target, version='0.9.20', url='http://example/' + name, sha=hashlib.sha1(data).hexdigest()))
        self.manifest = self.base / 'release-0.7.2.json'
        self.manifest.write_text('{"old":true}')
        self.live = {
            'merged-extensions.xml': self.root / 'merged.xml',
            'base-extensions.xml': self.base / 'extensions.xml',
            'radio-extensions.xml': self.radio / 'extensions.xml',
            'bootstrap-config.json': self.root / 'bootstrap.json',
            'public-config.json': self.base / 'config.json',
        }
        for name, live in self.live.items():
            if name.endswith('.xml'):
                names = {'StandaloneBase'} if name.startswith('base-') else {'StandaloneRadio'} if name.startswith('radio-') else {'StandaloneBase', 'StandaloneRadio'}
                xml = ET.Element('extensions')
                apps = ET.SubElement(xml, 'applets')
                for a in self.entries:
                    if a['name'] not in names:
                        continue
                    entry = ET.SubElement(apps, 'applet', name=a['name'], target=a['target'], version=a['version'])
                    ET.SubElement(entry, 'url').text = a['url']
                    ET.SubElement(entry, 'sha').text = a['sha']
                (self.stage / name).write_bytes(ET.tostring(xml))
                live.write_text('<extensions><applets/></extensions>')
                Path(str(live) + '.sha1').write_text(hashlib.sha1(live.read_bytes()).hexdigest() + '\n')
            else:
                (self.stage / name).write_text(json.dumps(dict(applets=self.entries)))
                live.write_text('{"applets":[],"old":true}')
        (self.stage / 'release-0.7.2.json').write_text(json.dumps(dict(version='0.7.2', applets=self.entries, artifacts=self.artifacts)))
        (self.stage / 'preconditions.json').write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [*self.live.values(), self.manifest, *self.base.glob('*.zip')]}))
        owner = pwd.getpwuid(os.getuid()).pw_name
        settings = dict(BASE_ROOT=self.base, RADIO_ROOT=self.radio, MERGED_XML=self.live['merged-extensions.xml'], BOOTSTRAP_CONFIG=self.live['bootstrap-config.json'], WEB_OWNER=owner, BOOTSTRAP_OWNER=owner, CONTAINER_NAME='test-bootstrap')
        self.config = self.root / 'deployment.env'
        self.config.write_text('\n'.join(k + '=' + shlex.quote(str(v)) for k, v in settings.items()))
        docker = self.root / 'bin/docker'
        docker.write_text('#!/bin/sh\necho "$1" >>"$DOCKER_TRACE"\ncase "$1" in restart) test "${FAIL_RESTART:-0}" = 0;; ps) echo "test-bootstrap Up";; *) exit 1;; esac\n')
        docker.chmod(0o755)
        # Only ownership is abstracted: real copies, modes, renames and hashes
        # are exercised in the temporary tree without requiring host root.
        install = self.root / 'bin/install'
        install.write_text('#!/usr/bin/env python3\nimport os,sys\na=sys.argv[1:]\nfor opt in ("-o","-g"):\n while opt in a:\n  i=a.index(opt);del a[i:i+2]\nos.execv("/usr/bin/install",["install",*a])\n')
        install.chmod(0o755)
        self.before = self.snapshot()

    def snapshot(self):
        files = [*self.live.values(), self.manifest, *self.base.glob('*.zip'), *self.radio.glob('*.zip')]
        return {str(p): p.read_bytes() for p in files}

    def deploy(self, replacement=None, fail=False):
        env = dict(os.environ, SB_DEPLOY_CONFIG=str(self.config), SB_RELEASE_STAGE=str(self.stage), SB_RELEASE_VERSION='0.7.2', SB_RELEASE_BACKUP_ROOT=str(self.root / 'backups'), PATH=str(self.root / 'bin') + ':' + os.environ['PATH'], FAIL_RESTART='1' if fail else '0', DOCKER_TRACE=str(self.root / 'docker-trace'))
        env.pop('SB_REPLACE_RELEASE_VERSION', None)
        if replacement is not None:
            env['SB_REPLACE_RELEASE_VERSION'] = replacement
        return subprocess.run(['sh', str(SCRIPT)], env=env, capture_output=True, text=True)

    def test_published_bytes_remain_immutable_by_default(self):
        result = self.deploy()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Refusing replacement', result.stderr)
        self.assertEqual(self.before, self.snapshot())

    def test_replacement_requires_exact_version(self):
        self.assertNotEqual(self.deploy('0.7.3').returncode, 0)
        self.assertEqual(self.before, self.snapshot())

    def test_authorized_replacement_backs_up_old_archives(self):
        result = self.deploy('0.7.2')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'docker-trace').read_text().splitlines().count('restart'), 1)
        backup = next((self.root / 'backups').iterdir())
        for target in ('baby', 'fab4'):
            name = f'StandaloneBase-0.7.2-{target}.zip'
            self.assertEqual((backup / name).read_bytes(), ('old-' + target).encode())
            self.assertEqual((self.base / name).read_bytes(), ('new-' + target).encode())
        self.assertEqual((backup / 'release-0.7.2.json').read_bytes(), self.before[str(self.manifest)])
        for name, live in self.live.items():
            self.assertEqual(live.read_bytes(), (self.stage / name).read_bytes())

    def test_failure_restores_matching_archive_and_catalog_bytes(self):
        result = self.deploy('0.7.2', fail=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('catalogs restored', result.stderr)
        self.assertEqual((self.root / 'docker-trace').read_text().splitlines().count('restart'), 2)
        self.assertEqual(self.before, self.snapshot())


if __name__ == '__main__':
    unittest.main()
