
CC ?= gcc
CFLAGS ?= -g -O2 -Wall

CFLAGS += -std=c99 -D_GNU_SOURCE -fpic
LIBS = -ldl

wrapper.so: wrapper.c
	$(CC) $(CFLAGS) -shared -o $@ $^ $(LIBS)

.PHONY: clean
clean:
	rm -f wrapper.so
