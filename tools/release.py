#!/usr/bin/env python3
"""Prepare verified release assets; --publish explicitly creates commits, tag and GitHub release."""
import argparse
from datetime import datetime, timedelta, timezone
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
from version import development, select

ROOT = Path(__file__).resolve().parents[1]


def run(*args, cwd=None, capture=False):
    result = subprocess.run(list(map(str, args)), cwd=ROOT if cwd is None else cwd, check=True,
                            stdout=subprocess.PIPE if capture else None, text=capture)
    return result.stdout.strip() if capture else None


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dict_identity(directory):
    manifest = json.loads((directory / 'manifest.json').read_text())
    provenance = {k: manifest[k] for k in ('pipeline_version', 'format', 'source', 'overrides_sha256', 'pos', 'score')}
    payload = [sha(directory / 'dict.xdb'), sha(directory / 'LICENSE.jieba'), provenance]
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()


def dictionary_archive(bundle, output, epoch):
    name = output.name[:-7]
    with output.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=epoch) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as archive:
            for path in sorted(bundle.iterdir()):
                info = archive.gettarinfo(str(path), arcname=name + '/' + path.name)
                info.uid = info.gid = 0; info.uname = info.gname = ''; info.mtime = epoch
                with path.open('rb') as source:
                    archive.addfile(info, source)


