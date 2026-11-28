#ifndef QUEUE_H
#define QUEUE_H

#include "uthread_internal.h"

typedef struct {
    tcb_t *items[MAX_THREADS];
    int head;
    int tail;
    int count;
} queue_t;

int queue_push(queue_t *q, tcb_t *t);
tcb_t *queue_pop(queue_t *q);

#endif
