"""Release version selection is separate from network publication."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release_version', ROOT / 'tools/version.py')
version = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(version)


class ReleaseTests(unittest.TestCase):
    def test_odd_development_to_even_release(self):
        self.assertEqual(version.select('0.1.1'), '0.1.2')
        self.assertEqual(version.select('0.1.1', '0.2.0'), '0.2.0')
        self.assertEqual(version.development('0.1.2'), '0.1.3')

    def test_invalid_odd_duplicate_or_older_releases_are_rejected(self):
        for requested in ('0.1.3', '0.1.0', '0.1.1', '01.2.0', 'v0.2.0', '0.2'):
            with self.subTest(requested=requested), self.assertRaises(ValueError):
                version.select('0.1.1', requested)
        with self.assertRaises(ValueError):
            version.select('0.1.2')


class PublishTests(unittest.TestCase):
    def test_annotated_tag_points_to_even_version_and_main_advances(self):
        import os
        import subprocess
        import sys
        import tempfile
        from unittest import mock
        with mock.patch.object(sys, 'path', [str(ROOT / 'tools'), *sys.path]):
            import release
        with tempfile.TemporaryDirectory(prefix='scws2-publish-test-') as temporary:
            root = Path(temporary); repository = root / 'repo'; repository.mkdir()
            remote = root / 'remote.git'

            def git(*args, cwd=repository):
                return subprocess.check_output(['git', *map(str, args)], cwd=cwd, stderr=subprocess.STDOUT, text=True).strip()

            git('init', '-q', '-b', 'main')
            git('config', 'user.name', 'Release test'); git('config', 'user.email', 'release@example.invalid')
            git('config', 'commit.gpgsign', 'false'); git('config', 'tag.gpgSign', 'false')
            (repository / 'VERSION').write_text('0.1.1\n')
            git('add', 'VERSION'); git('commit', '-qm', 'Development')
            git('init', '--bare', '-q', remote)
            git('remote', 'add', 'origin', remote); git('push', '-qu', 'origin', 'main')
            # Replace only the GitHub transport; commits, annotated tag and pushes are real local Git operations.
            binary = root / 'bin'; binary.mkdir(); log = root / 'github-arguments'
            gh = binary / 'gh'; gh.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$SCWS2_TEST_GH_LOG"\n')
            gh.chmod(0o755)
            assets = root / 'assets'; assets.mkdir()
            core = assets / 'scws2-0.1.2.tar.gz'; core.write_bytes(b'checked fixture')
            checksum = assets / 'SHA256SUMS'; checksum.write_text('fixture')
            notes = assets / 'RELEASE.md'; notes.write_text('fixture')
            with mock.patch.object(release, 'ROOT', repository), mock.patch.dict(os.environ,
                    {'PATH': str(binary) + os.pathsep + os.environ['PATH'], 'SCWS2_TEST_GH_LOG': str(log)}):
                release.publish('0.1.2', '0.1.3', 'v0.1.2', [core], checksum, notes, {'identity': 'fixture'}, assets)
            self.assertEqual(git('show', 'v0.1.2:VERSION'), '0.1.2')
            self.assertEqual(git('cat-file', '-t', 'v0.1.2'), 'tag')
            self.assertEqual((repository / 'VERSION').read_text(), '0.1.3\n')
            self.assertEqual(git('tag'), 'v0.1.2')
            self.assertEqual(git('rev-parse', 'HEAD'), git('rev-parse', 'refs/heads/main', cwd=remote))
            self.assertEqual(git('status', '--porcelain'), '')
            self.assertIn('v0.1.2', log.read_text())
            self.assertIn('--verify-tag', log.read_text())

    def test_dictionary_identity_ignores_compiler_noise_but_tracks_content(self):
        import json
        import sys
        import tempfile
        from unittest import mock
        with mock.patch.object(sys, 'path', [str(ROOT / 'tools'), *sys.path]):
            import release
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = {key: {} for key in ('source', 'pos', 'score')}
            manifest.update(pipeline_version=3, format='xdb2-v4', overrides_sha256='fixture', compiler_sha256='old')
            (root / 'dict.xdb').write_bytes(b'fixture dictionary')
            (root / 'LICENSE.jieba').write_bytes(b'fixture license')
            (root / 'manifest.json').write_text(json.dumps(manifest))
            first = release.dict_identity(root)
            manifest['compiler_sha256'] = 'new build'
            (root / 'manifest.json').write_text(json.dumps(manifest))
            self.assertEqual(release.dict_identity(root), first)
            (root / 'dict.xdb').write_bytes(b'changed dictionary')
            self.assertNotEqual(release.dict_identity(root), first)
