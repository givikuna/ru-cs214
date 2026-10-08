CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

all: memgrind memtest errortest coalescetest

memgrind: memgrind.o mymalloc.o
	$(CC) $(CFLAGS) -o memgrind memgrind.o mymalloc.o

memtest: memtest.o mymalloc.o
	$(CC) $(CFLAGS) -o memtest memtest.o mymalloc.o

errortest: errortest.o mymalloc.o
	$(CC) $(CFLAGS) -o errortest errortest.o mymalloc.o

coalescetest: coalescetest.o mymalloc.o
	$(CC) $(CFLAGS) -o coalescetest coalescetest.o mymalloc.o

memgrind.o: memgrind.c mymalloc.h
	$(CC) $(CFLAGS) -c memgrind.c

memtest.o: memtest.c mymalloc.h
	$(CC) $(CFLAGS) -c memtest.c

errortest.o: errortest.c mymalloc.h
	$(CC) $(CFLAGS) -c errortest.c

coalescetest.o: coalescetest.c mymalloc.h
	$(CC) $(CFLAGS) -c coalescetest.c

mymalloc.o: mymalloc.c mymalloc.h
	$(CC) $(CFLAGS) -c mymalloc.c

clean:
	rm -f *.o memgrind memtest errortest coalescetest
