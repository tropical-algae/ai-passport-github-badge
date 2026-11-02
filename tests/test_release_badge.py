#!/usr/bin/env python3
"""Release planning with real temporary Git histories, plus verified packaging."""
from __future__ import annotations
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import release_badge as RELEASE
from test_archive_firmware import FirmwareArchiveTest


class ReleasePlanningTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='badge-release-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.git('init', '-q', '-b', 'main')
        self.git('config', 'user.name', 'Release Test')
        self.git('config', 'user.email', 'release@example.invalid')
        (self.root / 'VERSION').write_text('0.2.5\n')
        self.git('add', 'VERSION')
        self.commit('chore: initialize badge')

    def git(self, *args):
        return RELEASE.git(self.root, *args)

    def commit(self, message):
        self.git('commit', '-q', '--allow-empty', '-m', message)

    def tag(self, tag='v0.2.5'):
        self.git('tag', tag)

    def test_first_version_and_unchanged_docs_skip(self):
        result = RELEASE.plan(self.root)
        self.assertEqual(result['tag'], 'v0.2.5')
        self.tag()
        self.commit('docs: explain setup')
        self.assertFalse(RELEASE.plan(self.root)['release'])

    def test_patch_minor_and_breaking_precedence(self):
        self.tag()
        self.commit('fix(cache): preserve previous record')
        self.assertEqual(RELEASE.plan(self.root)['version'], '0.2.6')
        self.commit('perf: reduce avatar memory')
        self.assertEqual(RELEASE.plan(self.root)['version'], '0.2.6')
        self.commit('feat: add activity history')
        self.assertEqual(RELEASE.plan(self.root)['version'], '0.3.0')
        self.commit('refactor(api)!: change config')
        self.assertEqual(RELEASE.plan(self.root)['version'], '1.0.0')

    def test_breaking_footer_and_multiline_messages(self):
        self.tag('v1.2.3')
        self.commit('refactor: reorganize\n\nMultiple body lines.\n\nBREAKING CHANGE: config is incompatible')
        result = RELEASE.plan(self.root)
        self.assertEqual(result['tag'], 'v2.0.0')
        self.assertEqual(len(result['commits']), 1)
        self.assertRegex(result['commits'][0]['sha'], r'^[0-9a-f]{40}$')

    def test_releases_are_numeric_not_lexicographic(self):
        self.tag('v1.9.9')
        self.commit('feat: later release')
        self.tag('v1.10.0')
        self.commit('fix: after release')
        self.assertEqual(RELEASE.plan(self.root)['tag'], 'v1.10.1')

    def test_explicit_version_validation_duplicates_and_injection(self):
        self.tag()
        self.assertEqual(RELEASE.plan(self.root, 'v0.2.7')['version'], '0.2.7')
        for value in ('0.2.5', '0.1.0', '01.2.3', '1.2', '1.2.3-beta', '1.2.3\nx=1', '$(id)', ' 1.2.3'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                RELEASE.plan(self.root, value)

    def test_unreachable_higher_release_fails(self):
        self.tag()
        self.git('checkout', '-q', '-b', 'other')
        self.commit('feat: other branch')
        self.tag('v9.0.0')
        self.git('checkout', '-q', 'main')
        with self.assertRaisesRegex(ValueError, 'not reachable'):
            RELEASE.plan(self.root, '10.0.0')

    def test_duplicate_tag_aliases_fail(self):
        self.tag()
        self.tag('0.2.5')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            RELEASE.plan(self.root)

    def test_wrong_target_fails_and_annotated_tags_work(self):
        previous = self.git('rev-parse', 'HEAD')
        self.git('tag', '-a', 'v0.2.5', '-m', 'release')
        self.commit('fix: correct sync')
        self.assertEqual(RELEASE.plan(self.root)['tag'], 'v0.2.6')
        with self.assertRaisesRegex(ValueError, 'checked-out'):
            RELEASE.plan(self.root, target=previous)

    def test_shallow_history_fails(self):
        self.commit('fix: next commit')
        clone = self.root / 'shallow'
        subprocess.run(['git', 'clone', '-q', '--depth=1', self.root.as_uri(), str(clone)], check=True)
        with self.assertRaisesRegex(ValueError, 'full history'):
            RELEASE.plan(clone)


class ReleasePackageTest(unittest.TestCase):
    def setUp(self):
        self.fixture = FirmwareArchiveTest()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root = self.fixture.root
        RELEASE.git(self.root, 'init', '-q', '-b', 'main')
        RELEASE.git(self.root, 'config', 'user.name', 'Release Test')
        RELEASE.git(self.root, 'config', 'user.email', 'release@example.invalid')
        (self.root / 'VERSION').write_text('0.2.5\n')
        (self.root / '.gitignore').write_text('build/\nbuild-input/\nrelease/\n')
        (self.root / 'LICENSE').write_text('GPL-3.0 fixture\n')
        (self.root / 'LICENSE-FoloToy').write_text('MIT upstream fixture\n')
        (self.root / 'THIRD-PARTY-LICENSES.txt').write_text('Original dependency license fixture\n')
        RELEASE.git(self.root, 'add', 'VERSION', '.gitignore', 'LICENSE', 'LICENSE-FoloToy', 'THIRD-PARTY-LICENSES.txt')
        RELEASE.git(self.root, 'commit', '-q', '-m', 'feat: publish badge')
        self.selected = RELEASE.plan(self.root)
        app = bytearray((self.fixture.build / self.fixture.app_name).read_bytes())
        app[48:80] = b'github-badge-0.2.5'.ljust(32, b'\0')
        self.fixture.write(self.fixture.app_name, app)
        self.fixture.merge()
        self.fixture.output = self.root / 'build' / 'firmware'
        self.fixture.create()
        (self.root / 'build' / self.fixture.full_name).write_bytes((self.fixture.build / self.fixture.full_name).read_bytes())
        self.provenance = self.root / 'build' / 'firmware-provenance.json'
        self.provenance.write_text(json.dumps({'source_commit': self.selected['sha'], 'source_clean': True,
            'full_bin_sha256': hashlib.sha256((self.root / 'build' / self.fixture.full_name).read_bytes()).hexdigest()}))

    def test_public_allowlist_matches_version_hashes_and_offsets(self):
        destination = self.root / 'release'
        RELEASE.package(self.root, self.selected, destination)
        names = {p.name for p in destination.iterdir()}
        self.assertEqual(names, {'github-pixel-passport-v0.2.5-app.bin', 'github-pixel-passport-v0.2.5-full.bin',
                               'github-pixel-passport-v0.2.5-manifest.json', 'FLASHING.txt', 'release-notes.md', 'SHA256SUMS.txt', 'LICENSE', 'LICENSE-FoloToy', 'THIRD-PARTY-LICENSES.txt'})
        for name in ('LICENSE', 'LICENSE-FoloToy', 'THIRD-PARTY-LICENSES.txt'):
            self.assertEqual((destination / name).read_bytes(), (self.root / name).read_bytes())
        manifest = json.loads((destination / 'github-pixel-passport-v0.2.5-manifest.json').read_text())
        self.assertEqual(manifest['source_commit'], self.selected['sha'])
        for name, info in manifest['files'].items():
            self.assertEqual(hashlib.sha256((destination / name).read_bytes()).hexdigest(), info['sha256'])
        for line in (destination / 'SHA256SUMS.txt').read_text().splitlines():
            digest, name = line.split('  ')
            self.assertEqual(hashlib.sha256((destination / name).read_bytes()).hexdigest(), digest)
        self.assertEqual(manifest['files']['github-pixel-passport-v0.2.5-app.bin']['flash_offset'], '0x10000')
        with self.assertRaises(FileExistsError):
            RELEASE.package(self.root, self.selected, destination)

    def test_stale_or_dirty_build_provenance_is_rejected(self):
        original = json.loads(self.provenance.read_text())
        for field, value in (("source_commit", "0" * 40), ("source_clean", False), ("full_bin_sha256", "f" * 64)):
            self.provenance.write_text(json.dumps(dict(original, **{field: value})))
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "provenance"):
                RELEASE.package(self.root, self.selected, self.root / 'release')
        self.provenance.write_text(json.dumps(original))
        (self.root / 'VERSION').write_text('0.2.6\n')
        with self.assertRaisesRegex(ValueError, "clean source"):
            RELEASE.package(self.root, self.selected, self.root / 'release')

    def test_mismatched_version_or_corrupt_archive_refuses_package(self):
        wrong = dict(self.selected, version='0.2.6', tag='v0.2.6')
        with self.assertRaisesRegex(ValueError, 'version'):
            RELEASE.package(self.root, wrong, self.root / 'wrong')
        archive_path = self.fixture.destination()
        with (archive_path / self.fixture.app_name).open('ab') as file:
            file.write(b'corruption')
        with self.assertRaises(ValueError):
            RELEASE.package(self.root, self.selected, self.root / 'corrupt')


