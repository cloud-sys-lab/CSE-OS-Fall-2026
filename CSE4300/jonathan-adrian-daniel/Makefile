CC = cc
CFLAGS = -std=c11 -Wall -g -D_XOPEN_SOURCE=600 -D_DARWIN_C_SOURCE \
         -Wno-deprecated-declarations -Iinclude -Isrc

SRC = src/uthread.c src/queue.c src/heap.c $(wildcard src/policies/*.c)
OBJ = $(SRC:%.c=build/%.o)
TESTS = $(patsubst %.c,build/%,$(wildcard tests/*.c))
BENCH = build/bench/bench

all: $(TESTS)

build/tests/%: tests/%.c $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@

$(BENCH): bench/bench.c $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O2 $^ -o $@ -lm

build/%.o: %.c include/uthread.h src/uthread_internal.h src/queue.h src/heap.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

bench: $(BENCH)
	./$(BENCH)

clean:
	rm -rf build

.PHONY: all test bench clean
