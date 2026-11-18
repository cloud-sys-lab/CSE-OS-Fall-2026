CC = cc
CFLAGS = -std=c11 -Wall -g -D_XOPEN_SOURCE=600 -D_DARWIN_C_SOURCE \
         -Wno-deprecated-declarations -Iinclude -Isrc

SRC = src/uthread.c src/queue.c $(wildcard src/policies/*.c)
OBJ = $(SRC:%.c=build/%.o)
TESTS = $(patsubst %.c,build/%,$(wildcard tests/*.c))

all: $(TESTS)

build/tests/%: tests/%.c $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@

build/%.o: %.c include/uthread.h src/uthread_internal.h src/queue.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

clean:
	rm -rf build

.PHONY: all test clean
