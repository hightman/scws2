"""Exercise real Make dependencies in an isolated, source-only checkout."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildTests(unittest.TestCase):
    def test_rule_generation_is_automatic_and_incremental(self):
        files = ['Makefile', 'include/scws2.h', 'cli/scws2.c', 'cli/scws2-dict.c', 'tools/gen_rules.py',
                 'src/scws2.c', 'src/utf8.c', 'src/utf8.h', 'src/xdb.c', 'src/xdb.h', 'src/text.c', 'src/text_cache.c',
                 'src/rules.c', 'src/rules.h', 'src/rules.ini']
        with tempfile.TemporaryDirectory(prefix='scws2-build-test-') as temporary:
            root = Path(temporary)
            for name in files:
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


if __name__ == '__main__':
    unittest.main()
