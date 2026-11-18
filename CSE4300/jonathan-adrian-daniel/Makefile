CC = cc
CFLAGS = -std=c11 -Wall -g -D_XOPEN_SOURCE=600 -D_DARWIN_C_SOURCE \
         -Wno-deprecated-declarations -Iinclude -Isrc

SRC = src/uthread.c $(wildcard src/policies/*.c)
OBJ = $(SRC:%.c=build/%.o)

all: build/test_basic

build/test_basic: tests/test_basic.c $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@

build/%.o: %.c include/uthread.h src/uthread_internal.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: build/test_basic
	./build/test_basic

clean:
	rm -rf build

.PHONY: all test clean
