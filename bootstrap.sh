#!/bin/sh
set -eu
cd "$(dirname "$0")"
if command -v glibtoolize >/dev/null 2>&1; then
    LIBTOOLIZE=${LIBTOOLIZE:-glibtoolize}; export LIBTOOLIZE
fi
${PYTHON:-python3} -B tools/gen_rules.py --output generated/rules_data.h
autoreconf -fi
