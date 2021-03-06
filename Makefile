compiler       = gcc
compiler_flags = -std=c99 -pedantic -W -Wall -Wno-variadic-macros -Waggregate-return -Wbad-function-cast -Wcast-align -Wcast-qual -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winline -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-compare -Wstrict-prototypes -Wundef -Wwrite-strings

sources   = common.c err.c midi.c sds.c
headers   = $(wildcard *.h)
libraries = -lasound -lsndfile

rm = rm -f

all: send-sds receive-sds

send-sds: $(sources) $(headers)
	$(compiler) $(compiler_flags) -o $@ send-sds.c $(sources) $(libraries)

receive-sds: $(sources) $(headers)
	$(compiler) $(compiler_flags) -o $@ receive-sds.c $(sources) $(libraries)

clean:
	-$(rm) receive-sds send-sds *.log *~

image:
	docker build -t bsorahan/send-sds .

.PHONY: all clean image
