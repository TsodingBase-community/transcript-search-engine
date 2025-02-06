CFLAGS=-Wall -Werror -pedantic -std=c11 -D_POSIX_SOURCE -O3

all: searcher

searcher: searcher.o stb_ds.o

searcher.o: searcher.c
	gcc ${CFLAGS} -c searcher.c

stb_ds.o: stb_ds.h
	gcc ${CFLAGS} -DSTB_DS_IMPLEMENTATION -x c -c stb_ds.h