def publish(target, following, tag, assets, checksums, notes_file, record, output):
    (ROOT / 'VERSION').write_text(target + '\n')
    run('git', 'add', 'VERSION'); run('git', 'commit', '-m', 'Release ' + target)
    run('git', 'tag', '-a', tag, '-m', 'scws2 ' + target)
    run('git', 'push', '--atomic', 'origin', 'main', 'refs/tags/' + tag)
    # A network failure leaves the release commit/tag intact; never force-rewrite them.
    run('gh', 'release', 'create', tag, '--verify-tag', '--title', 'scws2 ' + target,
        '--notes-file', notes_file, *[output / a.name for a in assets], checksums)
    (ROOT / 'dict').mkdir(exist_ok=True)
    (ROOT / 'dict/LAST_RELEASE.json').write_text(json.dumps(record, indent=2) + '\n')
    (ROOT / 'VERSION').write_text(following + '\n')
    run('git', 'add', 'VERSION', 'dict/LAST_RELEASE.json')
    run('git', 'commit', '-m', 'Start development ' + following)
    run('git', 'push', 'origin', 'main')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version', nargs='?')
    parser.add_argument('--publish', action='store_true', help='commit even version, tag, push and publish, then commit odd version')
    parser.add_argument('--dry-run', action='store_true', help='validate and print chosen versions without building or writing')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    args = parser.parse_args(argv)
    current = (ROOT / 'VERSION').read_text().strip()
    target = select(current, args.version)
    following = development(target)
    tag = 'v' + target
    print(f'Development {current} -> release {target} ({tag}) -> development {following}', flush=True)
    if run('git', 'status', '--porcelain', capture=True):
        raise ValueError('working tree must be clean, including untracked files')
    if subprocess.run(['git', 'rev-parse', '--verify', '--quiet', 'refs/tags/' + tag], cwd=ROOT,
                      stdout=subprocess.DEVNULL).returncode == 0:
        raise ValueError(f'tag already exists: {tag}')
    if args.dry_run:
        return
    if args.publish:
        if not shutil.which('gh'):
            raise ValueError('GitHub CLI gh is required for --publish')
        if run('git', 'branch', '--show-current', capture=True) != 'main':
            raise ValueError('--publish requires main')
        run('gh', 'auth', 'status')
        run('git', 'fetch', 'origin', 'main', '--tags')
        if subprocess.run(['git', 'rev-parse', '--verify', '--quiet', 'refs/tags/' + tag], cwd=ROOT, stdout=subprocess.DEVNULL).returncode == 0:
            raise ValueError(f'tag already exists after fetching: {tag}')
        if run('git', 'rev-list', '--count', 'HEAD..origin/main', capture=True) != '0':
            raise ValueError('main is behind origin/main')
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    epoch = int(run('git', 'show', '-s', '--format=%ct', 'HEAD', capture=True))
    with tempfile.TemporaryDirectory(prefix='scws2-release-') as temporary:
        work = Path(temporary)
        archive = subprocess.check_output(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT)
        with tarfile.open(fileobj=io.BytesIO(archive)) as source:
            source.extractall(work)
        (work / 'VERSION').write_text(target + '\n')
        run('sh', 'bootstrap.sh', cwd=work)
        prefix = work / 'installed'
        run('./configure', '--prefix=' + str(prefix), cwd=work)
        run('make', '-j2', cwd=work)
        run('make', 'check', cwd=work)
        run('make', 'install', cwd=work)
        run('make', 'dist-gzip', cwd=work)
        core = work / f'scws2-{target}.tar.gz'
        # Verify the shipped archive, including an out-of-tree, Python-disabled build.
        unpack = work / 'unpack'; unpack.mkdir()
        with tarfile.open(core) as source:
            source.extractall(unpack)
        shipped = unpack / f'scws2-{target}'
        build = work / 'without-python'; build.mkdir()
        run(shipped / 'configure', '--prefix=' + str(work / 'no-python-install'), cwd=build)
        run('make', '-j2', 'PYTHON=false', 'AUTOCONF=false', 'AUTOMAKE=false', 'ACLOCAL=false', 'AUTOHEADER=false', cwd=build)
        run('make', 'install', 'PYTHON=false', 'AUTOCONF=false', 'AUTOMAKE=false', 'ACLOCAL=false', 'AUTOHEADER=false', cwd=build)
        # PHP uses only installed headers/shared library; no engine sources or Python.
        if not shutil.which('phpize'):
            raise ValueError('phpize is required to verify a release')
        run('phpize', cwd=shipped / 'phpext')
        run('./configure', '--with-scws2=' + str(prefix), cwd=shipped / 'phpext')
        run('make', '-j2', cwd=shipped / 'phpext')
        environment = dict(os.environ, NO_INTERACTION='1', REPORT_EXIT_STATUS='1')
        subprocess.run(['make', 'test'], cwd=shipped / 'phpext', env=environment, check=True)
        for candidate in (ROOT / 'dict/.cache', ROOT / '.cache/dict'):
            if candidate.is_dir():
                shutil.copytree(candidate, work / 'dict/.cache', dirs_exist_ok=True)
                break
        run('make', cwd=work / 'dict')
        identity = dict_identity(work / 'dict/build')
        previous_file = ROOT / 'dict/LAST_RELEASE.json'
        previous = json.loads(previous_file.read_text()) if previous_file.exists() else {}
        notes = [f'# scws2 {target}', '', 'Core source archive builds without Python.']
        assets = [core]
        record = previous
        if previous.get('identity') == identity:
            notes += ['', 'Dictionary unchanged: ' + previous['url']]
        else:
            date = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d')
            filename = f'scws2-dict-{date}.tar.gz'
            if previous.get('asset') == filename:
                raise ValueError('a different dictionary was already released today; do not overwrite its asset')
            bundle = work / 'dictionary-package'; bundle.mkdir()
            for name in ('dict.xdb', 'LICENSE.jieba', 'manifest.json'):
                shutil.copy2(work / 'dict/build' / name, bundle / name)
            shutil.copy2(work / 'dict/install.sh', bundle / 'install.sh')
            (bundle / 'Makefile').write_text('PREFIX ?=\nDESTDIR ?=\n.PHONY: all install\nall:\n\t@test -f dict.xdb\ninstall:\n\tPREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" sh ./install.sh\n')
            (bundle / 'README').write_text('Prebuilt scws2 dictionary (XDB2 schema 4).\nmake install PREFIX=/path/to/scws2\nNo Python or compiler is required for installation.\n')
            package = work / filename
            dictionary_archive(bundle, package, epoch); assets.append(package)
            repository = run('gh', 'repo', 'view', '--json', 'nameWithOwner', '--jq', '.nameWithOwner', capture=True) if args.publish else 'hightman/scws2'
            record = {'identity': identity, 'asset': filename, 'tag': tag, 'sha256': sha(package),
                      'url': f'https://github.com/{repository}/releases/download/{tag}/{filename}'}
            notes += ['', 'Dictionary: ' + record['url']]
        for asset in assets:
            destination = output / asset.name
            shutil.copy2(asset, destination)
        checksums = output / 'SHA256SUMS'
        checksums.write_text(''.join(f'{sha(output / a.name)}  {a.name}\n' for a in assets))
        notes_file = output / 'RELEASE.md'; notes_file.write_text('\n'.join(notes) + '\n')
        (output / 'dictionary-release.json').write_text(json.dumps(record, indent=2) + '\n')
        if args.publish:
            publish(target, following, tag, assets, checksums, notes_file, record, output)
    print('Verified assets: ' + str(output))
    if not args.publish:
        print('Preparation only: no commits, tags, pushes or GitHub Release were created.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f'ERROR: {error}')
