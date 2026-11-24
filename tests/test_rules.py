import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('gen_rules', ROOT / 'tools/gen_rules.py')
rules = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rules)


class RuleDataTests(unittest.TestCase):
    def test_configuration_contains_only_supported_data(self):
        text = (ROOT / 'src/rules.ini').read_text(encoding='utf-8')
        groups = rules.parse(text)
        self.assertEqual(set(groups), set(rules.SECTIONS))
        self.assertNotIn(':tf', text)
        self.assertNotIn(':idf', text)
        for unit in ('个', '個', '小时', '小時', '吨', '噸'):
            self.assertIn(tuple(map(rules.width, unit)), groups['munit'])

    def test_unsupported_or_ambiguous_configuration_fails(self):
        text = (ROOT / 'src/rules.ini').read_text(encoding='utf-8')
        cases = [text + '\n[attrs]\nn + n = 100\n', text + '\n[symbol]\n!\n', text.replace('[symbol]', '[symbol]\nA'),
                 text.replace('[chnum1]', '[chnum1]\n甲'), text.replace('[chnum5]', '[chnum5]\n:tf=5'),
                 text + '\n' + '个' * 17 + '\n']
        for data in cases:
            with self.subTest(data=data[-40:]), self.assertRaises(ValueError):
                rules.parse(data)

    def test_generated_output_and_stale_table_detection(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'rules_data.h'
            command = [sys.executable, '-B', str(ROOT / 'tools/gen_rules.py'), '--output', str(output)]
            subprocess.run(command, check=True)
            self.assertEqual(rules.generate(), output.read_text(encoding='utf-8'))
            subprocess.run(command + ['--check'], check=True)
            output.write_text('stale', encoding='utf-8')
            result = subprocess.run(command + ['--check'], capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
