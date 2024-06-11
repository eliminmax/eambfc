# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD
# vi: noet sw=4 ts=4 sts=4 cc=81

EAMBFC ?= ../eambfc
.POSIX:
.SUFFIXES: .bf .brnfck
.bf:
	$(EAMBFC) -kOj $< >.$*.build_err && rm .$*.build_err
.brnfck:
	$(EAMBFC) -kOj -e .brnfck $< >.$*.build_err && rm .$*.build_err

# build test assets
build-all: hello loop null rw wrap wrap2 colortest truthmachine \
	too-many-nested-loops unmatched-close unmatched-open \
	unseekable alternative-extension piped-in dead-code

test: clean build-all
	./test.sh $(EAMBFC)

dead-code: dead-code.bf
hello: hello.bf
loop: loop.bf
null: null.bf
wrap: wrap.bf
wrap2: wrap2.bf
colortest: colortest.bf
truthmachine: truthmachine.bf
too-many-nested-loops: too-many-nested-loops.bf
unmatched-close: unmatched-close.bf
unmatched-open: unmatched-open.bf
# test error from unseekable outputs
unseekable:
	# make a fifo (named pipe) that will be written to
	mkfifo $@
	# compile, writing to the fifo
	($(EAMBFC) -kOj $@.bf & cat $@ >$@_f; wait) \
		>.$@.build_err
	rm $@
	mv $@_f $@
	chmod u+x $@
	if [ "$$(wc -c .$@.build_err | awk '{print $$1}')" -eq 0 ]; then\
		rm .$@.build_err; else false;\
	fi

piped-in:
	mkfifo $@.bf
	(cat hello.bf > $@.bf & $(EAMBFC) -kOj $@.bf; wait) \
		>.$@.build_err
	if [ "$$(wc -c .$@.build_err | awk '{print $$1}')" -eq 0 ]; then\
		rm .$@.build_err; else false; fi
# test support for alternative extensions
alternative-extension: alternative-extension.brnfck

# expect to encounter a compiler error for these, don't treat as a failure
.IGNORE: too-many-nested-loops unmatched-close unmatched-open

clean:
	rm -f .*.build_err hello rw loop null wrap wrap2 colortest \
		truthmachine too-many-nested-loops unmatched-close \
		unmatched-open unseekable alternative-extension unseekable_f \
		piped-in piped-in.bf dead-code
