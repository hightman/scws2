#!/usr/bin/env python3
"""Display configurable segmentation cases and compare optional expectations."""
import argparse
import difflib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def load_cases(path):
    document = json.loads(Path(path).read_text(encoding='utf-8'))
    if not isinstance(document, dict) or not isinstance(document.get('cases'), list) or not document['cases']:
        raise ValueError('配置须包含非空 cases 数组')
    seen = set()
    for case in document['cases']:
        if not isinstance(case, dict) or not isinstance(case.get('id'), str) or not case['id']:
            raise ValueError('每个用例须有非空字符串 id')
        name = case['id']
        if name in seen:
            raise ValueError(f'重复用例 id: {name}')
        seen.add(name)
        if not isinstance(case.get('text'), str):
            raise ValueError(f'{name}: text 须为字符串')
        if case.get('mode', 'standard') not in ('standard', 'search'):
            raise ValueError(f'{name}: mode 须为 standard 或 search')
        for flag in ('symbols', 'bigrams'):
            if flag in case and not isinstance(case[flag], bool):
                raise ValueError(f'{name}: {flag} 须为布尔值')
        if case.get('bigrams') and case.get('mode', 'standard') != 'search':
            raise ValueError(f'{name}: bigrams 只能用于 search 模式')
        for field in ('expected', 'expected_pos', 'expected_primary'):
            if field in case and (not isinstance(case[field], list)
                                  or any(not isinstance(value, str) for value in case[field])):
                raise ValueError(f'{name}: {field} 须为字符串数组')
        if 'expected_pos' in case and ('expected' not in case or len(case['expected_pos']) != len(case['expected'])):
            raise ValueError(f'{name}: expected_pos 须与 expected 长度一致')
    return document


def segment(executable, dictionaries, case):
    command = [str(executable)]
    for path in dictionaries:
        command.extend(['-d', str(path)])
    for enabled, option in ((case.get('mode') == 'search', '--search'),
                            (case.get('symbols', False), '--symbols'),
                            (case.get('bigrams', False), '--bigrams')):
        if enabled:
            command.append(option)
    result = subprocess.run(command, input=case['text'].encode('utf-8'), capture_output=True, timeout=30)
    if result.returncode:
        raise ValueError(result.stderr.decode('utf-8', errors='replace').strip() or f'CLI 退出码 {result.returncode}')
    tokens = []
    for line in result.stdout.decode('utf-8').splitlines():
        fields = line.split('\t', 7)
        if len(fields) != 8:
            raise ValueError('CLI 输出不是预期的八列 TSV')
        offset, length, char_offset, char_length, pos, kind, origin, word = fields
        tokens.append({'word': word, 'pos': pos, 'offset': int(offset), 'length': int(length),
                       'char_offset': int(char_offset), 'char_length': int(char_length),
                       'kind': kind, 'origin': origin})
    return tokens


def words(values):
    return ' | '.join(values) if values else '(空)'


def compare_words(expected, actual, tokens, label='expected'):
    differences = []
    matcher = difflib.SequenceMatcher(a=expected, b=actual, autojunk=False)
    for operation, a, b, c, d in matcher.get_opcodes():
        if operation == 'equal':
            continue
        detail = (f'{operation}: {label}[{a}:{b}] {words(expected[a:b])}'
                  f' → actual[{c}:{d}] {words(actual[c:d])}')
        if c < d:
            detail += '；原文字符范围 ' + ', '.join(
                f"[{token['char_offset']}:{token['char_offset'] + token['char_length']}]"
                for token in tokens[c:d])
        differences.append(detail)
    return differences


