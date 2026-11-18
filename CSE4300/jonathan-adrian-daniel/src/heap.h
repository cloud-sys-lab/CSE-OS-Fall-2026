#ifndef HEAP_H
#define HEAP_H

#include "uthread_internal.h"

typedef struct {
    tcb_t *t;
    unsigned long seq;
} heap_entry_t;

/*
 * Min-heap of threads keyed on expected_ms. Ties are broken by insertion
 * order so threads with equal expected_ms come out FIFO.
 */
typedef struct {
    heap_entry_t items[MAX_THREADS];
    int count;
    unsigned long next_seq;
} heap_t;

int heap_push(heap_t *h, tcb_t *t);
tcb_t *heap_pop(heap_t *h);

#endif
