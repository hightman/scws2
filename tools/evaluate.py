#!/usr/bin/env python3
"""Measure scws2 word-span quality and warm native throughput."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import random
import shlex
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, capture_output=True, **kwargs)


def git(*args):
    return run(['git', '-C', ROOT, *args]).stdout


def sha(data):
    return hashlib.sha256(data).hexdigest()


def word_spans(words):
    offset, result = 0, set()
    for word in words:
        result.add((offset, offset + len(word)))
        offset += len(word)
    return result


def score_spans(gold_lines, predictions):
    if len(gold_lines) != len(predictions) or not gold_lines:
        raise ValueError('reference/prediction line counts differ or corpus is empty')
    correct = gold_count = predicted_count = exact = overlap_lines = uncovered = 0
    for gold, predicted in zip(gold_lines, predictions):
        reference = word_spans(gold)
        coverage = [0] * len(''.join(gold))
        for start, end in predicted:
            if not 0 <= start < end <= len(coverage):
                raise ValueError('invalid prediction span')
            for index in range(start, end):
                coverage[index] += 1
        overlap_lines += any(value > 1 for value in coverage)
        uncovered += sum(value == 0 for value in coverage)
        correct += len(reference & set(predicted))
        gold_count += len(gold)
        predicted_count += len(predicted)
        exact += sorted(reference) == predicted
    if not gold_count or not predicted_count:
        raise ValueError('cannot score empty token streams')
    return {'correct_words': correct, 'predicted_words': predicted_count, 'gold_words': gold_count,
            'precision': correct / predicted_count, 'recall': correct / gold_count,
            'f1': 2 * correct / (predicted_count + gold_count), 'exact_sentences': exact,
            'overlapping_sentences': overlap_lines, 'uncovered_characters': uncovered}


def score(gold_lines, predicted_lines):
    if len(gold_lines) != len(predicted_lines):
        raise ValueError('reference/prediction line counts differ')
    for number, (gold, predicted) in enumerate(zip(gold_lines, predicted_lines), 1):
        if ''.join(gold) != ''.join(predicted):
            raise ValueError(f'prediction changed or omitted source characters at line {number}')
    return score_spans(gold_lines, [sorted(word_spans(words)) for words in predicted_lines])


def decode_spans(texts, raw):
    lines = raw.decode().splitlines()
    if len(lines) != len(texts):
        raise ValueError('native dump has wrong line count')
    words, spans = [], []
    for text, line in zip(texts, lines):
        byte_to_char, offset = {0: 0}, 0
        for i, char in enumerate(text, 1):
            offset += len(char.encode())
            byte_to_char[offset] = i
        decoded, positioned = [], []
        for pair in line.split():
            start, size = map(int, pair.split(':'))
            if start not in byte_to_char or start + size not in byte_to_char or size <= 0:
                raise ValueError('native dump has invalid UTF-8 byte span')
            a, b = byte_to_char[start], byte_to_char[start + size]
            decoded.append(text[a:b]); positioned.append((a, b))
        words.append(decoded); spans.append(positioned)
    return words, spans


def compile_engine(output, cc):
    run([sys.executable, '-B', ROOT / 'tools/gen_rules.py', '--output', output / 'rules_data.h'])
    (output / 'scws2_version.h').write_text('#define SCWS2_VERSION "' + (ROOT / 'VERSION').read_text().strip() + '"\n')
    command = cc + ['-DSCWS2_HAVE_MMAP=1', '-DSCWS2_HAVE_TEXT_CACHE=1', '-O2', '-std=c11', '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'src'),
                    '-I' + str(output)]
    command += [str(ROOT / 'src' / name) for name in ('scws2.c', 'utf8.c', 'xdb.c', 'text.c', 'text_cache.c', 'rules.c')]
    binary = output / 'bench-scws2'
    command += [str(ROOT / 'tools/bench_scws2.c'), '-o', str(binary)]
    result = subprocess.run(command, capture_output=True)
    (output / 'compile.log').write_bytes((shlex.join(command) + '\n').encode() + result.stdout + result.stderr)
    result.check_returncode()
    return binary


def benchmark_all(commands, counts, samples, seconds):
    results = {}
    for name, command in commands.items():
        trial = json.loads(run(command[:3] + ['1'] + command[3:]).stdout)
        iterations = min(1000000, max(1, math.ceil(seconds / max(trial['seconds'], 1e-6))))
        results[name] = {'iterations_per_sample': iterations, 'samples': samples, 'seconds_per_pass': []}
    rng = random.Random(0)
    for sample in range(samples):
        names = list(commands)
        rng.shuffle(names)
        print(f'Timing interleaved round {sample + 1}/{samples}...', flush=True)
        for name in names:
            command, result = commands[name], results[name]
            iterations = result['iterations_per_sample']
            measurement = json.loads(run(command[:3] + [str(iterations)] + command[3:],
                                         timeout=max(60, seconds * 10)).stdout)
            if measurement['tokens'] != iterations * counts[name]:
                raise ValueError('token count changed between quality and throughput runs')
            result['seconds_per_pass'].append(measurement['seconds'] / iterations)
    for result in results.values():
        result['median_seconds_per_pass'] = statistics.median(result['seconds_per_pass'])
    return results


def public_dataset(name):
    manifest = json.loads((ROOT / 'tests/evaluation/sources.json').read_text())
    cache = ROOT / '.cache/evaluation'
    required = [name + '_test_gold.utf8', name + '_training_words.utf8', 'score']
    by_name = {item['name']: item for item in manifest['files']}
    for filename in required:
        path = cache / filename
        if not path.is_file() or sha(path.read_bytes()) != by_name[filename]['sha256']:
            raise ValueError(f'missing or changed evaluation input: {path}; run make fetch-eval')
    return cache / required[0], cache / required[1], cache / 'score', by_name[required[0]]['url']


def machine_info(cc):
    def sysctl(name):
        result = subprocess.run(['sysctl', '-n', name], capture_output=True, text=True)
        return result.stdout.strip() if not result.returncode else None
    return {'system': platform.system(), 'release': platform.mac_ver()[0] or platform.release(),
            'architecture': platform.machine(),
            'cpu': sysctl('machdep.cpu.brand_string') if sys.platform == 'darwin' else platform.processor(),
            'memory_bytes': sysctl('hw.memsize') if sys.platform == 'darwin' else None,
            'compiler': run(cc + ['--version']).stdout.decode().splitlines()[0], 'optimization': '-O2', 'threads': 1}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dict', type=Path, default=ROOT / 'dict/build/dict.xdb')
    parser.add_argument('--dataset', choices=('pku', 'msr'), default='pku')
    parser.add_argument('--reference', type=Path, help='Override dataset with UTF-8 gold, whitespace-separated words per line')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--samples', type=int, default=7)
    parser.add_argument('--seconds', type=float, default=1.0, help='Target timed seconds per sample')
    args = parser.parse_args(argv)
    if args.samples < 1 or not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error('samples and seconds must be positive')
    if not args.dict.is_file():
        parser.error(f'dictionary not found: {args.dict}')
    output = (args.output or ROOT / 'build/evaluation' / ('custom' if args.reference else args.dataset)).resolve()
    output.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(os.environ.get('CC', 'cc'))
    scorer = training_words = None
    if args.reference:
        reference, corpus_source = args.reference, args.reference.name
    else:
        reference, training_words, scorer, corpus_source = public_dataset(args.dataset)
    original = reference.read_bytes()
    gold = [line.split() for line in original.decode('utf-8').splitlines() if line.split()]
    text = [''.join(words) for words in gold]
    gold_file, input_file = output / 'gold.utf8', output / 'input.utf8'
    gold_file.write_text('\n'.join(' '.join(words) for words in gold) + '\n', encoding='utf-8')
    input_file.write_text('\n'.join(text) + '\n', encoding='utf-8')
    binary = compile_engine(output, cc)
    configurations = [('scws2-standard-memory', 'memory', 'standard'),
                      ('scws2-standard-mmap', 'mmap', 'standard'),
                      ('scws2-search-memory', 'memory', 'search')]
    source_files = sorted([ROOT / 'include/scws2.h', ROOT / 'tools/bench_scws2.c', ROOT / 'tools/gen_rules.py',
                           *list((ROOT / 'src').glob('*.c')), *list((ROOT / 'src').glob('*.h'))])
    report = {'timestamp_utc': datetime.now(timezone.utc).isoformat(), 'machine': machine_info(cc),
              'current_revision': git('rev-parse', 'HEAD').decode().strip(),
              'source_sha256': {str(path.relative_to(ROOT)): sha(path.read_bytes()) for path in source_files},
              'source_modified': bool(git('status', '--porcelain', '--', *[str(path.relative_to(ROOT)) for path in source_files])),
              'corpus': {'source': corpus_source, 'source_sha256': sha(original),
                         'sentences': len(gold), 'gold_words': sum(map(len, gold)),
                         'characters': sum(map(len, text)), 'utf8_bytes_without_newlines': sum(len(s.encode()) for s in text)},
              'dictionary': {'sha256': sha(args.dict.read_bytes()), 'bytes': args.dict.stat().st_size},
              'rules_sha256': sha((ROOT / 'src/rules.ini').read_bytes()),
              'timing_protocol': f'{args.samples} interleaved shuffled rounds (seed=0); warm cache; native single thread; median',
              'results': {}}
    commands, counts = {}, {}
    for name, loading, mode in configurations:
        command = [str(binary), str(args.dict.resolve()), str(input_file), loading, mode]
        prediction = output / (name + '.utf8')
        raw_spans = run(command[:3] + ['-1'] + command[3:]).stdout
        (output / (name + '.spans')).write_bytes(raw_spans)
        predicted, positioned = decode_spans(text, raw_spans)
        prediction.write_text('\n'.join(' '.join(words) for words in predicted) + '\n', encoding='utf-8')
        quality = score_spans(gold, positioned) if mode == 'standard' else None
        result = {'quality': quality, 'tokens_per_pass': sum(map(len, predicted))}
        if scorer and name == 'scws2-standard-memory':
            official_raw = run(['perl', scorer, training_words, gold_file, prediction]).stdout
            (output / (name + '-sighan.txt')).write_bytes(official_raw)
            official = official_raw.decode('utf-8', errors='replace')
            summary = official[official.rfind('=== SUMMARY:'):]
            result['sighan_report'] = summary.strip()
            values = {field: float(re.search(pattern + r':\s*([0-9.]+)', summary).group(1))
                      for field, pattern in [('precision', 'TOTAL TEST WORDS PRECISION'),
                                             ('recall', 'TOTAL TRUE WORDS RECALL'), ('f1', 'F MEASURE')]}
            result['sighan_quality'] = values
            if any(abs(values[field] - quality[field]) > 0.00051 for field in values):
                raise ValueError('word-span metrics disagree with the SIGHAN scorer')
        counts[name] = result['tokens_per_pass']
        commands[name] = command
        report['results'][name] = result
    speeds = benchmark_all(commands, counts, args.samples, args.seconds)
    for name, result in report['results'].items():
        result['speed'] = speeds[name]
        elapsed = result['speed']['median_seconds_per_pass']
        result['speed']['mib_per_second'] = report['corpus']['utf8_bytes_without_newlines'] / elapsed / 1048576
        result['speed']['characters_per_second'] = report['corpus']['characters'] / elapsed
    if (output / 'scws2-standard-memory.utf8').read_bytes() != (output / 'scws2-standard-mmap.utf8').read_bytes():
        raise ValueError('memory and mmap segmentation differ')
    (output / 'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for name, result in report['results'].items():
        quality = result['quality']
        metrics = (f"P={quality['precision']:.4%} R={quality['recall']:.4%} F1={quality['f1']:.4%}"
                   if quality else 'overlapping output; no word-span F1')
        print(f"{name}: {metrics}; {result['speed']['mib_per_second']:.2f} MiB/s")
    print(f'Report: {output / "results.json"}')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'evaluation failed: {error}', file=sys.stderr)
        sys.exit(2)
