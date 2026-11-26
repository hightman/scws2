CC ?= cc
PYTHON ?= python3
PREFIX ?=
DESTDIR ?=
CFLAGS ?= -O2
.PHONY: all install clean check
all: build/scws2-dict
	$(PYTHON) -B build.py fetch
	$(PYTHON) -B build.py build --compiler build/scws2-dict
build/scws2-dict: ../tools/scws2-dict.c
	mkdir -p build
	$(CC) $(CFLAGS) -std=c11 $< -o $@
install:
	PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" MAKE="$(MAKE)" sh ./install.sh
check:
	$(PYTHON) -B build.py check build/dict.xdb
clean:
	rm -rf build
