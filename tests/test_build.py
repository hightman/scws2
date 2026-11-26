"""Exercise real Make dependencies in an isolated, source-only checkout."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


BUILD_FILES = ['Makefile', 'include/scws2.h', 'tools/scws2.c', 'tools/scws2-dict.c', 'tools/gen_rules.py',
               'src/scws2.c', 'src/utf8.c', 'src/utf8.h', 'src/xdb.c', 'src/xdb.h', 'src/text.c', 'src/text_cache.c',
               'src/rules.c', 'src/rules.h', 'src/rules.ini']


class BuildTests(unittest.TestCase):
    def test_rule_generation_is_automatic_and_incremental(self):
        with tempfile.TemporaryDirectory(prefix='scws2-build-test-') as temporary:
            root = Path(temporary)
            for name in BUILD_FILES:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, target)

            def make(*args):
                result = subprocess.run(['make', '-j2', 'PYTHON=' + sys.executable, *args], cwd=root,
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result.stdout

            header, obj = root / 'build/rules_data.h', root / 'build/rules.o'
            self.assertFalse(header.exists())
            self.assertIn('gen_rules.py --output', make())
            data = header.read_bytes()
            previous = (header.stat().st_mtime_ns, obj.stat().st_mtime_ns)
            self.assertNotIn('gen_rules.py --output', make())
            self.assertEqual(previous, (header.stat().st_mtime_ns, obj.stat().st_mtime_ns))
            for name in ('src/rules.ini', 'tools/gen_rules.py'):
                # Accommodate Make/filesystem combinations with second resolution.
                time.sleep(1.1)
                (root / name).touch()
                with self.subTest(changed=name):
                    self.assertIn('gen_rules.py --output', make())
                    current = (header.stat().st_mtime_ns, obj.stat().st_mtime_ns)
                    self.assertGreater(current[0], previous[0])
                    self.assertGreater(current[1], previous[1])
                    self.assertEqual(header.read_bytes(), data)
                    previous = current
            prefix = root / 'installed'
            make('install', 'PREFIX=' + str(prefix))
            self.assertTrue((prefix / 'include/scws2.h').exists())
            self.assertFalse(list(prefix.rglob('*.ini')))

    def test_dictionary_build_and_conditional_install_are_ordered(self):
        with tempfile.TemporaryDirectory(prefix='scws2-dict-install-test-') as temporary:
            root = Path(temporary)
            for name in BUILD_FILES + ['dict/build.py']:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, target)
            # Fail immediately if any tested make target tries to use the network.
            guard = root / 'no-network'
            guard.mkdir()
            (guard / 'sitecustomize.py').write_text(
                'import urllib.request\ndef blocked(*a, **kw):\n    raise RuntimeError("unexpected network")\n'
                'urllib.request.urlopen = blocked\n')
            environment = dict(os.environ, PYTHONPATH=str(guard))

            def make(*goals, success=True):
                result = subprocess.run(['make', '-j4', 'PYTHON=' + sys.executable, *goals],
                                        cwd=root, env=environment, capture_output=True, text=True, timeout=30)
                if success:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                else:
                    self.assertNotEqual(result.returncode, 0)
                return result.stdout

            prefix = root / 'installed'
            make()
            make('install', 'PREFIX=' + str(prefix))
            self.assertFalse((root / 'build/dict').exists())
            self.assertFalse((prefix / 'etc').exists())
            revision = 'a' * 40
            entries = []
            cache = root / '.cache/dict'
            cache.mkdir(parents=True)
            for path, name, data in [('jieba/dict.txt', 'jieba-dict.txt', '测试 20 n\n词典 10 n\n'.encode()),
                                     ('LICENSE', 'jieba-LICENSE', b'fixture license\n')]:
                (cache / name).write_bytes(data)
                entries.append({'path': path, 'cache_name': name, 'bytes': len(data),
                                'sha256': hashlib.sha256(data).hexdigest(),
                                'url': f'https://raw.githubusercontent.com/fxsjy/jieba/{revision}/{path}'})
            lock = {'schema': 1, 'source': {'id': 'jieba', 'repository': 'https://github.com/fxsjy/jieba',
                    'revision': revision, 'license': 'MIT', 'parser': 'jieba-v1', 'files': entries}}
            (root / 'dict/sources.lock.json').write_text(json.dumps(lock))
            (root / 'dict/overrides.tsv').write_text('term\taction\tfrequency\tpos\n')
            stage = root / 'package'
            arguments = ['PREFIX=/usr/local', 'DESTDIR=' + str(stage)]
            output = make('dict', 'install', *arguments)
            self.assertIn('dict/build.py fetch', output)
            destination = stage / 'usr/local/etc'
            for name in ('dictionary.xdb', 'LICENSE.jieba', 'manifest.json'):
                self.assertEqual((destination / name).read_bytes(), (root / 'build/dict' / name).read_bytes())
            source, installed = root / 'build/dict/dictionary.xdb', destination / 'dictionary.xdb'
            previous = installed.stat()
            make('install', *arguments)
            self.assertEqual((installed.stat().st_ino, installed.stat().st_mtime_ns),
                             (previous.st_ino, previous.st_mtime_ns))
            installed.write_bytes(b'newer installed dictionary')
            newer = source.stat().st_mtime_ns + 10_000_000_000
            os.utime(installed, ns=(newer, newer))
            make('install', *arguments)
            self.assertEqual(installed.read_bytes(), b'newer installed dictionary')
            newer += 10_000_000_000
            os.utime(source, ns=(newer, newer))
            with installed.open('rb') as old_reader:
                make('install', *arguments)
                self.assertEqual(old_reader.read(), b'newer installed dictionary')
            self.assertEqual(installed.read_bytes(), source.read_bytes())
            self.assertEqual(list(destination.glob('.scws2-dict.*')), [])
            # An unsuccessful dict goal must prevent install, even with -j4.
            lock['source']['files'][0]['url'] = 'invalid-unpinned-url'
            (root / 'dict/sources.lock.json').write_text(json.dumps(lock))
            previous = installed.stat()
            make('dict', 'install', *arguments, success=False)
            self.assertEqual(installed.stat().st_ino, previous.st_ino)


if __name__ == '__main__':
    unittest.main()
