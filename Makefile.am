AM_CPPFLAGS = -I$(builddir)/include -I$(srcdir)/include -I$(srcdir)/src -I$(srcdir)/generated
AM_CFLAGS = -Wall -Wextra
lib_LTLIBRARIES = libscws2.la
libscws2_la_SOURCES = src/scws2.c src/utf8.c src/xdb.c src/text.c src/text_cache.c src/rules.c
libscws2_la_LDFLAGS = -version-info 0:0:0 -no-undefined
include_HEADERS = include/scws2.h include/scws2_version.h
bin_PROGRAMS = scws2
scws2_SOURCES = tools/scws2.c
scws2_LDADD = libscws2.la
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = scws2.pc
BUILT_SOURCES = generated/rules_data.h
EXTRA_DIST = .gitignore \
  AGENTS.md \
  API.md \
  AUTHORS \
  COPYING \
  README.md \
  dict/FORMAT.md \
  dict/POS.md \
  dict/README.md \
  dict/build.py \
  dict/jieba.LICENSE \
  dict/overrides.tsv \
  dict/sources.lock.json \
  phpext/.gitignore \
  phpext/DESIGN.md \
  phpext/README.md \
  phpext/compat.h \
  phpext/config.m4 \
  phpext/php_scws2.h \
  phpext/scws2.c \
  phpext/tests/001-api.phpt \
  phpext/tests/002-tokens.phpt \
  phpext/tests/003-dictionaries.phpt \
  phpext/tests/004-search-lifetime.phpt \
  phpext/tests/005-object-guards.phpt \
  phpext/tests/006-paths.phpt \
  phpext/tests/007-strict-types.phpt \
  phpext/tests/008-text-cache.phpt \
  phpext/tests/009-named-arguments.phpt \
  phpext/tests/010-memory-limit.phpt \
  phpext/tests/common.inc \
  src/RULES.md \
  src/rules.h \
  src/rules.ini \
  src/utf8.h \
  src/xdb.h \
  tests/cache_api.c \
  tests/cases/README.md \
  tests/cases/segmentation.json \
  tests/core_api.c \
  tests/evaluation/README.md \
  tests/evaluation/msr.json \
  tests/evaluation/pku.json \
  tests/evaluation/sources.json \
  tests/test_build.py \
  tests/test_core.py \
  tests/test_dictionary.py \
  tests/test_evaluation.py \
  tests/test_rules.py \
  tests/test_text_cache.py \
  tests/text_dict.c \
  tools/bench_scws2.c \
  tools/check_segments.py \
  tools/evaluate.py \
  tools/fetch_eval.py \
  tools/gen_rules.py \
  tools/scws2-dict.c \
  VERSION \
  bootstrap.sh \
  include/scws2_version.h.in \
  generated/rules_data.h \
  scws2.pc.in \
  release.sh \
  tools/release.py \
  dict/Makefile \
  dict/install.sh \
  tools/version.py \
  tests/test_release.py \
  .github/workflows/check.yml

# The source archive contains this table; Python is needed only after rule edits.
$(srcdir)/generated/rules_data.h: $(srcdir)/src/rules.ini $(srcdir)/tools/gen_rules.py
	$(PYTHON) -B $(srcdir)/tools/gen_rules.py --output $@
PYTHON = python3
rules:
	$(PYTHON) -B $(srcdir)/tools/gen_rules.py --output $(srcdir)/generated/rules_data.h
check-local:
	$(PYTHON) -B $(srcdir)/tools/gen_rules.py --check
	$(PYTHON) -B -m unittest discover -s $(srcdir)/tests -p 'test_*.py' -v
cases: scws2
	$(PYTHON) -B $(srcdir)/tools/check_segments.py --scws2 ./scws2 -d $(srcdir)/dict/build/dict.xdb --observe
print-prefix:
	@printf '%s\n' '$(prefix)'
.PHONY: rules cases print-prefix

fetch-eval:
	$(PYTHON) -B $(srcdir)/tools/fetch_eval.py
eval:
	$(PYTHON) -B $(srcdir)/tools/evaluate.py --dict $(srcdir)/dict/build/dict.xdb --dataset pku $(EVAL_ARGS)
	$(PYTHON) -B $(srcdir)/tools/evaluate.py --dict $(srcdir)/dict/build/dict.xdb --dataset msr $(EVAL_ARGS)

CONFIGURE_DEPENDENCIES = $(srcdir)/VERSION
EXTRA_DIST += $(wildcard $(srcdir)/dict/LAST_RELEASE.json)
