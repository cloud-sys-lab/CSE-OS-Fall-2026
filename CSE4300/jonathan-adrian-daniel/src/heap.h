#ifndef HEAP_H
#define HEAP_H

#include "uthread_internal.h"

typedef struct {
    tcb_t *t;
    unsigned long seq;
} heap_entry_t;

typedef int (*heap_less_fn)(const tcb_t *a, const tcb_t *b);

typedef struct {
    heap_entry_t items[MAX_THREADS];
    int count;
    unsigned long next_seq;
    heap_less_fn less;
} heap_t;

int heap_push(heap_t *h, tcb_t *t);
tcb_t *heap_pop(heap_t *h);

#endif
