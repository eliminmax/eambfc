# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only
.POSIX:

PREFIX = /usr/local

# This flag enables POSIX.1-2008-specific macros and features
POSIX_CFLAG = -D _POSIX_C_SOURCE=200908L

# __BACKENDS__ add backend object file to EAMBFC_DEPS
EAMBFC_DEPS = serialize.o backend_arm64.o backend_riscv64.o backend_s390x.o \
		backend_x86_64.o optimize.o err.o util.o resource_mgr.o \
		compile.o parse_args.o main.o

# if these are changed, rebuild everything
COMMON_HEADERS = err.h types.h config.h

# __BACKENDS__ add backend source file to ALL_SOURCES
ALL_SOURCES = serialize.c compile.c optimize.c err.c util.c resource_mgr.c \
		backend_arm64.c backend_riscv64.c backend_s390x.c \
		backend_x86_64.c parse_args.c main.c unit_test.c

UNIT_TEST_DEPS = $(ALL_SOURCES) $(COMMON_HEADERS) unit_test.h serialize.h

# replace default .o suffix rule to pass the POSIX flag, as adding to CFLAGS is
# overridden if CFLAGS are passed as an argument to make.
.SUFFIXES: .c.o
.c.o:
	$(CC) $(CFLAGS) $(POSIX_CFLAG) -c -o $@ $<

all: eambfc

eambfc: $(EAMBFC_DEPS)
	$(CC) $(POSIX_CFLAG) $(CFLAGS) $(LDFLAGS) -o $@ $(EAMBFC_DEPS) $(LDLIBS)

install: eambfc
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f eambfc $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	cp -f eambfc.1 $(DESTDIR)$(PREFIX)/share/man/man1/eambfc.1

version.h: version gen_version_h.sh
	./gen_version_h.sh

resource_mgr.o: resource_mgr.c $(COMMON_HEADERS)
serialize.o: serialize.c $(COMMON_HEADERS) serialize.h
compile.o: compile.c $(COMMON_HEADERS)
parse_args.o: parse_args.c parse_args.h version.h $(COMMON_HEADERS)
main.o: main.c parse_args.h $(COMMON_HEADERS)
err.o: err.c $(COMMON_HEADERS)
util.o: util.c $(COMMON_HEADERS) util.h
optimize.o: optimize.c $(COMMON_HEADERS)

# __BACKENDS__ add target for backend object file
backend_arm64.o: backend_arm64.c $(COMMON_HEADERS) serialize.h
backend_riscv64.o: backend_riscv64.c $(COMMON_HEADERS) serialize.h
backend_s390x.o: backend_s390x.c $(COMMON_HEADERS) serialize.h
backend_x86_64.o: backend_x86_64.c $(COMMON_HEADERS) serialize.h

test: eambfc
	(cd tests; make clean test)

all_arch_test: can_run_all eambfc
	(cd tests; make -s test_all)

unit_test_driver: $(UNIT_TEST_DEPS)
	$(CC) $$(llvm-config --cflags) -DBFC_TEST=1 -o $@ \
		$(LDFLAGS) unit_test.c $(ALL_SOURCES) \
		$(LDLIBS) -lcunit $$(llvm-config --ldflags --libs)

# remove eambfc and the objects it's built from, and remove test artifacts
clean:
	rm -rf $(EAMBFC_DEPS) eambfc alt-builds unit_test_driver
	(cd tests; make clean)
