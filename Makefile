CC ?= cc
AR ?= ar
PYTHON ?= python3
PREFIX ?= /usr/local
CASES ?= tests/cases/segmentation.json
CASE_DICT ?= build/dict/dictionary.xdb
EVAL_ARGS ?=
CFLAGS ?= -O2
CPPFLAGS += -Iinclude -Isrc -Ibuild
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
SYSTEM := $(shell uname -s)
ifeq ($(SYSTEM),Darwin)
SHARED = build/libscws2.dylib
SHARED_FLAGS = -dynamiclib -Wl,-install_name,@rpath/libscws2.dylib
else
SHARED = build/libscws2.so
SHARED_FLAGS = -shared
endif
OBJECTS = build/scws2.o build/utf8.o build/xdb.o build/text.o build/text_cache.o build/rules.o

.PHONY: all check test cases check-cases fetch-eval eval dict rules install clean
all: build/libscws2.a $(SHARED) build/scws2 build/scws2-dict

build:
	mkdir -p build

build/rules_data.h: src/rules.ini tools/gen_rules.py | build
	$(PYTHON) -B tools/gen_rules.py --output $@

build/%.o: src/%.c include/scws2.h src/utf8.h src/xdb.h src/rules.h build/rules_data.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -fPIC -c $< -o $@

build/libscws2.a: $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

$(SHARED): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SHARED_FLAGS) $(OBJECTS) -o $@

build/scws2: tools/scws2.c build/libscws2.a include/scws2.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) tools/scws2.c build/libscws2.a $(LDFLAGS) -o $@

build/scws2-dict: tools/scws2-dict.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(LDFLAGS) -o $@

check test: all
	$(PYTHON) -B tools/gen_rules.py --check
	$(PYTHON) -B -m unittest discover -s tests -p 'test_*.py' -v

# Observation allows differences; strict checks also track known defects.
cases: build/scws2
	$(PYTHON) -B tools/check_segments.py --cases "$(CASES)" -d "$(CASE_DICT)" --observe

check-cases: build/scws2
	$(PYTHON) -B tools/check_segments.py --cases "$(CASES)" -d "$(CASE_DICT)"

fetch-eval:
	$(PYTHON) -B tools/fetch_eval.py

eval: all
	$(PYTHON) -B tools/evaluate.py --dataset pku $(EVAL_ARGS)
	$(PYTHON) -B tools/evaluate.py --dataset msr $(EVAL_ARGS)

rules:
	$(PYTHON) -B tools/gen_rules.py --output build/rules_data.h

dict: build/scws2-dict
	$(PYTHON) -B dict/build.py build

install: all
	install -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/bin
	install -m 644 include/scws2.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 build/libscws2.a $(SHARED) $(DESTDIR)$(PREFIX)/lib/
	install -m 755 build/scws2 $(DESTDIR)$(PREFIX)/bin/
	install -m 755 build/scws2-dict $(DESTDIR)$(PREFIX)/bin/scws2-dict

clean:
	rm -rf build
