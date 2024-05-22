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

clean:
	rm -f serialize.o eam_compile.o main.o eambfc
