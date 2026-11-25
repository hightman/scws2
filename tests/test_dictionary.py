"""Offline dictionary integration tests. Compile the actual dependency-free C tool."""
import importlib.util
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('dictionary', ROOT / 'dict/build.py')
dictionary = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(dictionary)


class DictionaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory()
        cls.compiler = Path(cls.workspace.name) / 'scws2-dict'
        command = shlex.split(os.environ.get('CC', 'cc'))
        flags = shlex.split(os.environ.get('DICT_TEST_CFLAGS', '-O2'))
        subprocess.run(command + ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror'] + flags
                       + [str(ROOT / 'cli/scws2-dict.c'), '-o', str(cls.compiler)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def compile(self, content, success=True):
        source, output = self.root / 'input.tsv', self.root / 'output.xdb'
        source.write_bytes(content if isinstance(content, bytes) else content.encode())
        result = subprocess.run([str(self.compiler), str(source), str(output)], capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            return output
        self.assertNotEqual(result.returncode, 0)
        return result

    def fixture(self):
        revision = 'a' * 40
        files = []
        for path, name, content in [('jieba/dict.txt', 'jieba-dict.txt', '人工 20 n\n人工智能 10 n\n智能 8 n\n𠀀 1 n\n'.encode()),
                                    ('LICENSE', 'jieba-LICENSE', b'fixture license\n')]:
            (self.root / name).write_bytes(content)
            files.append({'path': path, 'cache_name': name, 'bytes': len(content),
                          'sha256': dictionary.digest(content),
                          'url': f'https://raw.githubusercontent.com/fxsjy/jieba/{revision}/{path}'})
        lock = {'schema': 1, 'source': {'id': 'jieba', 'repository': 'https://github.com/fxsjy/jieba',
                'revision': revision, 'license': 'MIT', 'parser': 'jieba-v1', 'files': files}}
        (self.root / 'lock.json').write_text(json.dumps(lock))
        (self.root / 'overrides.tsv').write_text('term\taction\tfrequency\tpos\n', encoding='utf-8')
        return lock

    def test_build_preserves_terms_costs_and_all_prefixes(self):
        path = self.compile('人工\t700\t3\tn\n人工智能\t1500\t3\tnr\n智能\t1100\t3\t\n𠀀词\t0\t2\tnz\nC++\t500\t3\ten\n')
        stats = dictionary.check_xdb(path)
        self.assertEqual(stats['main_entries'], 4)
        self.assertEqual(stats['search_entries'], 5)
        self.assertEqual(stats['tagged_entries'], 4)
        # Independently look up records through hash and BST pointers.
        data = path.read_bytes()
        def lookup(term):
            key = term.encode()
            offset, length = struct.unpack_from('<II', data, 64 + dictionary.hash_key(key, 0xf422f, 16381) * 8)
            while length:
                n = data[offset + 16]
                found = data[offset + 17:offset + 17 + n]
                if key == found:
                    cost, flags = struct.unpack_from('<II', data, offset + 17 + n)
                    pos = data[offset + 25 + n:offset + 27 + n].rstrip(b'\0').decode('ascii')
                    return cost, flags, pos
                offset, length = struct.unpack_from('<II', data, offset + (8 if key > found else 0))
            return None
        self.assertEqual(lookup('人工'), (700, 7, 'n'))
        self.assertEqual(lookup('人工智能'), (1500, 3, 'nr'))
        self.assertEqual(lookup('智能'), (1100, 3, ''))
        self.assertEqual(lookup('人工智'), (0, 4, ''))
        self.assertEqual(lookup('𠀀'), (0, 4, ''))
        self.assertEqual(lookup('𠀀词'), (0, 2, 'nz'))
        self.assertIsNone(lookup('不存在'))

    def test_output_is_independent_of_input_order(self):
        rows = ['人工\t200\t3\tn\n', '人工智能\t600\t3\tnr\n', '智能\t800\t3\t\n']
        first = self.compile(''.join(rows)).read_bytes()
        second = self.compile(''.join(reversed(rows))).read_bytes()
        self.assertEqual(first, second)

    def test_malformed_compiler_inputs(self):
        cases = [b'', b'a\t1\t3\tn', b'a\t1\t3\tn\na\t2\t3\tv\n', b'a\t-1\t3\tn\n',
                 b'a\t4294967296\t3\tn\n', b'a\t1\t2\tn\n', b'a\t1\t1\tn\n',
                 b'a\t1\t7\tn\n', b'\xff\t1\t3\tn\n', b'\xed\xa0\x80\t1\t3\tn\n',
                 b'\xf4\x90\x80\x80\t1\t3\tn\n', b'\xc0\xaf\t1\t3\tn\n',
                 b'a\x00b\t1\t3\tn\n', b'a b\t1\t3\tn\n', b'x' * 241 + b'\t1\t3\tn\n',
                 b'a\t1\t3\n', b'a\t1\t3\tn\textra\n', b'a\t1\t3\t1n\n',
                 b'a\t1\t3\tn v\n', b'a\t1\t3\t\xff\n', b'a\t1\t3\t' + b'n' * 3 + b'\n']
        for content in cases:
            with self.subTest(content=content[:30]):
                self.compile(content, success=False)

    def test_key_byte_limit_and_four_byte_utf8(self):
        dictionary.check_xdb(self.compile('𠀀' * 60 + '\t1\t3\tn\n'))
        self.compile('𠀀' * 61 + '\t1\t3\tn\n', success=False)
        dictionary.check_xdb(self.compile('词\t1\t3\t' + 'n' * 2 + '\n'))

    def test_corruption_rejected(self):
        valid = self.compile('人工\t200\t3\tn\n人工智能\t600\t3\tnr\n').read_bytes()
        mutations = [valid[:20], valid[:-1], valid + b'junk']
        bad = bytearray(valid)
        bad[-1] ^= 1
        mutations.append(bytes(bad))
        bad = bytearray(valid)
        bad[:4] = b'XDB\x22'
        mutations.append(bytes(bad))
        first_offset = 64 + 16381 * 8
        bad = bytearray(valid)
        struct.pack_into('<II', bad, first_offset, first_offset, 27 + bad[first_offset + 16])
        mutations.append(bytes(bad))
        bad = bytearray(valid)
        struct.pack_into('<I', bad, first_offset + 17 + bad[first_offset + 16] + 4, 128)
        mutations.append(bytes(bad))
        for content in mutations:
            with self.subTest(length=len(content)):
                path = self.root / 'corrupt.xdb'
                path.write_bytes(content)
                with self.assertRaises(ValueError):
                    dictionary.check_xdb(path)

    def test_prepare_overrides_and_repeatability(self):
        lock = self.fixture()
        patch = self.root / 'overrides.tsv'
        patch.write_text('term\taction\tfrequency\tpos\n人工\tdeny\t\t\n智能\tsearch\t\t\n大模型\tupsert\t50\tnz\n', encoding='utf-8')
        out = self.root / 'prepared'
        manifest = dictionary.prepare(lock, self.root, patch, out)
        rows = {line.split('\t')[0]: line.split('\t')[1:] for line in (out / 'lexicon.tsv').read_text().splitlines()}
        self.assertNotIn('人工', rows)
        self.assertEqual(rows['智能'], ['0', '2', 'n'])
        self.assertEqual(rows['大模型'][1], '3')
        self.assertEqual(rows['大模型'][2], 'nz')
        self.assertLess(int(rows['大模型'][0]), int(rows['人工智能'][0]))
        original = (out / 'manifest.json').read_bytes()
        self.assertEqual(dictionary.prepare(lock, self.root, patch, out), manifest)
        self.assertEqual((out / 'manifest.json').read_bytes(), original)

    def test_duplicate_source_uses_max_not_sum(self):
        words, stats = dictionary.parse_jieba('测试 20 n\n测试 10 v\n无效 0 n\n'.encode())
        self.assertEqual(words['测试'], (20, 3, 'n'))
        self.assertEqual(stats['duplicate_rows'], 1)
        self.assertEqual(stats['zero_frequency_rows'], 1)
        self.assertEqual(stats['pos_conflicts'], 1)

    def test_bad_source_and_patch_fail(self):
        with self.assertRaises(ValueError):
            dictionary.parse_jieba(b'bad row\n')
        for text in ['term\taction\tfrequency\tpos\na\tupsert\t0\tn\n',
                     'term\taction\tfrequency\tpos\na\tdeny\t\t\na\tsearch\t\t\n',
                     'term\taction\tfrequency\tpos\na\tsearch\t5\tn\n',
                     'term\taction\tfrequency\tpos\na\tunknown\t\tn\n',
                     'term\taction\tfrequency\tpos\na\tdeny\t\tn\n',
                     'term\taction\tfrequency\tpos\na\tupsert\t5\t名词\n']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                dictionary.apply_overrides({}, text.encode())

    def test_cache_checksum_failure(self):
        lock = self.fixture()
        (self.root / 'jieba-dict.txt').write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            dictionary.prepare(lock, self.root, self.root / 'overrides.tsv', self.root / 'prepared')

    def test_fetch_reuses_verified_cache_without_network(self):
        lock = self.fixture()
        with mock.patch.object(dictionary.urllib.request, 'urlopen', side_effect=AssertionError('network used')):
            dictionary.fetch(lock, self.root)

    def test_floating_or_wrong_source_url_rejected(self):
        lock = self.fixture()
        lock['source']['files'][0]['url'] = 'https://raw.githubusercontent.com/fxsjy/jieba/master/jieba/dict.txt'
        path = self.root / 'lock.json'
        path.write_text(json.dumps(lock))
        with self.assertRaisesRegex(ValueError, 'pinned'):
            dictionary.load_lock(path)

    def test_cli_build_and_failed_build_preserves_previous_output(self):
        self.fixture()
        out = self.root / 'bundle'
        command = [os.environ.get('PYTHON', os.sys.executable), str(ROOT / 'dict/build.py'), 'build',
                   '--lock', str(self.root / 'lock.json'), '--cache', str(self.root),
                   '--overrides', str(self.root / 'overrides.tsv'), '--output', str(out)]
        result = subprocess.run(command + ['--compiler', str(self.compiler)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        before = {p.name: p.read_bytes() for p in out.iterdir()}
        result = subprocess.run(command + ['--compiler', str(self.root / 'missing-compiler')], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual({p.name: p.read_bytes() for p in out.iterdir()}, before)
        self.assertEqual(json.loads((out / 'manifest.json').read_bytes())['entries'], 4)

    def test_diff_tracks_added_removed_and_changed_entries(self):
        lock = self.fixture()
        patch = self.root / 'overrides.tsv'
        old, new = self.root / 'old', self.root / 'new'
        dictionary.prepare(lock, self.root, patch, old)
        patch.write_text('term\taction\tfrequency\tpos\n人工\tdeny\t\t\n智能\tsearch\t\t\n大模型\tupsert\t50\t\n', encoding='utf-8')
        dictionary.prepare(lock, self.root, patch, new)
        report = dictionary.diff_bundles(old, new, 1)
        self.assertEqual(report['counts']['added'], 1)
        self.assertEqual(report['counts']['removed'], 1)
        self.assertEqual(report['counts']['flags_changed'], 1)
        self.assertLessEqual(len(report['samples']['changed']), 1)
        (new / 'lexicon.tsv').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            dictionary.diff_bundles(old, new, 1)

    def test_source_pos_unknown_and_conflict_resolution(self):
        rows = ['测试 20 v', '测试 20 n', '测试 10 nrfg', '未知 5']
        first, _ = dictionary.parse_jieba(('\n'.join(rows) + '\n').encode())
        second, _ = dictionary.parse_jieba(('\n'.join(reversed(rows)) + '\n').encode())
        self.assertEqual(first, second)
        self.assertEqual(first['测试'], (20, 3, 'n'))
        self.assertEqual(first['未知'], (5, 3, ''))
        for pos in ('1n', 'n' * 3, '名词'):
            with self.subTest(pos=pos), self.assertRaisesRegex(ValueError, 'POS'):
                dictionary.parse_jieba(f'词 5 {pos}\n'.encode())

    def test_source_long_tags_have_explicit_mapping(self):
        words, stats = dictionary.parse_jieba('音译人名 10 nrt\n古人 8 nrfg\n人名 5 nr\n名词 3 n\n未知 2\n'.encode())
        self.assertEqual(words['音译人名'], (10, 3, 'nr'))
        self.assertEqual(words['古人'], (8, 3, 'nr'))
        self.assertEqual(words['人名'], (5, 3, 'nr'))
        self.assertEqual(words['名词'], (3, 3, 'n'))
        self.assertEqual(words['未知'], (2, 3, ''))
        self.assertEqual(stats['pos_mapped_rows'], 2)
        with self.assertRaisesRegex(ValueError, 'unmapped POS'):
            dictionary.parse_jieba('词 5 unknown\n'.encode())
        with self.assertRaises(ValueError):
            dictionary.apply_overrides({}, 'term\taction\tfrequency\tpos\n词\tupsert\t5\tnrfg\n'.encode())

    def test_override_pos_inheritance_update_and_clear(self):
        words = {'人工': (20, 3, 'n'), '智能': (8, 3, 'a'), '人工智能': (10, 3, 'nr')}
        dictionary.apply_overrides(words, ('term\taction\tfrequency\tpos\n'
            '人工\tupsert\t30\t\n智能\tsearch\t\t-\n人工智能\tupsert\t10\tnz\n'
            '新词\tupsert\t5\t\n补词\tsearch\t\tn\n').encode())
        self.assertEqual(words['人工'], (30, 3, 'n'))
        self.assertEqual(words['智能'], (0, 2, ''))
        self.assertEqual(words['人工智能'], (10, 3, 'nz'))
        self.assertEqual(words['新词'], (5, 3, ''))
        self.assertEqual(words['补词'], (0, 2, 'n'))

    def test_pos_only_change_does_not_affect_costs_or_flags(self):
        lock = self.fixture()
        patch = self.root / 'overrides.tsv'
        old, new = self.root / 'old', self.root / 'new'
        dictionary.prepare(lock, self.root, patch, old)
        patch.write_text('term\taction\tfrequency\tpos\n智能\tupsert\t8\ta\n', encoding='utf-8')
        dictionary.prepare(lock, self.root, patch, new)
        report = dictionary.diff_bundles(old, new, 10)
        self.assertEqual(report['counts'], {'added': 0, 'removed': 0, 'changed': 1,
                         'cost_changed': 0, 'flags_changed': 0, 'pos_changed': 1})
        # Older bundles have no POS; diff supports comparison, not binary loading.
        rows = (old / 'lexicon.tsv').read_text().splitlines()
        data = ''.join('\t'.join(row.split('\t')[:3]) + '\n' for row in rows).encode()
        manifest = json.loads((old / 'manifest.json').read_text())
        manifest['format'] = 'xdb2-v1'
        manifest['lexicon_sha256'] = dictionary.digest(data)
        (old / 'lexicon.tsv').write_bytes(data)
        (old / 'manifest.json').write_text(json.dumps(manifest))
        self.assertEqual(dictionary.diff_bundles(old, new, 10)['counts']['pos_changed'], 4)
        # Earlier schema 2 bundles may contain long tags; compare them verbatim.
        old_rows = []
        for row in rows:
            parts = row.split('\t')
            if parts[0] == '人工智能':
                parts[3] = 'nrfg'
            old_rows.append('\t'.join(parts) + '\n')
        data = ''.join(old_rows).encode()
        manifest['format'] = 'xdb2-v2'
        manifest['lexicon_sha256'] = dictionary.digest(data)
        (old / 'lexicon.tsv').write_bytes(data)
        (old / 'manifest.json').write_text(json.dumps(manifest))
        report = dictionary.diff_bundles(old, new, 10)
        self.assertEqual(report['counts']['pos_changed'], 2)
        self.assertEqual(report['counts']['cost_changed'], 0)

    def test_bad_pos_binary_and_old_schema_rejected(self):
        valid = self.compile('词\t100\t3\tnr\n').read_bytes()
        offset = 64 + 16381 * 8
        key_size = valid[offset + 16]
        mutations = []
        for index, value in ((offset + 25 + key_size, ord('1')), (offset + 26 + key_size, 255)):
            bad = bytearray(valid)
            bad[index] = value
            mutations.append(bad)
        for version in (1, 2, 3):
            bad = bytearray(valid)
            struct.pack_into('<I', bad, 4, version)
            mutations.append(bad)
        bad = bytearray(valid)
        bad[offset + 25 + key_size:offset + 27 + key_size] = b'\0n'
        mutations.append(bad)
        bad = bytearray(valid)
        struct.pack_into('<II', bad, offset + 17 + key_size, 0, 4)
        mutations.append(bad)
        for data in mutations:
            with self.subTest(data=data[-10:]):
                path = self.root / 'bad-pos.xdb'
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    dictionary.check_xdb(path)


if __name__ == '__main__':
    unittest.main()
