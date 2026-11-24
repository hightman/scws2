import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('gen_rules', ROOT / 'tools/gen_rules.py')
rules = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rules)


class RuleDataTests(unittest.TestCase):
    def test_configuration_contains_only_supported_data(self):
        for name in ('rules.ini', 'rules_cht.ini'):
            text = (ROOT / 'etc' / name).read_text(encoding='utf-8')
            self.assertEqual(set(rules.parse(text)), set(rules.SECTIONS))
            self.assertNotIn(':tf', text)
            self.assertNotIn(':idf', text)

    def test_unsupported_or_ambiguous_configuration_fails(self):
        text = (ROOT / 'etc/rules.ini').read_text(encoding='utf-8')
        cases = [text + '\n[attrs]\nn + n = 100\n', text + '\n[symbol]\n!\n', text.replace('[symbol]', '[symbol]\nA'),
                 text.replace('[chnum1]', '[chnum1]\n甲'), text.replace('[chnum5]', '[chnum5]\n:tf=5'),
                 text + '\n' + '个' * 17 + '\n']
        for data in cases:
            with self.subTest(data=data[-40:]), self.assertRaises(ValueError):
                rules.parse(data)

    def test_committed_tables_match_the_two_rule_sources(self):
        self.assertEqual(rules.generate(), (ROOT / 'src/rules_data.h').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
