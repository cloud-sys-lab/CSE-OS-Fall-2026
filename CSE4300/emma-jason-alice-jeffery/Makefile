CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude

SRCS = $(wildcard src/*.c) $(wildcard src/schedulers/*.c)
OBJS = $(patsubst %.c,build/%.o,$(notdir $(SRCS)))

.PHONY: all clean test_queue

all: scheduler

scheduler: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/schedulers/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

test_queue: src/queue.c tests/test_queue.c
	$(CC) $(CFLAGS) -o $@ src/queue.c tests/test_queue.c

# test_metrics build rule and runs all tests 
test_metrics: src/metrics.c tests/test_metrics.c
	$(CC) $(CFLAGS) -o $@ src/metrics.c tests/test_metrics.c
 
# build everything, run C unit tests and Python CLI tests
test: scheduler test_queue test_metrics
	./test_queue
	./test_metrics
	python3 tests/run_tests.py
	
clean:
	rm -rf build scheduler test_queue test_metrics *.dSYM
