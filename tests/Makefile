# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD
# vi: noet sw=4 ts=4 sts=4 cc=81

.POSIX:

EAMBFC = ../eambfc
EAMBFC_ARGS = -Ok
.SUFFIXES: .bf .brnfck
.bf:
	$(EAMBFC) -j $(EAMBFC_ARGS) $< >.$*.build_err && rm .$*.build_err
.brnfck:
	$(EAMBFC) -j $(EAMBFC_ARGS) -e .brnfck \
		$< >.$*.build_err && rm .$*.build_err

# build test assets
build_all: hello loop wrap wrap2 colortest truthmachine dead_code piped_in \
	unmatched_close unmatched_open unseekable alternative_extension rw null

# __BACKENDS__
test_all:
	make EAMBFC_ARGS="$(EAMBFC_ARGS) -a arm64" clean build_all
	./test.sh $(EAMBFC) $(EAMBFC_ARGS) -a arm64
	make EAMBFC_ARGS="$(EAMBFC_ARGS) -a s390x" clean build_all
	./test.sh $(EAMBFC) $(EAMBFC_ARGS) -a s390x
	make EAMBFC_ARGS="$(EAMBFC_ARGS) -a x86_64" clean build_all
	./test.sh $(EAMBFC) $(EAMBFC_ARGS) -a x86_64

test: clean build_all
	./test.sh $(EAMBFC) $(EAMBFC_ARGS)

dead_code: dead_code.bf
hello: hello.bf
loop: loop.bf
null: null.bf
wrap: wrap.bf
wrap2: wrap2.bf
colortest: colortest.bf
truthmachine: truthmachine.bf
too_many_nested_loops: too_many_nested_loops.bf
unmatched_close: unmatched_close.bf
unmatched_open: unmatched_open.bf
# test error from unseekable outputs
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
		piped_in piped_in.bf dead_code
