#!/usr/bin/env python3
"""SCWS 2 dictionary acquisition, preparation and independent XDB2 validation."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unicodedata
import urllib.request

PIPELINE_VERSION = 3
MAIN, SEARCH, PREFIX = 1, 2, 4
SCALE = 1024
MAX_KEY = 240
MAX_POS = 2
JIEBA_POS_MAP = {'nrt': 'nr', 'nrfg': 'nr'}
HEADER = struct.Struct('<4s8I28s')
POINTER = struct.Struct('<II')
VALUE = struct.Struct('<II2s')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def atomic_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode()


def load_lock(path):
    lock = json.loads(Path(path).read_text(encoding='utf-8'))
    source = lock['source']
    if lock['schema'] != 1 or source['parser'] != 'jieba-v1':
        raise ValueError('unsupported lock schema or source parser')
    revision = source['revision']
    if len(revision) != 40 or any(c not in '0123456789abcdef' for c in revision):
        raise ValueError('source revision must be a full Git commit SHA')
    if len(source['files']) != 2 or {f['path'] for f in source['files']} != {'jieba/dict.txt', 'LICENSE'}:
        raise ValueError('lock must contain dictionary and license')
    for item in source['files']:
        if Path(item['cache_name']).name != item['cache_name']:
            raise ValueError('invalid cache name')
        expected_url = f'https://raw.githubusercontent.com/fxsjy/jieba/{revision}/{item["path"]}'
        if item['url'] != expected_url:
            raise ValueError('source URL must identify the pinned upstream file')
        if len(item['sha256']) != 64 or not 0 < item['bytes'] <= 64 * 1024 * 1024:
            raise ValueError('invalid locked checksum or size')
    return lock


def verified_source(item, cache):
    data = (Path(cache) / item['cache_name']).read_bytes()
    if len(data) != item['bytes'] or digest(data) != item['sha256']:
        raise ValueError(f'checksum/size mismatch: {item["cache_name"]}')
    return data


def fetch(lock, cache):
    for item in lock['source']['files']:
        path = Path(cache) / item['cache_name']
        if path.exists():
            verified_source(item, cache)
            continue
        with urllib.request.urlopen(item['url'], timeout=60) as response:
            data = response.read(item['bytes'] + 1)
        if len(data) != item['bytes'] or digest(data) != item['sha256']:
            raise ValueError(f'download checksum/size mismatch: {item["cache_name"]}')
        atomic_write(path, data)


def valid_term(term):
    return (bool(term) and len(term.encode('utf-8')) <= MAX_KEY
            and not any(c.isspace() or unicodedata.category(c).startswith('C') for c in term))


def valid_pos(pos, max_length=MAX_POS):
    letters = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'
    return (not pos or (len(pos) <= max_length and pos[0] in letters
                        and all(c in letters + '0123456789_-' for c in pos)))


def parse_jieba(data):
    words, stats = {}, {'rows': 0, 'duplicate_rows': 0, 'pos_conflicts': 0, 'pos_mapped_rows': 0,
                       'rejected_rows': 0, 'zero_frequency_rows': 0}
    for line_number, line in enumerate(data.decode('utf-8-sig').splitlines(), 1):
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) not in (2, 3) or not parts[1].isascii() or not parts[1].isdigit():
            raise ValueError(f'invalid jieba row {line_number}')
        term, freq = parts[0], int(parts[1])
        source_pos = parts[2] if len(parts) == 3 else ''
        pos = JIEBA_POS_MAP.get(source_pos, source_pos)
        if not valid_pos(pos):
            raise ValueError(f'invalid or unmapped POS tag {source_pos!r} at jieba row {line_number}')
        stats['rows'] += 1
        stats['pos_mapped_rows'] += source_pos != pos
        # Preserve spelling and Unicode code points. Normalization needs an offset policy.
        if not valid_term(term):
            stats['rejected_rows'] += 1
            continue
        if freq == 0:
            stats['zero_frequency_rows'] += 1
            continue
        if term in words:
            stats['duplicate_rows'] += 1
            previous = words[term]
            stats['pos_conflicts'] += previous[2] != pos
            # Keep the tag from the highest-frequency row. On ties prefer a
            # known tag, then ASCII order; never depend on source row ordering.
            if (-previous[0], not bool(previous[2]), previous[2]) <= (-freq, not bool(pos), pos):
                continue
        words[term] = (freq, MAIN | SEARCH, pos)
    if not words:
        raise ValueError('source dictionary contains no usable entries')
    return words, stats


def apply_overrides(words, data):
    rows = csv.DictReader(data.decode('utf-8').splitlines(), delimiter='\t')
    if rows.fieldnames != ['term', 'action', 'frequency', 'pos']:
        raise ValueError('overrides header must be term/action/frequency/pos (TSV)')
    seen = set()
    counts = {'upsert': 0, 'deny': 0, 'search': 0}
    for number, row in enumerate(rows, 2):
        if None in row or any(v is None for v in row.values()):
            raise ValueError(f'invalid override columns at row {number}')
        term, action, frequency = row['term'], row['action'], row['frequency']
        pos = row['pos']
        if not valid_term(term) or term in seen or action not in counts or (pos != '-' and not valid_pos(pos)):
            raise ValueError(f'invalid or repeated override at row {number}')
        seen.add(term)
        # Empty inherits an existing tag; '-' explicitly clears it.
        resolved_pos = '' if pos == '-' else pos or words.get(term, (0, 0, ''))[2]
        if action == 'upsert':
            if not frequency.isascii() or not frequency.isdigit() or int(frequency) <= 0:
                raise ValueError(f'upsert needs a positive frequency at row {number}')
            words[term] = (int(frequency), MAIN | SEARCH, resolved_pos)
        else:
            if frequency:
                raise ValueError(f'{action} must not supply frequency at row {number}')
            if action == 'deny':
                if pos:
                    raise ValueError(f'deny must not supply POS at row {number}')
                words.pop(term, None)
            else:
                words[term] = (0, SEARCH, resolved_pos)
        counts[action] += 1
    return counts


def prepare(lock, cache, overrides, output):
    inputs = {item['path']: verified_source(item, cache) for item in lock['source']['files']}
    words, stats = parse_jieba(inputs['jieba/dict.txt'])
    patch = Path(overrides).read_bytes()
    stats['overrides'] = apply_overrides(words, patch)
    total = sum(freq for freq, flags, _ in words.values() if flags & MAIN)
    count = sum(bool(flags & MAIN) for _, flags, _ in words.values())
    if not count:
        raise ValueError('no main entries after overrides')
    denominator = total + count
    lines = []
    for term in sorted(words, key=lambda word: word.encode('utf-8')):
        freq, flags, pos = words[term]
        cost = int(math.floor(-math.log((freq + 1) / denominator) * SCALE + 0.5)) if flags & MAIN else 0
        if not 0 <= cost <= 0xffffffff:
            raise ValueError('cost exceeds uint32 range')
        lines.append(f'{term}\t{cost}\t{flags}\t{pos}\n')
    lexicon = ''.join(lines).encode()
    manifest = {'pipeline_version': PIPELINE_VERSION, 'format': 'xdb2-v3',
                'pos': {'tagset': 'jieba-compact-v1', 'source_tagset': 'jieba',
                        'mapping': JIEBA_POS_MAP, 'unknown': '',
                        'max_bytes': MAX_POS, 'used_in_scoring': False},
                'score': {'scale': SCALE, 'smoothing': 1, 'main_frequency_sum': total,
                          'main_entries': count, 'rounding': 'nearest-half-up'},
                'source': lock['source'], 'overrides_sha256': digest(patch),
                'pipeline_sha256': digest(Path(__file__).read_bytes()),
                'lexicon_sha256': digest(lexicon), 'stats': stats,
                'entries': len(words)}
    output = Path(output)
    atomic_write(output / 'lexicon.tsv', lexicon)
    atomic_write(output / 'manifest.json', json_bytes(manifest))
    atomic_write(output / 'LICENSE.jieba', inputs['LICENSE'])
    return manifest


def hash_key(key, base, buckets):
    h = base
    for byte in reversed(key):
        h = ((h * 33) ^ byte) & 0x7fffffff
    return h % buckets


def check_xdb(path):
    data = Path(path).read_bytes()
    if len(data) < HEADER.size:
        raise ValueError('truncated header')
    magic, version, size, features, base, buckets, count, file_size, value_size, reserved = HEADER.unpack_from(data)
    if (magic != b'XDB2' or version != 3 or size != 64 or features != 0 or base != 0xf422f
            or value_size != VALUE.size or reserved != bytes(28) or file_size != len(data)
            or not 1 <= buckets <= 1000000 or 64 + buckets * 8 > len(data)):
        raise ValueError('invalid XDB2 header')
    floor = 64 + buckets * 8
    seen, ranges, main_count, search_count, tagged_count = set(), [], 0, 0, 0
    records = {}
    for bucket in range(buckets):
        root = POINTER.unpack_from(data, 64 + bucket * 8)
        stack = [(root, None, None)]
        while stack:
            (offset, length), lower, upper = stack.pop()
            if not offset and not length:
                continue
            if offset < floor or length < 28 or offset + length > len(data) or offset in seen:
                raise ValueError('invalid, shared or cyclic node pointer')
            seen.add(offset)
            key_size = data[offset + 16]
            if not 1 <= key_size <= MAX_KEY or length != 27 + key_size:
                raise ValueError('invalid record length')
            key = data[offset + 17:offset + 17 + key_size]
            text = key.decode('utf-8')
            if not valid_term(text) or (lower is not None and key <= lower) or (upper is not None and key >= upper):
                raise ValueError('invalid key or search tree ordering')
            if hash_key(key, base, buckets) != bucket:
                raise ValueError('record in incorrect hash bucket')
            cost, flags, pos_bytes = VALUE.unpack_from(data, offset + 17 + key_size)
            pos = pos_bytes.rstrip(b'\0').decode('ascii')
            if not valid_pos(pos) or (not flags & SEARCH and pos):
                raise ValueError('invalid POS tag or tagged prefix-only entry')
            if not flags or flags & ~7 or (flags & MAIN and not flags & SEARCH) or (not flags & MAIN and cost):
                raise ValueError('invalid entry flags or cost')
            main_count += bool(flags & MAIN)
            search_count += bool(flags & SEARCH)
            tagged_count += bool(pos)
            records[key] = (cost, flags)
            ranges.append((offset, offset + length))
            stack.append((POINTER.unpack_from(data, offset + 8), key, upper))
            stack.append((POINTER.unpack_from(data, offset), lower, key))
    if len(seen) != count:
        raise ValueError('node count mismatch')
    end = floor
    for start, next_end in sorted(ranges):
        if start != end:
            raise ValueError('overlapping, missing or unreferenced record bytes')
        end = next_end
    if end != len(data):
        raise ValueError('trailing bytes')
    expected_prefixes = set()
    for key, (_, flags) in records.items():
        if flags & (MAIN | SEARCH):
            text = key.decode('utf-8')
            expected_prefixes.update(text[:i].encode() for i in range(1, len(text)))
    if expected_prefixes != {key for key, (_, flags) in records.items() if flags & PREFIX}:
        raise ValueError('missing or redundant prefix flags')
    return {'nodes': count, 'main_entries': main_count, 'search_entries': search_count,
            'tagged_entries': tagged_count,
            'bytes': len(data), 'sha256': digest(data)}


def read_bundle(directory):
    directory = Path(directory)
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    data = (directory / 'lexicon.tsv').read_bytes()
    if digest(data) != manifest['lexicon_sha256']:
        raise ValueError('bundle lexicon checksum mismatch')
    words = {}
    # Permit comparing an earlier POS-free bundle against a new one. Binary
    # readers still require the current schema and reject the old layout.
    version = manifest['format']
    if version not in ('xdb2-v1', 'xdb2-v2', 'xdb2-v3'):
        raise ValueError('unsupported bundle format')
    for line in data.decode('utf-8').splitlines():
        parts = line.split('\t')
        if version == 'xdb2-v1':
            parts.append('')
        term, cost, flags, pos = parts
        if (not valid_term(term) or term in words or not cost.isdecimal()
                or flags not in ('2', '3') or not valid_pos(pos, max_length=31 if version == 'xdb2-v2' else MAX_POS)):
            raise ValueError('invalid bundle lexicon')
        words[term] = (int(cost), int(flags), pos)
    return manifest, words


def diff_bundles(before, after, limit):
    if limit < 0:
        raise ValueError('diff sample limit must be nonnegative')
    old_manifest, old = read_bundle(before)
    new_manifest, new = read_bundle(after)
    added, removed = sorted(new.keys() - old.keys()), sorted(old.keys() - new.keys())
    changed = sorted(term for term in old.keys() & new.keys() if old[term] != new[term])
    return {'before': old_manifest['lexicon_sha256'], 'after': new_manifest['lexicon_sha256'],
            'counts': {'added': len(added), 'removed': len(removed), 'changed': len(changed),
                       'cost_changed': sum(old[t][0] != new[t][0] for t in changed),
                       'flags_changed': sum(old[t][1] != new[t][1] for t in changed),
                       'pos_changed': sum(old[t][2] != new[t][2] for t in changed)},
            'samples': {'added': added[:limit], 'removed': removed[:limit],
                        'changed': [{'term': t, 'before': old[t], 'after': new[t]} for t in changed[:limit]]}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('fetch', 'prepare', 'build'):
        cmd = sub.add_parser(name)
        cmd.add_argument('--lock', default='dict/sources.lock.json')
        cmd.add_argument('--cache', default='.cache/dict')
        if name != 'fetch':
            cmd.add_argument('--overrides', default='dict/overrides.tsv')
            cmd.add_argument('--output', default='build/dict')
        if name == 'build':
            cmd.add_argument('--compiler', default='build/scws2-dict')
    sub.add_parser('check').add_argument('file')
    cmd = sub.add_parser('diff')
    cmd.add_argument('before')
    cmd.add_argument('after')
    cmd.add_argument('--limit', type=int, default=20)
    args = parser.parse_args()
    try:
        if args.command == 'check':
            print(json.dumps(check_xdb(args.file), ensure_ascii=False, sort_keys=True))
            return
        if args.command == 'diff':
            print(json.dumps(diff_bundles(args.before, args.after, args.limit), ensure_ascii=False, indent=2))
            return
        lock = load_lock(args.lock)
        if args.command == 'fetch':
            fetch(lock, args.cache)
            print('Pinned dictionary and license verified.')
            return
        if args.command == 'build':
            out = Path(args.output)
            out.parent.mkdir(parents=True, exist_ok=True)
            compiler = Path(args.compiler).resolve()
            # Do not touch a previous build until compilation and validation pass.
            with tempfile.TemporaryDirectory(prefix='.dict-build-', dir=out.parent) as temporary:
                stage = Path(temporary)
                manifest = prepare(lock, args.cache, args.overrides, stage)
                subprocess.run([str(compiler), str(stage / 'lexicon.tsv'), str(stage / 'dictionary.xdb')], check=True)
                manifest['xdb'] = check_xdb(stage / 'dictionary.xdb')
                if manifest['xdb']['search_entries'] != manifest['entries']:
                    raise ValueError('compiled dictionary entry count mismatch')
                manifest['compiler_sha256'] = digest(compiler.read_bytes())
                for name in ('lexicon.tsv', 'LICENSE.jieba', 'dictionary.xdb'):
                    atomic_write(out / name, (stage / name).read_bytes())
                atomic_write(out / 'manifest.json', json_bytes(manifest))
        else:
            manifest = prepare(lock, args.cache, args.overrides, args.output)
        print(json.dumps(manifest, ensure_ascii=False, sort_keys=True))
    except (OSError, ValueError, KeyError, UnicodeError, OverflowError, subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
