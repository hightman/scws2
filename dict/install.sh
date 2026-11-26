#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
source="$root/build"
if test -f "$root/dict.xdb"; then source="$root"; fi
test -f "$source/dict.xdb" || { echo 'No built dict.xdb; run make in dict/ first.' >&2; exit 1; }
prefix=${PREFIX:-}
if test -z "$prefix"; then
    parent=
    if test -f "$root/../config.status" && test -f "$root/../Makefile"; then
        parent=$(${MAKE:-make} -s --no-print-directory -C "$root/.." print-prefix)
    fi
    for candidate in "$parent" /usr/local /usr/local/scws2 "$HOME/.local/scws2"; do
        if test -n "$candidate" && test -f "$candidate/include/scws2.h"; then prefix=$candidate; break; fi
    done
fi
test -n "$prefix" || { echo 'Cannot find an installed scws2; specify PREFIX=/path/to/scws2.' >&2; exit 1; }
case "$prefix" in /*) ;; *) echo 'PREFIX must be an absolute path.' >&2; exit 1;; esac
destination="${DESTDIR:-}$prefix/etc"
if test -f "$destination/dict.xdb" && ! test "$source/dict.xdb" -nt "$destination/dict.xdb"; then
    echo "Dictionary already current: $destination/dict.xdb"; exit 0
fi
mkdir -p "$destination"
stage=$(mktemp -d "$destination/.scws2-dict.XXXXXX")
trap 'rm -rf "$stage"' 0
trap 'exit 1' 1 2 3 15
for name in dict.xdb LICENSE.jieba manifest.json; do
    test -f "$source/$name" || { echo "Missing dictionary bundle file: $name" >&2; exit 1; }
    install -p -m 644 "$source/$name" "$stage/$name"
done
for name in LICENSE.jieba manifest.json dict.xdb; do mv -f "$stage/$name" "$destination/$name"; done
echo "Installed dictionary: $destination/dict.xdb"
