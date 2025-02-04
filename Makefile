# SPDX-FileCopyrightText: 2024-2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only
.POSIX:

PREFIX = /usr/local

# This flag enables POSIX.1-2008-specific macros and features
POSIX_CFLAG = -D _POSIX_C_SOURCE=200908L

# __BACKENDS__
BACKENDS = backend_arm64.o backend_s390x.o backend_x86_64.o

COMPILE_DEPS = serialize.o $(BACKENDS) optimize.o err.o util.o resource_mgr.o
EAMBFC_DEPS = compile.o $(COMPILE_DEPS) main.o


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

# __BACKENDS__
UNIBUILD_FILES = serialize.c compile.c optimize.c err.c util.c resource_mgr.c \
			backend_arm64.c backend_s390x.c backend_x86_64.c main.c

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
compile.o: util.h backend_x86_64.o compile.c
main.o: version.h main.c
err.o: err.c
util.o: util.h util.c
optimize.o: err.o util.h util.o optimize.c
# __BACKENDS__
backend_arm64.o: backend_arm64.c
backend_s390x.o: backend_s390x.c
backend_x86_64.o: backend_x86_64.c

# for testing
#
# can we run x86-64 Linux binaries properly?
# not enough to check the architecture and kernel, because other systems might
# be able to emulate the architecture and/or system call interface.
# For an example of the former, see Linux on 64-bit ARM with qemu + binfmt_misc
# For an example of the latter, see FreeBSD's Linux syscall emulation.
# `make test` works in both of those example cases
create_mini_elf.o: create_mini_elf.c
create_mini_elf: create_mini_elf.o compile.o $(COMPILE_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(POSIX_CFLAG)\
		$(COMPILE_DEPS) compile.o $@.o $(LDLIBS)
mini_elf: create_mini_elf
	./create_mini_elf
can_run_linux_amd64: mini_elf
	./mini_elf && touch $@
test: can_run_linux_amd64 eambfc
	(cd tests; make clean test)

multibuild:
	env SKIP_TEST=y ./multibuild.sh
multibuild_test: can_run_linux_amd64
	./multibuild.sh

strict: can_run_linux_amd64
	mkdir -p alt-builds
	@printf 'WARNING: `make $@` IS NOT PORTABLE!\n' >&2
	gcc $(GCC_STRICT_FLAGS) $(LDFLAGS) $(UNIBUILD_FILES) \
		-o alt-builds/eambfc-$@
	(cd tests; make clean test EAMBFC=../alt-builds/eambfc-$@)

ubsan: can_run_linux_amd64 config.h
	mkdir -p alt-builds
	@printf 'WARNING: `make $@` IS NOT PORTABLE AT ALL!\n' >&2
	gcc $(GCC_UBSAN_FLAGS) $(LDFLAGS) $(UNIBUILD_FILES) \
		-o alt-builds/eambfc-$@
	(cd tests; make EAMBFC=../alt-builds/eambfc-$@ clean test)

int_torture_test: can_run_linux_amd64 config.h
	mkdir -p alt-builds
	@printf 'WARNING: `make $@` IS NOT PORTABLE AT ALL!\n' >&2
	gcc $(GCC_INT_TORTURE_FLAGS) $(LDFLAGS) $(UNIBUILD_FILES) \
		-o alt-builds/eambfc-$@
	(cd tests; make EAMBFC=../alt-builds/eambfc-$@ clean test)

all_tests: test multibuild_test strict ubsan int_torture_test

# remove eambfc and the objects it's built from, then remove test artifacts
clean:
	rm -rf $(EAMBFC_DEPS) eambfc alt-builds create_mini_elf.o \
	    create_mini_elf mini_elf can_run_linux_amd64
	(cd tests; make clean)