def compare(case, tokens):
    if 'expected' not in case and 'expected_primary' not in case:
        return 'REVIEW', []
    differences = []
    if 'expected' in case:
        actual = [token['word'] for token in tokens]
        expected = case['expected']
        differences.extend(compare_words(expected, actual, tokens))
        if 'expected_pos' in case:
            matcher = difflib.SequenceMatcher(a=expected, b=actual, autojunk=False)
            for operation, a, b, c, d in matcher.get_opcodes():
                if operation != 'equal':
                    continue
                for left, right in zip(range(a, b), range(c, d)):
                    wanted, found = case['expected_pos'][left], tokens[right]['pos']
                    if wanted != found:
                        differences.append(f'词性 actual[{right + 1}] {actual[right]!r}: '
                                           f'预期 {wanted!r}，实际 {found!r}')
    if 'expected_primary' in case:
        primary = [token for token in tokens if token['kind'] == 'primary']
        differences.extend(compare_words(case['expected_primary'], [token['word'] for token in primary],
                                         primary, 'expected_primary'))
    return ('DIFF' if differences else 'PASS'), differences


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cases', type=Path, default=ROOT / 'tests/cases/segmentation.json', help='UTF-8 JSON 用例文件')
    parser.add_argument('--scws', type=Path, default=ROOT / 'build/scws', help='SCWS 2 CLI 路径')
    parser.add_argument('-d', '--dict', dest='dictionaries', type=Path, action='append', help='词典，可重复指定叠加词典')
    parser.add_argument('--case', action='append', dest='ids', help='只执行指定 id，可重复')
    parser.add_argument('--observe', action='store_true', help='仅观察，分词差异不影响退出码；执行错误仍失败')
    args = parser.parse_args(argv)
    try:
        document = load_cases(args.cases)
        cases = document['cases']
        if args.ids:
            unknown = set(args.ids) - {case['id'] for case in cases}
            if unknown:
                raise ValueError('未知用例 id: ' + ', '.join(sorted(unknown)))
            cases = [case for case in cases if case['id'] in args.ids]
    except (OSError, ValueError) as error:
        print(f'配置错误: {error}', file=sys.stderr)
        return 2
    dictionaries = args.dictionaries or [ROOT / 'build/dict/dictionary.xdb']
    print(f'用例: {args.cases}\nCLI: {args.scws}')
    print('词典: ' + ', '.join(map(str, dictionaries)))
    if document.get('description'):
        print(document['description'])
    totals = dict.fromkeys(('PASS', 'DIFF', 'REVIEW', 'ERROR'), 0)
    for number, case in enumerate(cases, 1):
        print(f"\n[{number}/{len(cases)}] {case['id']}  mode={case.get('mode', 'standard')} "
              f"symbols={case.get('symbols', False)} bigrams={case.get('bigrams', False)}")
        print('输入: ' + json.dumps(case['text'], ensure_ascii=False))
        if case.get('note'):
            print('备注: ' + case['note'])
        if 'expected' in case:
            print('预期: ' + words(case['expected']))
        if 'expected_primary' in case:
            print('预期主词: ' + words(case['expected_primary']))
        try:
            tokens = segment(args.scws.resolve(), dictionaries, case)
            status, differences = compare(case, tokens)
            print('实际: ' + words([token['word'] for token in tokens]))
            print('词性: ' + words([token['word'] + '/' + (token['pos'] or '?') for token in tokens]))
            if case.get('mode') == 'search':
                print('主词: ' + words([token['word'] for token in tokens if token['kind'] == 'primary']))
                print('补词: ' + words([token['word'] for token in tokens if token['kind'] != 'primary']))
            print('结果: ' + status)
            for difference in differences:
                print('  差异: ' + difference)
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            status = 'ERROR'
            print(f'结果: ERROR — {error}')
        totals[status] += 1
    print(f"\n汇总: {len(cases)} 句；符合预期 {totals['PASS']}；有差异 {totals['DIFF']}；"
          f"仅展示 {totals['REVIEW']}；执行错误 {totals['ERROR']}")
    return 2 if totals['ERROR'] else 1 if totals['DIFF'] and not args.observe else 0


if __name__ == '__main__':
    sys.exit(main())
