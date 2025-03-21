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

test_driver: test_driver.o test_utils.o
	$(CC) $(CFLAGS) $(POSIX_CFLAG) $(LDFLAGS) \
		-o $@ test_driver.o test_utils.o $(LDLIBS)

test_driver.o: test_driver.c
test_utils.o: test_utils.c


# test handling of provided output suffixes
hello.elf: hello.bf
	$(EAMBFC) -j $(EAMBFC_ARGS) -s .elf hello.bf >.$@.build_err
	if [ "$$(wc -c .$@.build_err | awk '{print $$1}')" -eq 0 ]; then\
		rm .$@.build_err; else false;\
	fi
	test -e "$@"

# test handling of unseekable outputs
unseekable:
	# make a fifo (named pipe) that will be written to
	mkfifo $@
	# compile, writing to the fifo
	($(EAMBFC) -j $(EAMBFC_ARGS) $@.bf & cat $@ >$@_f; wait) \
		>.$@.build_err
	rm $@
	mv $@_f $@
	chmod u+x $@
	if [ "$$(wc -c .$@.build_err | awk '{print $$1}')" -eq 0 ]; then\
		rm .$@.build_err; else false;\
	fi

piped_in:
	mkfifo $@.bf
	(cat hello.bf > $@.bf & $(EAMBFC) -j $(EAMBFC_ARGS) $@.bf; wait) \
		>.$@.build_err
	if [ "$$(wc -c .$@.build_err | awk '{print $$1}')" -eq 0 ]; then\
		rm .$@.build_err; else false; fi
	# clean up fifo now that it's done
	rm $@.bf
# test support for alternative extensions
alternative_extension: alternative_extension.brnfck

# expect to encounter a compiler error for these, don't treat as a failure
.IGNORE: too_many_nested_loops unmatched_close unmatched_open

clean:
	rm -f .*.build_err hello rw loop null wrap wrap2 colortest \
		truthmachine too_many_nested_loops unmatched_close \
		unmatched_open unseekable alternative_extension unseekable_f \
		piped_in piped_in.bf dead_code hello.elf test_driver hello.b \
		test_driver.o test_utils.o
