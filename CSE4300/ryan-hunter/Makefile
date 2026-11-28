CC ?= cc
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDFLAGS ?=
LDLIBS ?=

SOURCES = main.c process.c system_info.c sorting.c search.c control.c display.c refresh.c
OBJECTS = $(SOURCES:.c=.o)
CORE = process.c system_info.c sorting.c search.c control.c

.PHONY: all clean test sanitize soak
all: minitask

minitask: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

tests/test_core: tests/test_core.c $(CORE) process.h system_info.h sorting.h search.h control.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. $(LDFLAGS) -o $@ tests/test_core.c $(CORE) $(LDLIBS)

tests/demo_worker: tests/demo_worker.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

test: minitask tests/test_core tests/demo_worker
	./tests/test_core
	python3 tests/test_integration.py

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test

soak: minitask
	python3 tests/test_integration.py --soak

clean:
	rm -f minitask *.o *.d tests/test_core tests/demo_worker

-include $(OBJECTS:.o=.d)
