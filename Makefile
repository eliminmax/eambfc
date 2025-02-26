# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only
.POSIX:

PREFIX = /usr/local

# This flag enables POSIX.1-2008-specific macros and features
POSIX_CFLAG = -D _POSIX_C_SOURCE=200908L

# __BACKENDS__ add backend object file to BACKENDS
BACKENDS = backend_arm64.o backend_riscv64.o backend_s390x.o backend_x86_64.o


EAMBFC_DEPS = serialize.o $(BACKENDS) optimize.o err.o util.o resource_mgr.o \
	      compile.o parse_args.o main.o


# flags for some of the more specialized, non-portable builds
GCC_STRICT_FLAGS = -Wall -Wextra -Wpedantic -Werror -std=c99 -fanalyzer        \
		-Wformat-truncation=2 -Wduplicated-branches -Wshadow           \
		-Wformat-overflow=2 -Wformat-signedness -Wbad-function-cast    \
		-Wnull-dereference -Wredundant-decls -Wduplicated-cond         \
		-Warray-bounds=2 -Wuninitialized -Wunused-macros -Wformat=2    \
		-Wtrampolines -Wlogical-op -Winit-self -Wcast-qual -Wundef     \
		$(POSIX_CFLAG) $(CFLAGS)

GCC_UBSAN_FLAGS = -std=c99 -fanalyzer -fsanitize=address,undefined \
		-fno-sanitize-recover=all $(POSIX_CFLAG) $(CFLAGS)

GCC_INT_TORTURE_FLAGS = -D INT_TORTURE_TEST=1 $(GCC_STRICT_FLAGS) -Wno-format \
			-Wno-pedantic -fsanitize=address,undefined

# __BACKENDS__ add backend source file to UNIBUILD_FILES
UNIBUILD_FILES = serialize.c compile.c optimize.c err.c util.c resource_mgr.c \
			backend_arm64.c backend_riscv64.c backend_s390x.c \
			backend_x86_64.c parse_args.c main.c

# replace default .o suffix rule to pass the POSIX flag, as adding to CFLAGS is
# overridden if CFLAGS are passed as an argument to make.
.SUFFIXES: .c.o
.c.o:
	$(CC) $(CFLAGS) $(POSIX_CFLAG) -c -o $@ $<

all: eambfc

eambfc: $(EAMBFC_DEPS)
	$(CC) $(POSIX_CFLAG) $(LDFLAGS) -o eambfc $(EAMBFC_DEPS) $(LDLIBS)

install: eambfc
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f eambfc $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	cp -f eambfc.1 $(DESTDIR)$(PREFIX)/share/man/man1/eambfc.1

version.h: version gen_version_h.sh
	./gen_version_h.sh

resource_mgr.o: resource_mgr.c
serialize.o: serialize.c
compile.o: compile.c
parse_args.o: version.h config.h parse_args.c
main.o: main.c
err.o: err.c
util.o: util.c
optimize.o: optimize.c
# __BACKENDS__ add target for backend object file
backend_arm64.o: backend_arm64.c
backend_riscv64.o: backend_riscv64.c
backend_s390x.o: backend_s390x.c
backend_x86_64.o: backend_x86_64.c

# for testing
#
# can we run the compiled Linux binaries properly?
# It's not enough to check the architecture and kernel, because other systems
# might be able to emulate the architecture and/or system call interface.
# For an example of the former, see Linux on 64-bit ARM with qemu + binfmt_misc
# For an example of the latter, see FreeBSD's Linux syscall emulation.
# `make test` works in both of those example cases.
#
# These mini binaries each attempt to call the exit(0) syscall for the target
# architectures.
# These binaries were hand-made in a minimal hex editor, so they are their own
# source code.
#
# The tools/execfmt_support directory has a Python script that can be used to
# help audit the binaries, in case anyone is concerned by their presence. It can
# validate that the headers are what they claim to be, and has instructions on
# how to validate the machine code itself using an external disassembler.
can_run_x86_64:
	./tools/execfmt_support/x86_64

# __BACKENDS__ create execfmt_support binary for target in and add it here
can_run_all:
	./tools/execfmt_support/arm64 && \
	./tools/execfmt_support/riscv64 && \
	./tools/execfmt_support/s390x && \
	./tools/execfmt_support/x86_64

test: can_run_x86_64 eambfc
	(cd tests; make clean test)

strict: config.h
	mkdir -p alt-builds
	@printf 'WARNING: `make $@` IS NOT PORTABLE!\n' >&2
	gcc $(GCC_STRICT_FLAGS) $(LDFLAGS) $(UNIBUILD_FILES) \
		-o alt-builds/eambfc-$@

ubsan: can_run_all config.h
	mkdir -p alt-builds
	@printf 'WARNING: `make $@` IS NOT PORTABLE AT ALL!\n' >&2
	gcc $(GCC_UBSAN_FLAGS) $(LDFLAGS) $(UNIBUILD_FILES) \
		-o alt-builds/eambfc-$@
	(cd tests; make EAMBFC=../alt-builds/eambfc-$@ test_all)

int_torture_test: can_run_all config.h
	mkdir -p alt-builds
	@printf 'WARNING: `make $@` IS NOT PORTABLE AT ALL!\n' >&2
	gcc $(GCC_INT_TORTURE_FLAGS) $(LDFLAGS) $(UNIBUILD_FILES) \
		-o alt-builds/eambfc-$@
	(cd tests; make EAMBFC=../alt-builds/eambfc-$@ test_all)

all_tests: test strict ubsan int_torture_test

all_arch_test: can_run_all eambfc
	(cd tests; make -s test_all)

# remove eambfc and the objects it's built from, and remove test artifacts
clean:
	rm -rf $(EAMBFC_DEPS) eambfc alt-builds
	(cd tests; make clean)
