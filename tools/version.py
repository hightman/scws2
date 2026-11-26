#!/usr/bin/env python3
"""Validate semantic release numbers; patch parity is the development policy."""
import re


def parse(value):
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', value):
        raise ValueError('version must be major.minor.patch without leading zeroes')
    return tuple(map(int, value.split('.')))


def select(current, requested=None):
    old = parse(current)
    if not old[2] % 2:
        raise ValueError('the working version must be an odd development version')
    target = parse(requested) if requested is not None else (old[0], old[1], old[2] + 1)
    if target <= old or target[2] % 2:
        raise ValueError('release version must be newer with an even patch number')
    return '.'.join(map(str, target))


def development(release):
    major, minor, patch = parse(release)
    return f'{major}.{minor}.{patch + 1}'
