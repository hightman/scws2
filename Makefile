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
SHARED = build/libscws.dylib
SHARED_FLAGS = -dynamiclib -Wl,-install_name,@rpath/libscws.dylib
else
SHARED = build/libscws.so
SHARED_FLAGS = -shared
endif
OBJECTS = build/scws.o build/utf8.o build/xdb.o build/text.o build/rules.o

.PHONY: all check test cases check-cases fetch-eval eval dict rules install clean
all: build/libscws.a $(SHARED) build/scws build/scws2-dict

build:
	mkdir -p build

build/rules_data.h: src/rules.ini tools/gen_rules.py | build
	$(PYTHON) -B tools/gen_rules.py --output $@

build/%.o: src/%.c include/scws.h src/utf8.h src/xdb.h src/rules.h build/rules_data.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -fPIC -c $< -o $@

build/libscws.a: $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

$(SHARED): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SHARED_FLAGS) $(OBJECTS) -o $@

build/scws: cli/scws.c build/libscws.a include/scws.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) cli/scws.c build/libscws.a $(LDFLAGS) -o $@

build/scws2-dict: cli/scws2-dict.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $< $(LDFLAGS) -o $@

check test: all
	$(PYTHON) -B tools/gen_rules.py --check
	$(PYTHON) -B -m unittest discover -s tests -p 'test_*.py' -v

# Observation allows differences; strict checks also track known defects.
cases: build/scws
	$(PYTHON) -B tools/check_segments.py --cases "$(CASES)" -d "$(CASE_DICT)" --observe

check-cases: build/scws
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
	install -m 644 include/scws.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 build/libscws.a $(SHARED) $(DESTDIR)$(PREFIX)/lib/
	install -m 755 build/scws $(DESTDIR)$(PREFIX)/bin/
	install -m 755 build/scws2-dict $(DESTDIR)$(PREFIX)/bin/scws-dict

clean:
	rm -rf build
