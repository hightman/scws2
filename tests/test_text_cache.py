"""Persistent text cache reuse, invalidation, locking and storage selection."""
from concurrent.futures import ThreadPoolExecutor
import fcntl
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TextCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compilation = tempfile.TemporaryDirectory()
        cls.bin = Path(cls.compilation.name)
        subprocess.run([os.sys.executable, '-B', str(ROOT / 'tools/gen_rules.py'),
                        '--output', str(cls.bin / 'rules_data.h')], check=True)
        (cls.bin / 'scws2_version.h').write_text('#define SCWS2_VERSION "' + (ROOT / 'VERSION').read_text().strip() + '"\n')
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-DSCWS2_HAVE_MMAP=1', '-DSCWS2_HAVE_TEXT_CACHE=1', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror'] + shlex.split(os.environ.get('CORE_TEST_CFLAGS', '-O2'))
        includes = ['-I' + str(ROOT / p) for p in ('include', 'src')] + ['-I' + str(cls.bin)]
        sources = [str(ROOT / p) for p in ('src/text.c', 'src/text_cache.c', 'src/utf8.c', 'src/rules.c', 'tests/cache_api.c')]
        for name, definitions in [('normal', []), ('no-mmap', ['-DSCWS2_NO_MMAP']),
                                  ('mmap-failure', ['-DSCWS2_TEST_MMAP_FAILURE']),
                                  ('no-cache', ['-DSCWS2_NO_TEXT_CACHE'])]:
            subprocess.run(cc + flags + definitions + includes + sources + ['-o', str(cls.bin / name)], check=True)
        cls.large = cls.bin / 'large.txt'
        cls.large.write_text(''.join(f'条目{i:05d}词 n\n' for i in range(20000)), encoding='utf-8')

    @classmethod
    def tearDownClass(cls):
        cls.compilation.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.environment = dict(os.environ, TMPDIR=str(self.root))
        self.source = self.root / 'user.txt'
        self.source.write_text('自定义词 100 n\n', encoding='utf-8')

    def command(self, source=None, flags=2, base_flags=0, base='-', binary='normal', extra=()):
        return [str(self.bin / binary), str(base_flags), str(base), str(flags), str(source or self.source), *map(str, extra)]

    def load(self, **options):
        result = subprocess.run(self.command(**options), env=self.environment, capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        return [int(line) for line in result.stdout.splitlines()]

    def cached(self):
        return list(self.root.glob('scws2-text-*/*.xdb'))

    def test_reuses_cache_across_processes_and_normalizes_path(self):
        self.assertEqual(self.load(), [1])
        cache, = self.cached()
        previous = cache.stat()
        alias = self.root / 'alias.txt'
        alias.symlink_to(self.source)
        self.load(source=alias)
        self.assertEqual(self.cached(), [cache])
        self.assertEqual((cache.stat().st_ino, cache.stat().st_mtime_ns),
                         (previous.st_ino, previous.st_mtime_ns))
        self.assertEqual(cache.stat().st_mode & 0o777, 0o600)
        self.assertEqual(cache.parent.stat().st_mode & 0o777, 0o700)

    def test_source_update_and_deletion_invalidate_cache(self):
        self.load()
        old, = self.cached()
        previous = self.source.stat()
        self.source.write_text('自定义词 100 v\n', encoding='utf-8')
        os.utime(self.source, ns=(previous.st_atime_ns, previous.st_mtime_ns))
        self.load()
        new, = set(self.cached()) - {old}
        self.assertIn(b'v\0', new.read_bytes())
        self.source.unlink()
        result = subprocess.run(self.command(), env=self.environment, capture_output=True)
        self.assertEqual(result.returncode, 1)

    def test_damaged_cache_is_rebuilt(self):
        self.load()
        cache, = self.cached()
        original = cache.read_bytes()
        cache.write_bytes(b'broken')
        self.load()
        self.assertEqual(cache.read_bytes(), original)

    def test_writers_lock_and_publish_complete_files(self):
        self.load()
        cache, = self.cached()
        cache.unlink()
        lock = cache.with_name(cache.name + '.lock')
        with lock.open('rb') as locked:
            fcntl.flock(locked, fcntl.LOCK_EX)
            with subprocess.Popen(self.command(), env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
                try:
                    with self.assertRaises(subprocess.TimeoutExpired):
                        process.communicate(timeout=0.15)
                    self.assertFalse(cache.exists())
                finally:
                    fcntl.flock(locked, fcntl.LOCK_UN)
                    stdout, stderr = process.communicate(timeout=15)
                self.assertEqual(process.returncode, 0, stderr.decode())
                self.assertEqual(stdout, b'1\n')
        cache.unlink()
        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(lambda _: self.load(), range(8)))
        self.assertEqual(results, [[1]] * 8)
        self.assertEqual(len(self.cached()), 1)
        self.assertEqual(list(cache.parent.glob('*.tmp-*')), [])
        self.assertTrue(cache.read_bytes().endswith(b'XDB2END\n'))

    def test_explicit_flags_ignore_size_and_previous_calls(self):
        self.assertEqual(self.load(source=self.large, extra=(self.source,)), [1, 1])
        self.assertEqual(self.load(source=self.large, flags=3, extra=(self.source,)), [0, 0])
        large_cache, = [p for p in self.cached() if p.stat().st_size >= 1024 * 1024]
        self.assertEqual(self.load(source=self.large, base=large_cache, base_flags=1), [1])
        self.assertEqual(self.load(base=large_cache, flags=3), [0])
        self.assertEqual(self.load(source=self.large, flags=3, binary='no-mmap'), [0])
        self.assertEqual(self.load(source=large_cache, flags=1, binary='no-mmap'), [0])

    def test_unavailable_or_disabled_cache_falls_back_to_memory(self):
        self.environment['TMPDIR'] = str(self.root / 'missing' / 'directory')
        self.assertEqual(self.load(), [0])
        self.assertEqual(self.cached(), [])
        self.environment['TMPDIR'] = str(self.root)
        self.assertEqual(self.load(binary='no-cache'), [0])
        self.assertEqual(self.cached(), [])

    def test_default_mmap_and_explicit_memory_for_both_formats(self):
        self.assertEqual(self.load(flags=2), [1])
        cache, = self.cached()
        previous = cache.stat()
        self.assertEqual(self.load(flags=3), [0])
        self.assertEqual((cache.stat().st_ino, cache.stat().st_mtime_ns),
                         (previous.st_ino, previous.st_mtime_ns))
        self.assertEqual(self.load(source=cache, flags=0), [1])
        self.assertEqual(self.load(source=cache, flags=1), [0])
        for binary in ('no-mmap', 'mmap-failure'):
            for source, flags in ((cache, 0), (cache, 1), (self.source, 2), (self.source, 3)):
                with self.subTest(binary=binary, flags=flags):
                    self.assertEqual(self.load(source=source, flags=flags, binary=binary), [0])

    def test_mmap_request_uses_memory_when_cache_is_unavailable(self):
        self.load()
        base, = self.cached()
        self.environment['TMPDIR'] = str(self.root / 'unavailable' / 'directory')
        for binary in ('normal', 'no-cache', 'no-mmap'):
            self.assertEqual(self.load(base=base, base_flags=1, flags=2, binary=binary), [0])

    def test_library_format_flags_ignore_extensions(self):
        self.load()
        cache, = self.cached()
        misleading = self.root / 'binary.txt'
        misleading.write_bytes(cache.read_bytes())
        self.assertEqual(self.load(source=misleading, flags=0), [1])
        text = self.root / 'words.data'
        text.write_bytes(self.source.read_bytes())
        self.assertEqual(self.load(source=text, flags=2), [1])
        for source, flags in ((text, 0), (misleading, 2)):
            result = subprocess.run(self.command(source=source, flags=flags), env=self.environment,
                                    capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 1)
