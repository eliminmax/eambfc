# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD
.POSIX:

PREFIX ?= /usr/local

CFLAGS += -D _POSIX_C_SOURCE=200809L

# POSIX standard tool to compile C99 code, could be any C99-compatible compiler
# which supports the POSIX-specified options
CC ?= c99

PREFIX ?= /usr/local

eambfc: serialize.o eam_compile.o main.o
	$(CC) $(LDFLAGS) -o eambfc serialize.o eam_compile.o main.o $(LDLIBS)

install: eambfc
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f eambfc $(DESTDIR)$(PREFIX)/bin

serialize.o: serialize.c
eam_compile.o: eam_compile.c
main.o: main.c

# for testing
#
# can we run x86-64 Linux binaries properly?
# not enough to check the architecture and kernel, because other systems might
# be able to emulate the architecture and/or system call interface.
# For an example of the former, see Linux on 64-bit ARM with qemu + binfmt_misc
# Fir an example of the latter, see FreeBSD's Linux syscall emulation.
createminielf.o: createminielf.c
createminielf: createminielf.o serialize.o eam_compile.o
	$(CC) $(LDFLAGS) -o $@ serialize.o eam_compile.o $@.o $(LDLIBS)
minielf: createminielf
	./createminielf
can-run-linux-amd64:
	./minielf
test:
	./tests/test.sh


# remove eambfc and the objects it's build from, then remove test artifacts
clean:
	rm -f serialize.o eam_compile.o main.o eambfc \
		createminielf createminielf.o minielf
	make -f tests/Makefile clean
