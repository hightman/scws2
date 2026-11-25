"""Behavioral tests against the real C library, CLI and independent XDB builder."""
import os
import json
import math
import importlib.util
from pathlib import Path
import random
import shlex
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.environment = patch.dict(os.environ, {'TMPDIR': str(cls.root)})
        cls.environment.start()
        cls.addClassCleanup(cls.environment.stop)
        cls.cli = cls.root / 'scws2'
        cls.compiler = cls.root / 'scws2-dict'
        cls.api = cls.root / 'api-test'
        cls.no_mmap_api = cls.root / 'api-no-mmap-test'
        cls.text_compiler = cls.root / 'text-compiler-test'
        subprocess.run([os.sys.executable, '-B', str(ROOT / 'tools/gen_rules.py'),
                        '--output', str(cls.root / 'rules_data.h')], check=True)
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror'] + shlex.split(os.environ.get('CORE_TEST_CFLAGS', '-O2'))
        source = [str(ROOT / name) for name in ('src/scws2.c', 'src/utf8.c', 'src/xdb.c', 'src/text.c', 'src/text_cache.c', 'src/rules.c')]
        includes = ['-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'src'), '-I' + str(cls.root)]
        for main, output, extra in [('cli/scws2.c', cls.cli, []), ('tests/core_api.c', cls.api, ['-pthread']), ('tests/text_dict.c', cls.text_compiler, [])]:
            subprocess.run(cc + flags + includes + source + [str(ROOT / main), '-o', str(output)] + extra, check=True)
        subprocess.run(cc + flags + includes + ['-DSCWS2_NO_MMAP'] + source
                       + [str(ROOT / 'tests/core_api.c'), '-pthread', '-o', str(cls.no_mmap_api)], check=True)
        subprocess.run(cc + flags + [str(ROOT / 'cli/scws2-dict.c'), '-o', str(cls.compiler)], check=True)
        cls.dictionary = cls.build('base', [
            ('研究', 100, 3, 'v'), ('研究生', 50, 3, 'n'), ('生命', 100, 3, 'n'),
            ('命', 500, 3, 'n'), ('起源', 100, 3, 'n'), ('南京', 150, 3, 'ns'),
            ('南京市', 100, 3, 'ns'), ('市长', 80, 3, 'n'), ('长江', 100, 3, 'ns'),
            ('长江大桥', 90, 3, 'ns'), ('大桥', 70, 3, 'n'), ('江大', 0, 2, 'nz'),
            ('人工智能', 80, 3, 'n'), ('人工', 100, 3, 'n'), ('智能', 100, 3, 'n'),
            ('大模型', 0, 2, ''), ('中', 100, 3, 'n'), ('中国', 100, 3, 'ns'),
            ('中文', 100, 3, 'n'), ('测试', 100, 3, 'vn'), ('𠀀词', 100, 3, 'nz'),
            ('三点', 10000, 3, 'm'), ('五公斤', 10000, 3, 'mq'), ('阿·明', 100, 3, 'nr'),
            ('𠀀' * 60, 10, 3, 'nz')])
        cls.overlay = cls.build('overlay', [('研究', 5000, 3, 'n'), ('人工智能', 0, 2, 'nz')])

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def build(cls, name, rows):
        source, target = cls.root / (name + '.tsv'), cls.root / (name + '.xdb')
        source.write_text(''.join('\t'.join(map(str, row)) + '\n' for row in rows), encoding='utf-8')
        subprocess.run([str(cls.compiler), str(source), str(target)], check=True, capture_output=True)
        return target

    def invoke(self, text, *options, dictionary=None, success=True):
        command = [str(self.cli), '-d', str(dictionary or self.dictionary), *options]
        result = subprocess.run(command, input=text.encode() if isinstance(text, str) else text,
                                capture_output=True, timeout=15)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            tokens = []
            for line in result.stdout.decode().splitlines():
                offset, length, char_offset, char_length, pos, kind, origin, word = line.split('\t', 7)
                tokens.append({'offset': int(offset), 'length': int(length), 'char_offset': int(char_offset),
                               'char_length': int(char_length), 'pos': pos, 'kind': kind, 'origin': origin, 'word': word})
            return tokens
        self.assertEqual(result.returncode, 1, result.stderr.decode(errors='replace'))
        self.assertEqual(result.stdout, b'')
        return result

    def test_global_path_beats_greedy_longest_match(self):
        tokens = self.invoke('研究生命起源')
        self.assertEqual([t['word'] for t in tokens], ['研究', '生命', '起源'])
        self.assertEqual([t['pos'] for t in tokens], ['v', 'n', 'n'])

    def test_search_recovers_overlaps_across_primary_boundaries(self):
        standard = self.invoke('南京市长江大桥')
        self.assertEqual([t['word'] for t in standard], ['南京市', '长江大桥'])
        tokens = self.invoke('南京市长江大桥', '--search')
        by_word = {t['word']: t for t in tokens}
        self.assertEqual(by_word['市长']['kind'], 'secondary')
        self.assertEqual(by_word['江大']['origin'], 'dictionary')
        self.assertEqual(by_word['南京市']['kind'], 'primary')
        self.assertEqual(len(tokens), len({(t['offset'], t['length']) for t in tokens}))
        self.assertEqual([t['offset'] for t in tokens], sorted(t['offset'] for t in tokens))

    def test_search_only_words_do_not_change_primary_path(self):
        standard = self.invoke('人工智能大模型')
        self.assertEqual([t['word'] for t in standard], ['人工智能', '大', '模', '型'])
        tokens = self.invoke('人工智能大模型', '--search')
        found = next(t for t in tokens if t['word'] == '大模型')
        self.assertEqual((found['kind'], found['pos']), ('secondary', ''))
        self.assertEqual([t['word'] for t in tokens if t['kind'] == 'primary'], [t['word'] for t in standard])

    def test_unknown_bigrams_are_opt_in_and_do_not_inherit_pos(self):
        plain = self.invoke('龘靐齉', '--search')
        self.assertFalse(any(t['kind'] == 'fallback' for t in plain))
        tokens = self.invoke('龘靐齉', '--search', '--bigrams')
        bigrams = [t for t in tokens if t['kind'] == 'fallback']
        self.assertEqual([t['word'] for t in bigrams], ['龘靐', '靐齉'])
        self.assertTrue(all(t['pos'] == '' and t['origin'] == 'unknown' for t in bigrams))
        self.assertFalse(any(t['kind'] == 'fallback' for t in self.invoke('中龘', '--search', '--bigrams')))

    def test_overlay_priority_and_search_only_override(self):
        options = ('-d', str(self.overlay))
        self.assertEqual([t['word'] for t in self.invoke('研究生命起源', *options)], ['研究生', '命', '起源'])
        self.assertEqual([t['word'] for t in self.invoke('人工智能', *options)], ['人工', '智能'])

    def test_ascii_products_numbers_and_original_spans(self):
        text = 'GPT-4o C++ C# R&B P&G J++ J# UTF-8 PS/2 Wi-Fi iPhone16Pro 3.14 2024-11-24'
        tokens = self.invoke(text)
        self.assertEqual([t['word'] for t in tokens], text.split())
        self.assertEqual(tokens[-2]['pos'], 'm')
        search = self.invoke(text, '--search')
        self.assertEqual([t['word'] for t in search if t['kind'] == 'primary'], text.split())
        self.assertTrue({'GPT', '4', 'iPhone', '16', 'Pro', 'Wi', 'Fi'} <= {t['word'] for t in search})
        raw = text.encode()
        for token in search:
            self.assertEqual(raw[token['offset']:token['offset'] + token['length']].decode(), token['word'])

    def test_four_byte_unicode_offsets_and_width_classification(self):
        tokens = self.invoke('😀𠀀词 ＡＢＣ１２ 中国')
        self.assertEqual([t['word'] for t in tokens], ['𠀀词', 'ＡＢＣ１２', '中国'])
        self.assertEqual((tokens[0]['offset'], tokens[0]['length'], tokens[0]['char_offset'], tokens[0]['char_length']), (4, 7, 1, 2))
        self.assertEqual(tokens[1]['pos'], 'en')
        self.assertEqual(self.invoke('𠀀' * 60)[0]['length'], 240)

    def test_symbols_spaces_and_nul(self):
        self.assertEqual([t['word'] for t in self.invoke('中文，测试\n中国\t')], ['中文', '测试', '中国'])
        self.assertEqual([t['word'] for t in self.invoke('中文，😀', '--symbols')], ['中文', '，', '😀'])
        self.assertEqual([t['word'] for t in self.invoke(b'\0' + '中文'.encode() + b'\0')], ['中文'])
        self.assertEqual(self.invoke(' \n\t\u3000'), [])
        self.assertEqual(self.invoke(b''), [])

    def test_rejects_malformed_and_legacy_encodings(self):
        invalid = [b'\xff', b'\xc0\xaf', b'\xed\xa0\x80', b'\xf4\x90\x80\x80',
                   b'\xf8\x80\x80\x80\x80', b'\xe4\xb8', '中文'.encode('gbk')]
        for raw in invalid:
            with self.subTest(raw=raw):
                result = self.invoke(b'prefix ' + raw, success=False)
                self.assertIn(b'invalid UTF-8', result.stderr)

    def test_no_old_sentence_or_token_length_limit(self):
        tokens = self.invoke('中文' * 300)
        self.assertEqual(len(tokens), 300)
        self.assertEqual(tokens[-1]['offset'], 299 * 6)
        token = self.invoke('a' * 100000)[0]
        self.assertEqual((token['length'], token['char_length']), (100000, 100000))

    def text_dictionary(self, content, name='user.txt'):
        path = self.root / name
        path.write_bytes(content if isinstance(content, bytes) else content.encode())
        return path

    def test_text_dictionary_field_defaults_orders_and_prefixes(self):
        path = self.text_dictionary('\ufeff# comment\r\n; comment\r\n\r\n素词\r\n低频 0\n'
                                    '兼容词 100 nz\n倒序词\tnr\t100\n标注词 vn\n'
                                    '长标签 2 nrt\n另一标签 nrfg 3\n𠀀词 2 n\n'
                                    '重复词 5 n\n重复词 0 nz\n前缀 0 n\n前缀长词 2 n')
        expected = {'素词': 'un', '低频': 'un', '兼容词': 'nz', '倒序词': 'nr', '标注词': 'vn',
                    '长标签': 'nr', '另一标签': 'nr', '𠀀词': 'n', '重复词': 'nz', '前缀长词': 'n'}
        for word, pos in expected.items():
            with self.subTest(word=word):
                tokens = self.invoke(word, dictionary=path)
                self.assertEqual([(t['word'], t['pos']) for t in tokens], [(word, pos)])
        raw = self.root / 'raw-blobs.xdb'
        result = subprocess.run([str(self.text_compiler), str(path), str(raw)], capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        spec = importlib.util.spec_from_file_location('text_dictionary_check', ROOT / 'dict/build.py')
        validator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(validator)
        validator.check_xdb(raw)
        self.assertEqual(self.invoke('前缀长词', dictionary=raw), self.invoke('前缀长词', dictionary=path))
        explicit = self.text_dictionary('特殊词', 'custom.TXT')
        result = self.invoke('特殊词', dictionary=explicit)
        self.assertEqual([(t['word'], t['pos']) for t in result], [('特殊词', 'un')])
        arbitrary = self.text_dictionary('特殊词', 'custom.data')
        self.invoke('特殊词', dictionary=arbitrary, success=False)
        result = self.invoke('特殊词', '--mmap', '-d', str(explicit))
        self.assertEqual([(t['word'], t['pos']) for t in result], [('特殊词', 'un')])

    def test_text_frequency_calibration_is_independent_of_file_size(self):
        rng = random.Random(4549)
        frequencies = [0, 1, 2, 3, 65536, 0xffffffff] + [rng.randrange(1, 70000000) for _ in range(100)]
        path = self.text_dictionary(''.join(f'词{i} {freq} n\n' for i, freq in enumerate(frequencies)))
        raw = self.root / 'costs.xdb'
        subprocess.run([str(self.text_compiler), str(path), str(raw)], check=True, capture_output=True)
        data = raw.read_bytes()
        offset = 64 + struct.unpack_from('<I', data, 20)[0] * 8
        found = {}
        for _ in range(struct.unpack_from('<I', data, 24)[0]):
            size = data[offset + 16]
            key = data[offset + 17:offset + 17 + size].decode()
            cost, flags = struct.unpack_from('<II', data, offset + 17 + size)
            if flags & 2:
                found[key] = cost
            offset += 27 + size
        for i, frequency in enumerate(frequencies):
            expected = max(0, math.floor(18432 - math.log(max(frequency, 1)) * 1024 + 0.5))
            self.assertEqual(found[f'词{i}'], expected)
        single = self.text_dictionary('兼容词 100 nz', 'single.txt')
        multiple = self.text_dictionary('兼容词 100 nz\n其它词 1 n', 'multiple.txt')
        self.assertEqual(self.invoke('兼容词', dictionary=single), self.invoke('兼容词', dictionary=multiple))

    def test_malformed_text_dictionary_is_rejected(self):
        invalid = [b'', b'# only comment\n', b'word 1 n extra\n', b'word -1 n\n',
                   b'word 4294967296 n\n', b'word 1.2 n\n', b'word 1 unsupported\n',
                   b'word 1 9\n', b'\xff\n', b'word\0junk\n', b'x' * 241 + b'\n',
                   b'x' * 4096 + b'\n', '词\u3000词\n'.encode()]
        for number, content in enumerate(invalid):
            with self.subTest(number=number):
                path = self.text_dictionary(content)
                self.invoke('中文', dictionary=path, success=False)
        path = self.text_dictionary('素词\n中国 0 nz\n新词甲 100000000 n')
        malformed = self.text_dictionary('word 2 n\nbad -1 n', 'malformed.txt')
        for api in (self.api, self.no_mmap_api):
            result = subprocess.run([str(api), str(self.dictionary), 'text-result', str(path), str(malformed)],
                                    capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_native_api_lifecycle_errors_and_owned_dictionaries(self):
        result = subprocess.run([str(self.api), str(self.dictionary), str(self.overlay)], capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_cli_without_dictionary_uses_rules_and_unknown_characters(self):
        result = subprocess.run([str(self.cli), '-i', '中文 C++ 2026年'], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([line.split('\t')[-1] for line in result.stdout.splitlines()], ['中', '文', 'C++', '2026年'])

    def test_memory_only_build_reports_mmap_unsupported(self):
        result = subprocess.run([str(self.no_mmap_api), str(self.dictionary), str(self.overlay)],
                                capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_mapped_dictionary_survives_atomic_file_replacement(self):
        path, replacement = self.root / 'live.xdb', self.root / 'replacement.xdb'
        path.write_bytes(self.dictionary.read_bytes())
        replacement.write_bytes(self.overlay.read_bytes())
        result = subprocess.run([str(self.api), str(path), 'replace', str(replacement)],
                                capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_loading_modes_report_io_errors(self):
        for mode in ('memory', 'mmap'):
            result = subprocess.run([str(self.api), str(self.root / 'missing.xdb'), mode, 'io'],
                                    capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr.decode())

    def run_case_tool(self, cases, *options):
        path = self.root / 'cases.json'
        path.write_text(json.dumps({'cases': cases}, ensure_ascii=False), encoding='utf-8')
        return subprocess.run([os.sys.executable, '-B', str(ROOT / 'tools/check_segments.py'),
                               '--scws2', str(self.cli), '-d', str(self.dictionary),
                               '--cases', str(path), *options], capture_output=True, text=True, timeout=15)

    def test_case_tool_displays_compares_and_filters(self):
        cases = [
            {'id': 'words', 'text': '中文 测试', 'expected': ['中文', '测试'], 'expected_pos': ['n', 'vn']},
            {'id': 'search', 'text': '中文！', 'mode': 'search', 'symbols': True,
             'expected': ['中文', '！'], 'expected_pos': ['n', 'un']},
            {'id': 'empty', 'text': '', 'expected': []},
            {'id': 'observe', 'text': '研究生命起源'}]
        result = self.run_case_tool(cases)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('中文/n', result.stdout)
        self.assertIn('主词:', result.stdout)
        self.assertIn('符合预期 3；有差异 0；仅展示 1；执行错误 0', result.stdout)
        cases += [
            {'id': 'word-diff', 'text': '中文测试', 'expected': ['中文测试']},
            {'id': 'pos-diff', 'text': '中文', 'expected': ['中文'], 'expected_pos': ['v']}]
        result = self.run_case_tool(cases, '--case', 'word-diff', '--case', 'pos-diff')
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('expected[0:1] 中文测试', result.stdout)
        self.assertIn('原文字符范围 [0:2], [2:4]', result.stdout)
        self.assertIn('词性 actual[1]', result.stdout)
        self.assertIn('汇总: 2 句', result.stdout)
        observed = self.run_case_tool(cases, '--case', 'word-diff', '--observe')
        self.assertEqual(observed.returncode, 0, observed.stdout + observed.stderr)
        self.assertIn('有差异 1', observed.stdout)

    def test_case_tool_compares_primary_path_independently(self):
        # Search can contain the requested word as a supplement while its
        # primary path still differs. Do not let a full-output match hide it.
        cases = [{'id': 'primary', 'text': '人工智能', 'mode': 'search',
                  'expected': ['人工智能', '人工', '智能'],
                  'expected_primary': ['人工', '智能']}]
        result = self.run_case_tool(cases)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('expected_primary[0:2] 人工 | 智能', result.stdout)
        cases[0]['expected_primary'] = ['人工智能']
        result = self.run_case_tool(cases)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('符合预期 1', result.stdout)
        cases[0]['expected'] = ['wrong']
        result = self.run_case_tool(cases)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_case_tool_reports_config_and_execution_errors(self):
        for cases, options in [([{'id': 'bad', 'text': '中文', 'mode': 'gbk'}], []),
                               ([{'id': 'bad', 'text': '中文', 'expected_pos': ['n']}], []),
                               ([{'id': 'ok', 'text': '中文'}], ['--case', 'missing'])]:
            result = self.run_case_tool(cases, *options)
            self.assertEqual(result.returncode, 2)
            self.assertIn('配置错误', result.stderr)
        result = self.run_case_tool([{'id': 'one', 'text': '中文'}, {'id': 'two', 'text': '测试'}],
                                    '-d', str(self.root / 'missing.xdb'))
        self.assertEqual(result.returncode, 2)
        self.assertIn('汇总: 2 句', result.stdout)
        self.assertIn('执行错误 2', result.stdout)

    def test_invalid_dictionary_framing_is_rejected(self):
        original = self.dictionary.read_bytes()
        mutations = [b'', original[:63], original[:-1], original + b'junk']
        for offset, value in [(4, 2), (8, 32), (12, 1), (16, 0), (20, 0), (20, 0xffffffff),
                              (24, 0xffffffff), (28, 0), (32, 8), (36, 1)]:
            bad = bytearray(original)
            struct.pack_into('<I', bad, offset, value)
            mutations.append(bytes(bad))
        bad = bytearray(original)
        bad[-1] ^= 1
        mutations.append(bytes(bad))
        for i, raw in enumerate(mutations):
            with self.subTest(i=i):
                path = self.root / 'invalid.xdb'
                path.write_bytes(raw)
                self.invoke('中文', dictionary=path, success=False)
                for mode in ('memory', 'mmap'):
                    result = subprocess.run([str(self.api), str(path), mode, 'format'],
                                            capture_output=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_open_does_not_scan_dictionary_records(self):
        original = bytearray(self.dictionary.read_bytes())
        first = 64 + 16381 * 8
        # A record metadata error is for the offline checker, not open().
        original[first + 25 + original[first + 16]] = 255
        path = self.root / 'unchecked-record.xdb'
        path.write_bytes(original)
        self.assertEqual(self.invoke('abc', dictionary=path)[0]['word'], 'abc')

    def test_damaged_lookup_links_are_bounded(self):
        path = self.build('damaged-links', [('中文', 100, 3, 'n')])
        original = path.read_bytes()
        # Both 中 and 中文 hash to different buckets; damage the root for 中.
        h = 0xf422f
        for byte in reversed('中'.encode()):
            h = ((h * 33) ^ byte) & 0x7fffffff
        pointer = 64 + (h % 16381) * 8
        root = struct.unpack_from('<I', original, pointer)[0]
        for offset, value in ((pointer, len(original)), (root, root),
                              (root + 8, root), (root + 16, 255)):
            with self.subTest(offset=offset):
                bad = bytearray(original)
                if offset == root + 16:
                    bad[offset] = value
                else:
                    struct.pack_into('<I', bad, offset, value)
                if offset in (root, root + 8):
                    bad[root + 17:root + 20] = ('串' if offset == root else '一').encode()
                path.write_bytes(bad)
                self.invoke('中国', dictionary=path)

    def test_seeded_binary_mutations_never_crash_or_hang(self):
        rng = random.Random(230024)
        original = self.dictionary.read_bytes()
        path = self.root / 'mutated.xdb'
        for _ in range(40):
            bad = bytearray(original)
            for _ in range(rng.randint(1, 4)):
                bad[rng.randrange(len(bad))] ^= rng.randrange(1, 256)
            path.write_bytes(bad)
            result = subprocess.run([str(self.cli), '-d', str(path), '-i', '中文'], capture_output=True, timeout=5)
            self.assertIn(result.returncode, (0, 1), result.stderr.decode(errors='replace'))

    def test_removed_charset_option_is_rejected(self):
        self.invoke('中文', '-c', 'gbk', success=False)

    def test_numeric_rules_without_numeric_dictionary_entries(self):
        cases = {'一九四九年': 'mq', '二〇二六年': 'mq', '壹佰贰拾元': 'mq', '壹佰貳拾圓': 'mq',
                 '第十五章': 'mq', '每三个月': 'mq', '每三個月': 'mq', '三点五公斤': 'mq',
                 '三點五公斤': 'mq', '３．５％': 'mq', '三千万元': 'mq', '3.5万': 'm',
                 '3.5万元': 'mq', '五千克': 'mq', '三千米': 'mq', '一亿零三万': 'm',
                 '十二万三千四百五十六': 'm'}
        for text, pos in cases.items():
            with self.subTest(text=text):
                tokens = self.invoke(text)
                self.assertEqual([(t['word'], t['pos'], t['origin']) for t in tokens], [(text, pos, 'builtin')])

    def test_numeric_search_preserves_number_and_unit_boundaries(self):
        tokens = self.invoke('三点五公斤', '--search')
        self.assertEqual({t['word'] for t in tokens}, {'三点五公斤', '三点五', '公斤'})
        self.assertEqual([(t['word'], t['pos']) for t in tokens if t['kind'] == 'primary'], [('三点五公斤', 'mq')])
        tokens = self.invoke('第十五章', '--search')
        self.assertEqual({t['word'] for t in tokens}, {'第十五章', '十五', '章'})
        self.assertEqual([t['word'] for t in self.invoke('２０２６年１１月')], ['２０２６年', '１１月'])

    def test_ambiguous_idioms_and_malformed_numbers_are_not_forced(self):
        for text in ('一心一意', '三心二意', '一般情况', '每个人', '千万不要', '两三年', '一两年',
                     '一百百', '十百', '三点五点六', '一万亿'):
            with self.subTest(text=text):
                self.assertFalse(any(t['origin'] == 'builtin' and t['pos'] in ('m', 'mq') for t in self.invoke(text)))

    def test_numeric_spans_keep_original_offsets_and_scale_linearly(self):
        text = '😀第十五章，３．５％'
        raw = text.encode()
        for token in self.invoke(text, '--search', '--symbols'):
            self.assertEqual(raw[token['offset']:token['offset'] + token['length']].decode(), token['word'])
            self.assertEqual(text[token['char_offset']:token['char_offset'] + token['char_length']], token['word'])
        large = '一' * 5000 + '年'
        tokens = self.invoke(large)
        self.assertEqual(len(tokens), 1)
        self.assertEqual((tokens[0]['length'], tokens[0]['char_length']), (len(large.encode()), len(large)))
        self.assertFalse(any(t['origin'] == 'builtin' for t in self.invoke('一百百' * 3000)))

    def test_currency_and_middle_dot_symbol_policy(self):
        self.assertEqual(self.invoke('￥ ¥ € ·'), [])
        self.assertEqual([t['word'] for t in self.invoke('￥ ¥ € ·', '--symbols')], ['￥', '¥', '€', '·'])
        # Dictionary-backed names remain usable; no surname guessing is added.
        self.assertEqual([t['word'] for t in self.invoke('阿·明')], ['阿·明'])


if __name__ == '__main__':
    unittest.main()
