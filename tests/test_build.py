"""Check configured builds and independent dictionary installation offline."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def run(command, cwd, **options):
    result = subprocess.run(list(map(str, command)), cwd=cwd, capture_output=True, text=True, timeout=120, **options)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


class BuildTests(unittest.TestCase):
    def test_configured_out_of_tree_install_without_python(self):
        with tempfile.TemporaryDirectory(prefix='scws2-config-test-') as temporary:
            root = Path(temporary); prefix = root / 'installed'
            source = root / 'source'; source.mkdir()
            for name in ('include', 'src', 'tools', 'generated', 'build-aux'):
                shutil.copytree(ROOT / name, source / name, ignore=shutil.ignore_patterns('.deps', '.libs', '*.o', '*.lo', '*.la'))
            for name in ('configure', 'configure.ac', 'Makefile.am', 'Makefile.in', 'aclocal.m4', 'config.h.in', 'VERSION', 'scws2.pc.in'):
                shutil.copy2(ROOT / name, source / name)
            run([source / 'configure', '--prefix=' + str(prefix), '--disable-mmap', '--disable-text-cache'], root)
            run(['make', '-j2', 'PYTHON=false', 'AUTOCONF=false', 'AUTOMAKE=false', 'ACLOCAL=false', 'AUTOHEADER=false'], root)
            run(['make', 'install', 'PYTHON=false', 'AUTOCONF=false', 'AUTOMAKE=false', 'ACLOCAL=false', 'AUTOHEADER=false'], root)
            self.assertTrue((prefix / 'include/scws2.h').is_file())
            self.assertTrue((prefix / 'include/scws2_version.h').is_file())
            self.assertFalse((prefix / 'etc').exists())
            self.assertNotIn('#define SCWS2_HAVE_MMAP 1', (root / 'config.h').read_text())
            self.assertNotIn('#define SCWS2_HAVE_TEXT_CACHE 1', (root / 'config.h').read_text())
            self.assertEqual(run([prefix / 'bin/scws2', '--version'], root).strip(),
                             (ROOT / 'VERSION').read_text().strip())

    def test_independent_dictionary_make_and_install(self):
        with tempfile.TemporaryDirectory(prefix='scws2-dict-test-') as temporary:
            root = Path(temporary); directory = root / 'dict'; directory.mkdir()
            for name in ('Makefile', 'build.py', 'install.sh'):
                shutil.copy2(ROOT / 'dict' / name, directory / name)
            (root / 'tools').mkdir()
            shutil.copy2(ROOT / 'tools/scws2-dict.c', root / 'tools/scws2-dict.c')
            cache = directory / '.cache'; cache.mkdir()
            revision = 'a' * 40; files = []
            for path, name, data in [('jieba/dict.txt', 'jieba-dict.txt', '测试 20 n\n词典 10 n\n'.encode()),
                                     ('LICENSE', 'jieba-LICENSE', b'fixture license\n')]:
                (cache / name).write_bytes(data)
                files.append({'path': path, 'cache_name': name, 'bytes': len(data),
                              'sha256': hashlib.sha256(data).hexdigest(),
                              'url': f'https://raw.githubusercontent.com/fxsjy/jieba/{revision}/{path}'})
            lock = {'schema': 1, 'source': {'id': 'jieba', 'repository': 'https://github.com/fxsjy/jieba',
                    'revision': revision, 'license': 'MIT', 'parser': 'jieba-v1', 'files': files}}
            (directory / 'sources.lock.json').write_text(json.dumps(lock))
            (directory / 'overrides.tsv').write_text('term\taction\tfrequency\tpos\n')
            # Deny networking in the subprocess, including on regressions.
            guard = root / 'guard'; guard.mkdir()
            (guard / 'sitecustomize.py').write_text('import urllib.request\ndef deny(*a, **k):\n raise RuntimeError("network forbidden")\nurllib.request.urlopen=deny\n')
            environment = dict(os.environ, PYTHONPATH=str(guard))
            run(['make'], directory, env=environment)
            prefix = root / 'installed'; stage = root / 'package'
            run(['make', 'install', 'PREFIX=' + str(prefix), 'DESTDIR=' + str(stage)], directory)
            destination = Path(str(stage) + str(prefix)) / 'etc/dict.xdb'
            self.assertEqual(destination.read_bytes(), (directory / 'build/dict.xdb').read_bytes())
            old = destination.stat()
            run(['make', 'install', 'PREFIX=' + str(prefix), 'DESTDIR=' + str(stage)], directory)
            self.assertEqual(destination.stat().st_ino, old.st_ino)
            # Auto-detection reads configured parent prefix, not a guessed Makefile assignment.
            (prefix / 'include').mkdir(parents=True)
            (prefix / 'include/scws2.h').write_text('fixture installed header')
            (root / 'config.status').touch()
            (root / 'Makefile').write_text(f'print-prefix:\n\t@echo "{prefix}"\n')
            run(['make', 'install', 'DESTDIR=' + str(stage)], directory,
                env={k: v for k, v in os.environ.items() if k != 'PREFIX'})
            self.assertFalse(list(destination.parent.glob('.scws2-dict.*')))
