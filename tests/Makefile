# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD
# vi: noet sw=4 ts=4 sts=4 cc=81

.POSIX:

EAMBFC = ../eambfc
EAMBFC_ARGS = -Ok
# This flag enables POSIX.1-2008-specific macros and features
POSIX_CFLAG = -D _POSIX_C_SOURCE=200908L

# replace default .o suffix rule to pass the POSIX flag, as adding to CFLAGS is
# overridden if CFLAGS are passed as an argument to make.
.SUFFIXES: .c.o
.c.o:
	$(CC) $(CFLAGS) $(POSIX_CFLAG) -c -o $@ $<

test: test_driver
	./test_driver

test_driver.o: test_driver.c
test_utils.o: test_utils.c

test_driver: test_driver.o test_utils.o
	$(CC) $(CFLAGS) $(POSIX_CFLAG) $(LDFLAGS) \
		-o $@ test_driver.o test_utils.o $(LDLIBS)

clean:
	rm -f .*.build_err hello rw loop null wrap wrap2 colortest \
		truthmachine unmatched_close unmatched_open unseekable \
		alternate_extension unseekable.bf piped_in piped_in.bf \
		dead_code hello.elf test_driver hello.b test_driver.o \
		test_utils.o unseekable alternate_extension.bf
