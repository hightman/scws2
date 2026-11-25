#!/usr/bin/env python3
"""Explicitly download checksum-locked SIGHAN evaluation files into the cache."""
import hashlib
import json
from pathlib import Path
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def main():
    manifest = json.loads((ROOT / 'tests/evaluation/sources.json').read_text())
    cache = ROOT / '.cache/evaluation'
    cache.mkdir(parents=True, exist_ok=True)
    for item in manifest['files']:
        target = cache / item['name']
        if target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest() == item['sha256']:
            print('cached:', item['name'])
            continue
        data = urllib.request.urlopen(item['url'], timeout=30).read()
        if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('download checksum mismatch: ' + item['name'])
        temporary = target.with_suffix(target.suffix + '.part')
        temporary.write_bytes(data)
        temporary.replace(target)
        print('downloaded:', item['name'])
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(2)
