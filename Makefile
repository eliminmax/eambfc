# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only
.POSIX:

PREFIX = /usr/local

# POSIX standard tool to compile C99 code, could be any C99-compatible compiler
# which supports the POSIX-specified options
CC = c99

# This flag enables POSIX.1-2008-specific macros and features
POSIX_CFLAG = -D _POSIX_C_SOURCE=200908L

EAM_COMPILE_DEPS = serialize.o x86_64_encoders.o optimize.o err.o
EAMBFC_DEPS = eam_compile.o $(EAM_COMPILE_DEPS) main.o


# flags for some of the more specialized, non-portable builds
GCC_STRICT_FLAGS = -Wall -Wextra -Werror -Wpedantic -Winit-self -Winline       \
		-Wno-error=inline -Wundef -Wunused-macros -Wlogical-op         \
		-Wshadow -Wtrampolines -Wformat-signedness -Wcast-qual         \
		-Wnull-dereference -Wduplicated-cond -Wredundant-decls         \
		-Wduplicated-branches -Wbad-function-cast -std=c99 -fanalyzer  \
		$(POSIX_CFLAG) $(CFLAGS)

GCC_UBSAN_FLAGS = -std=c99 -fanalyzer -fsanitize=address,undefined \
		-fno-sanitize-recover=all $(POSIX_CFLAG) $(CFLAGS)

GCC_INT_TORTURE_FLAGS = -D INT_TORTURE_TEST=1 $(GCC_STRICT_FLAGS) -Wno-format \
			-Wno-pedantic -fsanitize=address,undefined

UNIBUILD_FILES = serialize.c eam_compile.c optimize.c err.c \
			x86_64_encoders.c main.c

# replace default .o suffix rule to pass the POSIX flag, as adding to CFLAGS is
# overridden if CFLAGS are passed as an argument to make.
.SUFFIXES: .c.o
.c.o:
	$(CC) $(CFLAGS) $(POSIX_CFLAG) -c -o $@ $<

all: eambfc optimize

eambfc: $(EAMBFC_DEPS)
	$(CC) $(POSIX_CFLAG) $(LDFLAGS) -o eambfc $(EAMBFC_DEPS) $(LDLIBS)

install: eambfc
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f eambfc $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	cp -f eambfc.1 $(DESTDIR)$(PREFIX)/share/man/man1/eambfc.1

config.h: config.template.h version
	if command -v git >/dev/null && [ -e .git ]; then \
		git_str="$$(git log -n1 --pretty=format:'git commit: %h')"; \
		if [ -n "$$(git status --short)" ]; then \
			git_str="$$git_str (with local changes)"; \
		fi \
	else \
		git_str='Not built from git repo'; \
	fi; \
	sed -e "/EAMBFC_VERSION/s/@@/\"$$(cat version)\"/" \
		-e "/EAMBFC_COMMIT/s/@@/\"$$git_str\"/" \
		<config.template.h >config.h

serialize.o: serialize.c
eam_compile.o: config.h x86_64_encoders.o eam_compile.c
main.o: config.h main.c
optimize.o: err.o optimize.c
x86_64_encoders.o: x86_64_encoders.c
err.o: config.h err.c

# for testing
#
# can we run x86-64 Linux binaries properly?
# not enough to check the architecture and kernel, because other systems might
# be able to emulate the architecture and/or system call interface.
# For an example of the former, see Linux on 64-bit ARM with qemu + binfmt_misc
# For an example of the latter, see FreeBSD's Linux syscall emulation.
# `make test` works in both of those example cases
create_mini_elf.o: config.h create_mini_elf.c
create_mini_elf: create_mini_elf.o eam_compile.o $(EAM_COMPILE_DEPS)
	$(CC) $(LDFLAGS) -o $@ $(POSIX_CFLAG)\
		$(EAM_COMPILE_DEPS) eam_compile.o $@.o $(LDLIBS)
mini_elf: create_mini_elf
	./create_mini_elf
can_run_linux_amd64: mini_elf
	./mini_elf && touch $@
test: can_run_linux_amd64 eambfc
	(cd tests; make clean test)

multibuild: config.h
	env SKIP_TEST=y ./multibuild.sh
multibuild_test: can_run_linux_amd64 config.h
	./multibuild.sh

optimize: optimize.c err.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(POSIX_CFLAG) \
		-D OPTIMIZE_STANDALONE -o $@ optimize.c err.o

strict: can_run_linux_amd64 config.h
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
	(cd tests; make clean test EAMBFC=../alt-builds/eambfc-$@)

int_torture_test: can_run_linux_amd64 config.h
	mkdir -p alt-builds
	@printf 'WARNING: `make $@` IS NOT PORTABLE AT ALL!\n' >&2
	gcc $(GCC_INT_TORTURE_FLAGS) $(LDFLAGS) $(UNIBUILD_FILES) \
		-o alt-builds/eambfc-$@
	(cd tests; make clean test EAMBFC=../alt-builds/eambfc-$@)

all_tests: test multibuild_test strict ubsan int_torture_test

# remove eambfc and the objects it's built from, then remove test artifacts
clean:
	rm -rf *.o eambfc alt-builds *mini_elf optimize can_run_linux_amd64
	if [ -e .git ]; then rm -f config.h; fi
	(cd tests; make clean)
