# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only
.POSIX:

PREFIX ?= /usr/local

# POSIX standard tool to compile C99 code, could be any C99-compatible compiler
# which supports the POSIX-specified options
CC ?= c99

# This flag enables POSIX.1-2008-specific macros and features
POSIX_CFLAG := -D _POSIX_C_SOURCE=200908L

# Compile-time configuration values
MAX_ERROR ?= 32
TAPE_BLOCKS ?= 8
MAX_NESTING_LEVEL ?= 64

# replace default .o suffix rule to pass the POSIX flag, as adding to CFLAGS is
# overridden if CFLAGS are passed as an argument to make.
.SUFFIXES: .c.o
.c.o:
	$(CC) $(CFLAGS) $(POSIX_CFLAG) -c -o $@ $<

eambfc: serialize.o eam_compile.o json_escape.o main.o
	$(CC) $(POSIX_CFLAG) $(LDFLAGS) -o eambfc \
		serialize.o eam_compile.o json_escape.o main.o $(LDLIBS)

install: eambfc
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f eambfc $(DESTDIR)$(PREFIX)/bin

config.h: config.template.h version
	if command -v git >/dev/null && [ -e .git ]; then \
		git_str="$$(git log -n1 --pretty=format:'git commit: %h')"; \
		if [ -n "$$(git status --short)" ]; then \
			git_str="$$git_str (with local changes)"; \
		fi \
	else \
		git_str='Not built from git repo'; \
	fi; \
	sed -e '/MAX_ERROR/s/@@/$(MAX_ERROR)/' \
		-e '/TAPE_BLOCKS/s/@@/$(TAPE_BLOCKS)/' \
		-e '/MAX_NESTING_LEVEL/s/@@/$(MAX_NESTING_LEVEL)/' \
		-e "/EAMBFC_VERSION/s/@@/\"$$(cat version)\"/" \
		-e "/EAMBFC_COMMIT/s/@@/\"$$git_str\"/" \
		<config.template.h >config.h

serialize.o: serialize.c
eam_compile.o: config.h eam_compile.c
json_escape.o: json_escape.c
main.o: config.h main.c
optimize.o: optimize.c

# for testing
#
# can we run x86-64 Linux binaries properly?
# not enough to check the architecture and kernel, because other systems might
# be able to emulate the architecture and/or system call interface.
# For an example of the former, see Linux on 64-bit ARM with qemu + binfmt_misc
# For an example of the latter, see FreeBSD's Linux syscall emulation.
# `make test` works in both of those example cases
create_mini_elf.o: config.h create_mini_elf.c
create_mini_elf: create_mini_elf.o serialize.o eam_compile.o
	$(CC) $(LDFLAGS) -o $@ $(POSIX_CFLAG)\
		serialize.o eam_compile.o $@.o $(LDLIBS)
mini_elf: create_mini_elf
	./create_mini_elf
can-run-linux-amd64: mini_elf
	./mini_elf && touch can-run-linux-amd64
test: can-run-linux-amd64 eambfc
	(cd tests; make clean test)

multibuild: config.h
	env SKIP_TEST=y ./multibuild.sh
multibuild-test: can-run-linux-amd64 config.h
	./multibuild.sh


optimize: optimize.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(POSIX_CFLAG) \
		-D OPTIMIZE_STANDALONE -o optimize optimize.c


# remove eambfc and the objects it's built from, then remove test artifacts
clean:
	rm -rf serialize.o eam_compile.o main.o eambfc alt-builds \
		create_mini_elf create_mini_elf.o json_escape.o mini_elf \
		can-run-linux-amd64 tags config.h optimize optimize.o
	(cd tests; make clean)