class PublishWorkflowTest(unittest.TestCase):
    """Execute the actual YAML shell block with a local gh simulator, no service writes."""
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='badge-publish-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        (self.root / 'release').mkdir()
        (self.root / 'bin').mkdir()
        self.state = self.root / 'state.json'
        self.sha = 'a' * 40
        self.state.write_text(json.dumps({'tag': None, 'draft': None, 'uploads': 0}))
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        self.script = '\n'.join(line[10:] for line in workflow.rsplit('        run: |\n', 1)[1].splitlines())
        simulator = '''#!/usr/bin/env python3
import json, os, pathlib, sys
path = pathlib.Path(os.environ['SIM_STATE'])
state = json.loads(path.read_text())
a = sys.argv[1:]
if a[0] == 'api':
    if '--method' in a:
        assert state['tag'] is None
        state['tag'] = {'type': 'commit', 'sha': os.environ['RELEASE_SHA']}
    else:
        if state['tag'] is None: sys.exit(1)
        print(json.dumps({'object': state['tag']}))
elif a[:2] == ['release', 'view']:
    if state['draft'] is None: sys.exit(1)
    print(json.dumps({'isDraft': state['draft']}))
elif a[:2] == ['release', 'create']:
    assert state['draft'] is None and '--draft' in a and '--verify-tag' in a
    assert a[a.index('--target') + 1] == os.environ['RELEASE_SHA']
    state['draft'] = True
elif a[:2] == ['release', 'upload']:
    assert state['draft'] is True and '--clobber' in a
    assert 'THIRD-PARTY-LICENSES.txt' in a
    state['uploads'] += 1
elif a[:2] == ['release', 'edit']:
    assert state['draft'] is True and '--draft=false' in a
    state['draft'] = False
else: raise AssertionError(a)
path.write_text(json.dumps(state))
'''
        fake = self.root / 'bin' / 'gh'; fake.write_text(simulator); fake.chmod(0o755)
        checksum = self.root / 'bin' / 'sha256sum'
        checksum.write_text('#!/bin/sh\nexit 0\n'); checksum.chmod(0o755)

    def run_publish(self, state):
        import os
        self.state.write_text(json.dumps(state))
        env = dict(os.environ, PATH=str(self.root / 'bin') + os.pathsep + os.environ['PATH'],
                   SIM_STATE=str(self.state), GH_TOKEN='local-test-only', GITHUB_REPOSITORY='example/badge',
                   RELEASE_SHA=self.sha, GITHUB_SHA=self.sha, RELEASE_TAG='v0.2.5', RELEASE_VERSION='0.2.5')
        result = subprocess.run(['bash', '-euo', 'pipefail', '-c', self.script], cwd=self.root,
                                env=env, capture_output=True, text=True)
        return result, json.loads(self.state.read_text())

    def test_new_release_and_failed_upload_draft_retry(self):
        for tag, draft in ((None, None), ({'type': 'commit', 'sha': self.sha}, True)):
            with self.subTest(draft=draft):
                result, state = self.run_publish({'tag': tag, 'draft': draft, 'uploads': 0})
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(state['tag']['sha'], self.sha)
                self.assertFalse(state['draft'])
                self.assertEqual(state['uploads'], 1)

    def test_published_release_and_wrong_target_are_immutable(self):
        cases = (({'type': 'commit', 'sha': self.sha}, False),
                 ({'type': 'commit', 'sha': 'b' * 40}, True),
                 ({'type': 'tag', 'sha': self.sha}, True))
        for tag, draft in cases:
            with self.subTest(tag=tag, draft=draft):
                result, state = self.run_publish({'tag': tag, 'draft': draft, 'uploads': 0})
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(state, {'tag': tag, 'draft': draft, 'uploads': 0})


if __name__ == '__main__':
    # Imported fixture class is a helper, not a second run of archive tests.
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(cls)
                               for cls in (ReleasePlanningTest, ReleasePackageTest, PublishWorkflowTest))
    unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() or sys.exit(1)
