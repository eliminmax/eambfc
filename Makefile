# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD
.POSIX:

PREFIX ?= /usr/local

CFLAGS += -D _POSIX_C_SOURCE=200809L

# POSIX standard tool to compile C99 code, could be any C99-compatible compiler
# which supports the POSIX-specified options
CC ?= c99

# Compile-time configuration values
MAX_ERROR ?= 32
TAPE_BLOCKS ?= 8


eambfc: serialize.o eam_compile.o json_escape.o main.o
	$(CC) $(LDFLAGS) -o eambfc \
		serialize.o eam_compile.o json_escape.o main.o $(LDLIBS)

install: eambfc
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f eambfc $(DESTDIR)$(PREFIX)/bin

config.h:
	sed -e '/MAX_ERROR/s/@@/$(MAX_ERROR)/' \
		-e '/TAPE_BLOCKS/s/@@/$(TAPE_BLOCKS)/' \
		-e "/EAMBFC_VERSION/s/@@/\"$$(cat version)\"/" \
		<config.template.h >config.h

serialize.o: serialize.c
eam_compile.o: config.h eam_compile.c
json_escape.o: json_escape.c
main.o: config.h main.c

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
	$(CC) $(LDFLAGS) -o $@ serialize.o eam_compile.o $@.o $(LDLIBS)
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


# remove eambfc and the objects it's built from, then remove test artifacts
clean:
	rm -rf serialize.o eam_compile.o main.o eambfc alt-builds \
		create_mini_elf create_mini_elf.o json_escape.o mini_elf \
		can-run-linux-amd64 tags config.h
	(cd tests; make clean)
