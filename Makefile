# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only
.POSIX:

PREFIX = /usr/local

# This flag enables POSIX.1-2008-specific macros and features
EXTRA_CFLAGS = -D _POSIX_C_SOURCE=200908L -I./include

# __BACKENDS__ add backend object file to EAMBFC_DEPS
EAMBFC_DEPS = serialize.o backend_arm64.o backend_i386.o backend_riscv64.o \
		backend_s390x.o backend_x86_64.o x86_common.o optimize.o err.o \
		util.o compile.o setup.o main.o

# __BACKENDS__ add backend source file to ALL_SOURCES
ALL_SOURCES = serialize.c compile.c optimize.c err.c util.c backend_arm64.c \
		backend_i386.c backend_riscv64.c backend_s390x.c \
		backend_x86_64.c x86_common.c setup.c main.c unit_test.c

UNIT_TEST_DEPS = $(ALL_SOURCES) err.h unit_test.h serialize.h

# replace default .o suffix rule to pass the POSIX flag, as adding to CFLAGS is
# overridden if CFLAGS are passed as an argument to make.
.SUFFIXES: .c.o
.c.o:
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

all: eambfc

eambfc: $(EAMBFC_DEPS)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -o $@ \
		$(EAMBFC_DEPS) $(LDLIBS)

install: eambfc
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f eambfc $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	cp -f eambfc.1 $(DESTDIR)$(PREFIX)/share/man/man1/eambfc.1

serialize.o: serialize.c serialize.h
compile.o: compile.c err.h
setup.o: setup.c setup.h version.h err.h
main.o: main.c setup.h err.h
err.o: err.c err.h
util.o: util.c util.h err.h
optimize.o: optimize.c err.h
x86_common.o: x86_common.c err.h serialize.h

# __BACKENDS__ add target for backend object file
backend_arm64.o: backend_arm64.c err.h serialize.h
backend_i386.o: backend_i386.c err.h serialize.h
backend_riscv64.o: backend_riscv64.c err.h serialize.h
backend_s390x.o: backend_s390x.c err.h serialize.h
backend_x86_64.o: backend_x86_64.c err.h serialize.h

test: eambfc
	(cd tests; make clean test)

unit_test_driver: $(UNIT_TEST_DEPS)
	$(CC) $$(llvm-config-19 --cflags) $$(pkgconf json-c --cflags) \
		-DBFC_TEST=1 $(CFLAGS) $(EXTRA_CFLAGS) -o $@ \
		$(LDFLAGS) $(ALL_SOURCES) $(LDLIBS) $$(pkgconf json-c --libs) \
		-lcunit $$(llvm-config-19 --ldflags --libs)

# remove eambfc and the objects it's built from, and remove test artifacts
clean:
	rm -rf $(EAMBFC_DEPS) eambfc alt-builds unit_test_driver
	(cd tests; make clean)
	# DELETE LINES AFTER THIS in "release" Makefile to avoid clobbering
	# release build's "version.h" and including nonportable tools
	(cd tools; make clean)
	rm -f release.make version.h
	# make sure to regenerate version.h right away to avoid any failures
	./gen_version_h.sh

# version.h
version.h: version gen_version_h.sh
	./gen_version_h.sh

release.make: Makefile
	sed '/DELETE[ ]LINES AFTER/,$$d;s/version[.]h //g' Makefile >$@
